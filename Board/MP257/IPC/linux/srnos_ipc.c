// SPDX-License-Identifier: GPL-2.0-only
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/random.h>
#include <linux/remoteproc.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include "../abi.h"

struct srnos_endpoint {
    struct miscdevice misc;
    const char *compatible;
    unsigned int core, address;
    struct rproc *rproc;
    atomic_t writer;
};

struct srnos_file {
    struct srnos_endpoint *endpoint;
    unsigned int session[2];
};

static struct srnos_endpoint endpoints[] = {
    { .misc = {.name="srnos-m33"}, .compatible="st,stm32mp2-m33",
      .core=SRNOS_IPC_M33, .address=SRNOS_IPC_M33_ADDRESS },
    { .misc = {.name="srnos-m0"}, .compatible="st,stm32mp2-m0",
      .core=SRNOS_IPC_M0, .address=SRNOS_IPC_M0_ADDRESS },
};

/* Called under rproc->lock. Use the existing carveout mapping only while the
 * processor is running; stop/reload may free the mapping between syscalls. */
static SRNOS_IpcPage __iomem *srnos_page(struct srnos_endpoint *ep)
{
    SRNOS_IpcPage __iomem *p;

    if (ep->rproc->state != RPROC_RUNNING)
        return ERR_PTR(-ENODEV);
    p = (__force SRNOS_IpcPage __iomem *)
        rproc_da_to_va(ep->rproc, ep->address, sizeof(*p), NULL);
    if (!p || readl(&p->magic) != SRNOS_IPC_MAGIC ||
        readl(&p->version) != SRNOS_IPC_VERSION || readl(&p->core) != ep->core ||
        readl(&p->size) != sizeof(*p))
        return ERR_PTR(-EPROTO);
    return p;
}

static bool srnos_bound(const struct srnos_file *ctx, SRNOS_IpcPage __iomem *p)
{
    return readl(&p->session[0]) == ctx->session[0] &&
           readl(&p->session[1]) == ctx->session[1];
}

static void srnos_publish(struct srnos_file *ctx, SRNOS_IpcPage __iomem *p,
                          const SRNOS_IpcRequest *request)
{
    SRNOS_IpcData local;
    unsigned int i;
    unsigned int next = (readl(&p->down.mark) + 2u) & ~1u;

    if (!next) next = 2;
    local.command.session[0] = ctx->session[0];
    local.command.session[1] = ctx->session[1];
    local.command.issued_ms = readl(&p->now_ms);
    local.command.request = *request;
    writel_relaxed(next - 1u, &p->down.mark);
    wmb();
    for (i = 0; i < sizeof(local.command)/sizeof(u32); ++i)
        writel_relaxed(local.words[i], &p->down.data.words[i]);
    wmb();
    writel(next, &p->down.mark);
}

static int srnos_handshake(struct srnos_file *ctx)
{
    const SRNOS_IpcRequest hello = {.op=SRNOS_IPC_HELLO};
    struct srnos_endpoint *ep = ctx->endpoint;
    unsigned int attempt;

    do {
        get_random_bytes(ctx->session, sizeof(ctx->session));
    } while (!(ctx->session[0] || ctx->session[1]));
    for (attempt = 0; attempt <= 100; ++attempt) {
        SRNOS_IpcPage __iomem *p;
        int ret = -EAGAIN;

        mutex_lock(&ep->rproc->lock);
        p = srnos_page(ep);
        if (IS_ERR(p)) ret = PTR_ERR(p);
        else if (!attempt) srnos_publish(ctx, p, &hello);
        else if (srnos_bound(ctx, p)) ret = 0;
        mutex_unlock(&ep->rproc->lock);
        if (ret != -EAGAIN) return ret;
        usleep_range(1000, 1500);
    }
    return -ETIMEDOUT;
}

static int srnos_open(struct inode *inode, struct file *file)
{
    struct srnos_endpoint *ep = container_of(file->private_data, struct srnos_endpoint, misc);
    struct srnos_file *ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
    int ret = 0;

    if (!ctx) return -ENOMEM;
    ctx->endpoint = ep;
    if (file->f_mode & FMODE_WRITE) {
        if (atomic_cmpxchg(&ep->writer, 0, 1))
            ret = -EBUSY;
        else {
            ret = srnos_handshake(ctx);
            if (ret) atomic_set(&ep->writer, 0);
        }
    }
    if (ret) kfree(ctx);
    else file->private_data = ctx;
    return ret;
}

static int srnos_release(struct inode *inode, struct file *file)
{
    struct srnos_file *ctx = file->private_data;
    if (file->f_mode & FMODE_WRITE) atomic_set(&ctx->endpoint->writer, 0);
    kfree(ctx);
    return 0;
}

