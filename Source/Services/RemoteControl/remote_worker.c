#include "thread_internal.h"
#include "SirinEmbedOS/Services/RemoteControl/Startup.h"
#include "SirinEmbedOS/Services/RemoteControl/DR16/DR16.h"
#include "SirinEmbedOS/Services/RemoteControl/VT03/VT03.h"
#include "SirinEmbedOS/Services/RemoteControl/WBus/WBus.h"
#include "SirinEmbedOS/Services/SirinRMT.h"
#include "../VideoLink/video_link_internal.h"
#include "../Message/message_internal.h"
#include "cmsis_os2.h"
#include <string.h>
static SRNOS_DR16T dr16;
static SRNOS_VT03T vt03;
static SRNOS_WBus wbus;
static SRNOS_InputSource dr16_source,vt03_source,wbus_source;
static bool configured,started;

static void dr16_frame(const SRNOS_DR16Dec *d,void *unused)
{
    (void)unused;SRNOS_InputFrame f={.channel_mask=0x7f,.update_buttons=true,
        .buttons_valid=0x3ffff,.relative_mask=7};
    for(unsigned i=0;i<5;i++)f.raw[i]=d->ch[i];
    f.raw[5]=d->s1;f.raw[6]=d->s2;
    static const uint8_t keys[]={SRNOS_INPUT_KEY_W,SRNOS_INPUT_KEY_S,SRNOS_INPUT_KEY_A,
        SRNOS_INPUT_KEY_D,SRNOS_INPUT_KEY_Q,SRNOS_INPUT_KEY_E,SRNOS_INPUT_KEY_SHIFT,SRNOS_INPUT_KEY_CTRL};
    for(unsigned i=0;i<8;i++)if(d->keys&(1u<<i))f.buttons|=UINT64_C(1)<<keys[i];
    f.buttons|=d->keys&0xff00u;
    f.buttons|=(uint64_t)!!d->ml<<16|(uint64_t)!!d->mr<<17;
    f.relative[0]=d->mx;f.relative[1]=d->my;f.relative[2]=d->mz;
    (void)SRNOS_InputPublish(dr16_source,&f,SRNOS_TimeMs());
}
static void vt03_frame(const SRNOS_VT03Dec *d,void *unused)
{
    (void)unused;SRNOS_InputFrame f={.channel_mask=0x3f,.update_buttons=true,
        .buttons_valid=0x7fffff,.relative_mask=7,.buttons=d->keys};
    for(unsigned i=0;i<4;i++)f.raw[i]=(int16_t)d->ch[i];
    f.raw[4]=(int16_t)d->wheel;f.raw[5]=d->gear;
    f.buttons|=(uint64_t)!!d->ml<<16|(uint64_t)!!d->mr<<17|(uint64_t)!!d->mm<<18|
        (uint64_t)!!d->pause<<19|(uint64_t)!!d->fn1<<20|(uint64_t)!!d->fn2<<21|(uint64_t)!!d->trigger<<22;
    f.relative[0]=d->mx;f.relative[1]=d->my;f.relative[2]=d->mz;
    (void)SRNOS_InputPublish(vt03_source,&f,SRNOS_TimeMs());
}
static void wbus_frame(const SRNOS_WBusDec *d,void *unused)
{
    /* Receiver-reported loss is not a new valid sample: leave all Input
     * timestamps/fields/epoch untouched. The framer still counts this frame.
     * FAILSAFE takes precedence, including frames carrying both flags. UART
     * stream discontinuities are independently invalidated by remote_step. */
    if(d->frame_lost&&!d->failsafe)return;
    (void)unused;SRNOS_InputFrame f={.channel_mask=0xffff,.update_buttons=true,
        .buttons_valid=UINT64_C(3)<<32,.buttons=(uint64_t)d->digital17<<32|(uint64_t)d->digital18<<33,
        .flags=d->failsafe?SRNOS_INPUT_FAILSAFE:0};
    for(unsigned i=0;i<16;i++)f.raw[i]=(int16_t)d->ch[i];
    (void)SRNOS_InputPublish(wbus_source,&f,SRNOS_TimeMs());
}
static bool register_physical(const char *name,unsigned channels,unsigned axes,uint64_t buttons,
    uint16_t timeout,SRNOS_InputSource *source)
{
    SRNOS_InputDescriptor d={.timeout_ms=timeout,.channel_count=(uint8_t)channels,
        .relative_mask=7,.buttons=buttons};
    memcpy(d.name,name,strlen(name)+1);
    for(unsigned i=0;i<axes;i++)d.channels[i]=(SRNOS_InputChannelDescriptor){364,1024,1684,SRNOS_INPUT_CHANNEL_AXIS,false};
    for(unsigned i=5;i<channels;i++)d.channels[i].kind=SRNOS_INPUT_CHANNEL_POSITION;
    return SRNOS_InputRegister(&d,source)==SRNOS_INPUT_OK;
}
int SRNOS_RemoteControlConfigure(const SRNOS_RemoteControlConfig *cfg)
{
    if(configured||!cfg||!cfg->offline_ms||cfg->offline_ms>60000)return -1;
    if(cfg->video_link_enabled&&!cfg->vt03_port)return -1;
    if(cfg->dr16_port) {
        SRNOS_DR16Config c={.channel=SRNOS_SerialGetChannel(cfg->dr16_port),.frame_callback=dr16_frame};
        if(!register_physical("dr16",7,4,0x3ffff,cfg->offline_ms,&dr16_source)||!SRNOS_DR16Init(&dr16,&c))return -1;
    }
    if(cfg->vt03_port) {
        SRNOS_VT03Config c={.channel=SRNOS_SerialGetChannel(cfg->vt03_port),.frame_callback=vt03_frame};
        if(cfg->video_link_enabled){if(!SRNOS_VideoLinkPrepare(c.channel))return -1;c.a5_callback=SRNOS_VideoLinkOnFrame;}
        if(!register_physical("vt03",6,5,0x7fffff,cfg->offline_ms,&vt03_source)||!SRNOS_VT03Init(&vt03,&c))return -1;
    }
    if(cfg->wbus_port) {
        SRNOS_InputDescriptor d={.name="wbus",.timeout_ms=cfg->offline_ms,.channel_count=16,.buttons=UINT64_C(3)<<32};
        memcpy(d.channels,cfg->wbus_channels,sizeof(d.channels));
        if(SRNOS_InputRegister(&d,&wbus_source)!=SRNOS_INPUT_OK||
           SRNOS_WBusInit(&wbus,SRNOS_SerialGetChannel(cfg->wbus_port))!=SRNOS_SERIAL_OK)return -1;
        wbus.framer.frame_callback=wbus_frame;
    }
    const char *names[]={"sirinrmt.host","sirinrmt.uart","sirinrmt.udp","sirinrmt.video"};
    for(unsigned i=0;i<4;i++)if(cfg->sirinrmt_transports&(1u<<i)) {
        SRNOS_InputSource source;
        if(SRNOS_SirinRMTRegister(names[i],(uint8_t)(1u<<i),0,cfg->offline_ms,&source)!=SRNOS_INPUT_OK)return -1;
    }
    configured=true;return 0;
}
static void remote_step(void *arg)
{
    (void)arg;uint32_t now=SRNOS_TimeMs();
    if(dr16.channel) {
        uint32_t resets=dr16.diag.stream_resets;
        (void)SRNOS_DR16PollDec(&dr16,NULL,now);
        if(resets!=dr16.diag.stream_resets)(void)SRNOS_InputInvalidate(dr16_source,SRNOS_INPUT_FRAME_LOST);
    }
    if(vt03.channel) {
        uint32_t resets=vt03.diag.stream_resets;
        (void)SRNOS_VT03PollDec(&vt03,NULL,now);
        if(resets!=vt03.diag.stream_resets)(void)SRNOS_InputInvalidate(vt03_source,SRNOS_INPUT_FRAME_LOST);
    }
    if(wbus.channel) {
        SRNOS_WBusDec decoded;uint32_t resets=wbus.diag.stream_resets;
        (void)SRNOS_WBusPoll(&wbus,&decoded,now);
        if(resets!=wbus.diag.stream_resets)(void)SRNOS_InputInvalidate(wbus_source,SRNOS_INPUT_FRAME_LOST);
    }
    SRNOS_MessagePollUART();SRNOS_VideoLinkStep(now);
}
static bool remote_wait(void *arg){(void)arg;osDelay(1);return true;}
static void remote_worker(void *arg)
{
    (void)arg;if(dr16.channel)SRNOS_DR16Start(&dr16);if(vt03.channel)SRNOS_VT03Start(&vt03);
    const SRNOS_InternalLoop loop={remote_step,remote_wait,NULL,0,false};SRNOS_KernelRunLoop(&loop);
}
int SRNOS_RemoteControlStart(void)
{
    if(started||!configured||osKernelGetState()!=osKernelReady)return -1;started=true;
    SRNOS_VideoLinkFreeze();
    if(!dr16.channel&&!vt03.channel&&!wbus.channel&&!SRNOS_MessageHasUART())return 0;
    const osThreadAttr_t attr={.name="SRNOS_InputIO",.stack_size=3072,.priority=osPriorityHigh7};
    return SRNOS_KernelThreadNew(remote_worker,NULL,&attr)?0:-1;
}