static ssize_t srnos_read(struct file *file, char __user *buf, size_t len, loff_t *offset)
{
    struct srnos_file *ctx = file->private_data;
    struct srnos_endpoint *ep = ctx->endpoint;
    SRNOS_IpcPage __iomem *p;
    SRNOS_IpcSlot __iomem *slot;
    SRNOS_IpcRead sample;
    unsigned int i;
    int ret;

    if (len != sizeof(sample) || (*offset != 0 && *offset != SRNOS_IPC_STATUS_OFFSET))
        return -EINVAL;
    mutex_lock(&ep->rproc->lock);
    p = srnos_page(ep);
    if (IS_ERR(p)) { ret = PTR_ERR(p); goto unlock; }
    slot = *offset == 0 ? &p->up : &p->status;
    sample.slot.mark = readl(&slot->mark);
    if (!sample.slot.mark) { ret = -ENODATA; goto unlock; }
    if (sample.slot.mark & 1u) { ret = -EAGAIN; goto unlock; }
    rmb();
    for (i = 0; i < SRNOS_IPC_WORDS; ++i)
        sample.slot.data.words[i] = readl_relaxed(&slot->data.words[i]);
    rmb();
    if (sample.slot.mark != readl(&slot->mark)) { ret = -EAGAIN; goto unlock; }
    sample.now_ms = readl(&p->now_ms);
    ret = 0;
unlock:
    mutex_unlock(&ep->rproc->lock);
    if (ret) return ret;
    return copy_to_user(buf, &sample, sizeof(sample)) ? -EFAULT : sizeof(sample);
}

static ssize_t srnos_write(struct file *file, const char __user *buf, size_t len, loff_t *offset)
{
    struct srnos_file *ctx = file->private_data;
    struct srnos_endpoint *ep = ctx->endpoint;
    SRNOS_IpcPage __iomem *p;
    SRNOS_IpcRequest req;
    unsigned int op = ep->core == SRNOS_IPC_M33 ? SRNOS_IPC_TARGET : SRNOS_IPC_UART;
    unsigned int limit = op == SRNOS_IPC_TARGET ? 0xffffu : sizeof(req.data);
    int ret = 0;

    bool angle;

    if (len != sizeof(req) || *offset != 0) return -EINVAL;
    if (copy_from_user(&req, buf, sizeof(req))) return -EFAULT;
    angle = ep->core == SRNOS_IPC_M33 &&
                 (req.op == SRNOS_IPC_ANGLE_PREVIEW || req.op == SRNOS_IPC_ANGLE_DRIVE);
    if ((!angle && req.op != op) || req.mask_or_length > limit || !req.ttl_ms ||
        req.ttl_ms > (angle ? SRNOS_IPC_CONTROL_MAX_TTL_MS : SRNOS_IPC_MAX_TTL_MS)) return -EINVAL;
    mutex_lock(&ep->rproc->lock);
    p = srnos_page(ep);
    if (IS_ERR(p)) ret = PTR_ERR(p);
    else if (!srnos_bound(ctx, p)) ret = -EPIPE;
    else if (angle && !readl(&p->up.data.motor.profile)) ret = -EOPNOTSUPP;
    else srnos_publish(ctx, p, &req);
    mutex_unlock(&ep->rproc->lock);
    return ret ? ret : (ssize_t)len;
}

static const struct file_operations srnos_ops = {
    .owner=THIS_MODULE, .open=srnos_open, .release=srnos_release,
    .read=srnos_read, .write=srnos_write, .llseek=noop_llseek,
};

static void srnos_unregister(unsigned int count)
{
    while (count) {
        struct srnos_endpoint *ep = &endpoints[--count];
        misc_deregister(&ep->misc);
        rproc_put(ep->rproc);
    }
}

static int __init srnos_init(void)
{
    unsigned int i;
    int ret = -ENODEV;
    for (i = 0; i < ARRAY_SIZE(endpoints); ++i) {
        struct srnos_endpoint *ep = &endpoints[i];
        struct device_node *node = of_find_compatible_node(NULL, NULL, ep->compatible);
        if (!node) { ret = -ENODEV; break; }
        ep->rproc = rproc_get_by_phandle(node->phandle);
        of_node_put(node);
        if (!ep->rproc) { ret = -ENODEV; break; }
        ep->misc.minor = MISC_DYNAMIC_MINOR;
        ep->misc.mode = 0600;
        ep->misc.fops = &srnos_ops;
        ret = misc_register(&ep->misc);
        if (ret) { rproc_put(ep->rproc); break; }
    }
    if (ret) srnos_unregister(i);
    return ret;
}
static void __exit srnos_exit(void) { srnos_unregister(ARRAY_SIZE(endpoints)); }
module_init(srnos_init);
module_exit(srnos_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SirinEmbedOS MP257 remoteproc latest-value snapshots");
