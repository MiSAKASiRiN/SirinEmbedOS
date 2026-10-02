# 内核与托管任务

Kernel 在现有 FreeRTOS 10.6.2 / CMSIS-RTOS2 上管理线程生命周期、外层循环、等待和内部统计；第三方内核不修改。父项目包含 `SirinEmbedOS/Kernel/Thread.h`、`Sync.h`，通常不再调用原生 RTOS API。接口只公开 SirinOS 值句柄和类型，不借出 TCB、原生句柄或统计 hook。

## 父项目从哪里开始

应用通常把回调及管理函数放在 `App/Tasks/`，在 `App/app_main.cpp` 实现 `SRNOS_ApplicationMain()`，在 `Config/` 的配置回调声明启动期选择。父项目持有业务参数、通信协议和控制算法，SDK 持有调度器和固定服务；不要再次启动内核，不把长期循环写进 ApplicationMain。

运行期创建适合需要可管理句柄的业务线程：先创建同步对象/准备参数，再 Create，保存句柄，最后才启用生产者或通知来源。配置列表适合随启动创建的固定任务；两条路径共用同一个线程限额和对象池。两种方式均要求名称唯一，不能把同一描述既放入列表又手动 Create。

以下 C 示例是独立片段；一个最终固件只实现一份 ApplicationConfigure/ApplicationMain。C++ 可使用 `SRNOS_ThreadConfig c{};` 后逐字段赋值；不依赖 C++20 指定初始化器。

## 运行期创建和执行

`SRNOS_ThreadConfig` 的 `init/step/cleanup` 直接替代旧 `entry`，无旧接口兼容层。名称和描述在创建时复制；参数内存由调用者持有，直到 Delete 成功。名称 1..15 字节，`SRNOS_` 保留给 SDK；父项目优先级 1..39，栈 1024..32768 B、8 字节对齐，包含 Picolibc TLS。

```c
#include <SirinEmbedOS/Kernel/Thread.h>
static SRNOS_Thread control;
static SRNOS_StepResult control_step(void *user, const SRNOS_ThreadWake *wake) {
    (void)user; (void)wake;
    // 读取输入、计算并提交本轮结果；不写永久循环或周期延时。
    return SRNOS_STEP_CONTINUE;
}
SRNOS_Result start_control(void) { // 仅由同一个管理者串行调用
    if (control) return SRNOS_BUSY; // 防止覆盖仍需回收的句柄
    const SRNOS_ThreadConfig c = {
        .name="Control", .step=control_step, .stack_bytes=4096,
        .priority=24, .mode=SRNOS_TASK_PERIODIC, .period_ms=1
    };
    // 调用者检查结果；SRNOS_RESOURCE 表示重名/数量/容量不足。
    return SRNOS_ThreadCreate(&c,&control);
}
```

Create 成功仅表示对象创建成功；回调可能在 Create 返回前抢占执行。通过 GetInfo 的 `init_result` 和状态查询初始化结果。`init_result` 初始为 SRNOS_BUSY，开始运行前可能尚未执行或仍在 init 内；应结合 state 判断，因为 init 本身也可以返回非 OK（包括 BUSY）导致失败。init 返回非 OK 则不执行 step。cleanup 在自身任务上下文恰好调用一次，包括 init 失败和 init 前已请求停止，因此须容忍部分初始化。回调返回失败使用 `SRNOS_STEP_FAILED`，不支持跨 C 边界抛出 C++ 异常。最终 FAILED 且 init_result 非 OK 表示初始化失败；FAILED 且 init_result 为 OK 表示后续 step 失败；EXITED 表示正常完成或已协作停止并完成清理。

| 模式 | 执行规则 |
|---|---|
| PERIODIC | 初始化成功后立即首轮；固定相位、绝对毫秒释放点。晚唤醒仅处理一次；结束后选严格未来的释放点，跳过错过周期，不追赶补跑。 |
| EVENT | 等通知位或正数超时后执行一轮；wake 提供 FLAGS/TIMEOUT 和位掩码。通知位会合并，不是计数队列；掩码非零且限 0..30 位，等待超时必须为正数或 WAIT_FOREVER。 |
| ONCE | 执行一次 step 后清理退出，即使返回 CONTINUE。 |

启动配置 `threads[]` 使用同一描述并自动创建，在参数加载尝试/USB 启动完成后放行。`SRNOS_ApplicationMain()` 仍是允许返回的应用启动回调，自己的任意循环不会被伪装成可识别的周期。应用就绪后允许运行期 Create/Delete；配置回调内禁止创建、阻塞和 I/O，硬件/服务选择仍冻结。

## 启动配置中的固定任务

```c
#include <SirinEmbedOS/Kernel/Startup.h>

static SRNOS_StepResult status_step(void *argument, const SRNOS_ThreadWake *wake)
{
    (void)argument; (void)wake;
    // 读取快照并处理一轮；这里无需再次创建线程或编写周期循环。
    return SRNOS_STEP_CONTINUE;
}

void SRNOS_ApplicationConfigure(SRNOS_SystemConfig *config)
{
    // 示例独占应用列表；已有其他模块登记时，应协调分配索引并检查容量。
    config->threads[0] = (SRNOS_ThreadConfig){
        .name = "AppStatus", .step = status_step,
        .stack_bytes = 2048, .priority = 16,
        .mode = SRNOS_TASK_PERIODIC, .period_ms = 1000
    };
    config->thread_count = 1;
}

void SRNOS_ApplicationMain(void)
{
    // SDK 已负责创建 AppStatus；本入口可以返回。
}
```

`threads[]` 固定最多 `SRNOS_SYSTEM_MAX_THREADS`（当前 8）条，另受父项目总线程限额/池预算限制。SDK 在配置回调返回后才实际创建，所以其中的名称和 argument 不能指向配置函数的局部数组/局部对象；名称使用字符串常量，参数通常使用静态对象。配置校验/启动创建失败会终止启动路径；需要可恢复创建失败处理的线程应选择运行期 Create。

启动列表当前没有“按名称查找句柄”或返回句柄数组的公共接口。需要管理句柄时，优先使用运行期 Create；必须使用启动列表时，可在该线程的 init 中调用 `SRNOS_ThreadGetCurrent()`，通过父项目已就绪的队列/锁等同步方式交给管理者，并设计好停止前的交接流程。不要把 64 位句柄写入共享变量后仅靠 volatile 跨线程读取。

列表线程和 ApplicationMain 在同一应用就绪阶段放行，先后顺序由调度决定。不能假设 ApplicationMain 一定先创建队列/锁；存在依赖时，在 ApplicationMain 中先创建资源再 Create，或在应用内安排明确的就绪同步。USB 启动/参数加载屏障也不代表所有传感器已经健康。

## 停止、删除和等待

RequestStop 设置停止位并立即唤醒框架周期等待、WaitFlags 和 Delay。正在运行的 init/step 自然返回后才执行 cleanup；有限同步 I/O 可留在 step 内。同步对象和设备内部阻塞不会被强制取消，长工作可调用 ShouldStop，阻塞应使用有限超时。

```c
SRNOS_Result stop_control(void) {
    SRNOS_Result r=SRNOS_ThreadRequestStop(control);
    if(r!=SRNOS_OK) return r;
    r=SRNOS_ThreadJoin(control,100); // 清理已完成，仍可 GetInfo
    if(r!=SRNOS_OK) return r;       // 超时：保留句柄、参数与资源
    r=SRNOS_ThreadDelete(control,SRNOS_NO_WAIT);
    if(r==SRNOS_OK) control=0;      // 仅成功回收后清空；BUSY 时保留并重试
    return r;
}
```

Delete 自带停止和等待；只在 cleanup 完成且目标停在框架退出点后删除 RTOS 对象、回收池块。退出的父项目任务仍占槽/栈，直到显式 Delete。并发 Join/操作尚未释放引用时 Delete 可返回 BUSY，应稍后重试。旧句柄因生命周期编号失效；重复 Delete 返回 STALE_HANDLE。禁止同步 Join/Delete 自己；结束自身用 step 返回 STOP。SDK 服务按内部归属保护，不能被父项目删除，Idle/Timer 保留内核入口。

管理者应串行执行自己持有句柄的创建/删除，避免覆盖旧句柄；多个任务共享句柄或 argument 时仍需应用同步。停用生产者/IRQ 通知来源，停止消费者并等待 Delete 成功，再释放消费者专属队列、锁和参数。cleanup 负责归还持有的锁及业务资源，不负责删除自身 RTOS 对象；Join 成功只证明已清理退出，不代表业务执行成功，仍可 GetInfo 检查 FAILED。GetInfo 中优先级是创建时配置值；实际运行统计通过 SystemMonitor 获取。

## 延时与时钟

`SRNOS_Delay(ms)` 的 0 是 Yield；有限时间为 0..INT32_MAX，`SRNOS_WAIT_FOREVER` 可被停止唤醒。`SRNOS_DelayUntil(deadline_ms)` 使用 `SRNOS_TimeMs()` 的 32 位绝对调度时钟，截止点等于/早于当前返回 DEADLINE_PASSED。调用者须保证截止点处于半个时钟范围内；支持 32 位回绕，不将微秒观察时钟混用。TimeUs 是 DWT 64 位观察时钟，TimeMs 是 1 ms tick；二者起点不同。创建/删除/阻塞和普通同步接口限任务上下文。

```c
// 有限多阶段步骤中的等待；周期任务外层等待仍由 SDK 执行。
static SRNOS_Result wait_for_settle(void)
{
    const uint32_t deadline = SRNOS_TimeMs() + 20u;
    SRNOS_Result r = SRNOS_DelayUntil(deadline);
    if (r == SRNOS_STOP_REQUESTED) return r;
    // 若处理/抢占期间截止点已过，直接进入下一阶段，不补等 20 ms。
    return r == SRNOS_DEADLINE_PASSED ? SRNOS_OK : r;
}
```

`SRNOS_NO_WAIT` 为 0，`SRNOS_WAIT_FOREVER` 为无限等待；有限超时最大 INT32_MAX 毫秒。绝对 deadline 不是超时值，不把 WAIT_FOREVER 传给 DelayUntil。Delay/WaitFlags 的停止唤醒只适用于父项目托管线程；ApplicationMain 是 SDK 所属启动线程，其 Delay 不存在父项目协作删除语义。

## 通知和同步

```c
#include <SirinEmbedOS/Kernel/Sync.h>
static SRNOS_Thread receiver;
static SRNOS_Queue queue;
static SRNOS_Mutex lock;
static SRNOS_StepResult receive_step(void *p,const SRNOS_ThreadWake *wake) {
    (void)p;
    (void)wake; // 通知或超时均检查队列，超时也可继续处理剩余积压。
    {
        uint32_t value;
        for(unsigned n=0;n<4;n++) { // 每轮限制工作量
            if(SRNOS_ThreadShouldStop()) return SRNOS_STEP_STOP;
            if(SRNOS_MutexLock(lock,10)!=SRNOS_OK) break; // 消息留在队列中
            SRNOS_Result r=SRNOS_QueueReceive(queue,&value,SRNOS_NO_WAIT);
            if(r==SRNOS_OK) {
                // 用 value 更新业务值；临界业务应短小。
            }
            (void)SRNOS_MutexUnlock(lock); // 所有已加锁路径都释放
            if(r!=SRNOS_OK) break;
        }
    }
    return SRNOS_STEP_CONTINUE;
}
SRNOS_Result start_receiver(void) {
    if(receiver) return SRNOS_BUSY; // 同一管理者串行管理生命周期
    SRNOS_Result r=SRNOS_MutexCreate(false,&lock); // 默认具备优先级继承
    if(r!=SRNOS_OK) return r;
    r=SRNOS_QueueCreate(4,sizeof(uint32_t),&queue);
    if(r!=SRNOS_OK) { SRNOS_MutexDelete(lock); return r; }
    const SRNOS_ThreadConfig c={.name="Receiver",.step=receive_step,
        .stack_bytes=2048,.priority=24,.mode=SRNOS_TASK_EVENT,
        .event_mask=1,.event_timeout_ms=100};
    r=SRNOS_ThreadCreate(&c,&receiver);
    if(r!=SRNOS_OK) { SRNOS_QueueDelete(queue); SRNOS_MutexDelete(lock); }
    return r;
}
SRNOS_Result send_value(uint32_t value) {
    SRNOS_Result r=SRNOS_QueueSend(queue,&value,10);
    return r==SRNOS_OK?SRNOS_ThreadNotify(receiver,1):r;
}
```

生产者停用、receiver 删除成功后，才能删除该示例的 queue/lock。ThreadWaitFlags 消费匹配的 0..30 位；框架控制通知使用独立 index，与现有驱动的 index 0 隔离，停止不会清业务位。计数信号量设置 maximum/initial；固定大小队列复制消息并保持 FIFO；事件标志支持 0..23 位、any/all、可选消费。所有同步对象在持有或操作进行中（包括等待者）删除均返回 BUSY，不替使用者强制解锁或取消等待。没有消息大小自适应、优先级消息或跨生命周期引用。

仅 NotifyFromISR、SemaphoreGiveFromISR、QueueSendFromISR/ReceiveFromISR 可在 ISR 使用，队列操作不阻塞；IRQ 数值优先级须为 5..15。互斥锁、事件标志、创建和删除均禁止 ISR。通知保留位/非法超时返回 INVALID；上下文不合法返回 CONTEXT；超时/空队列返回 TIMEOUT，满信号量返回 RESOURCE。

## 有界资源与预算

启动时一次预留默认 32 KiB 父项目对象池，运行期只从固定 256 B 块分配栈、静态 TCB、完成事件和同步对象存储，通过 RTOS 静态创建装配。父项目线程上限 8，mutex/semaphore/queue/event 各 8；`SRNOS_APP_OBJECT_POOL_BYTES`、`SRNOS_APP_THREAD_LIMIT` 和各 `SRNOS_APP_*_LIMIT` 可编译期覆盖。数量上限不保证全部最大对象能同时放下，连续块不足也会返回 RESOURCE。池扫描和清零不覆盖全局关中断。

KernelGetResources 报告池容量、当前/峰值、池分配失败及对象创建资源失败次数、父项目线程数以及 heap_5 空闲/最低空闲；池中空闲并不等于可满足任意连续分配。SDK 静态登记表和可选循环统计累加器在调度前各分配一次，保守预算各 8 KiB；32 KiB 父项目池另计。SystemHeapBudget 包含这些预留和原有服务栈/队列/TCB/TLS，不代表运行期实测余量。其他业务分配仍应单独预留。

## 旧业务线程迁移

| 原业务写法 | 当前公开接口/做法 |
|---|---|
| `entry(void *)` 内永久循环 | 一次性准备移入 init，一轮工作移入 step，资源释放移入 cleanup；step 必须返回。 |
| `xTaskCreate` / `osThreadNew` | 通过 ThreadCreate 或启动配置列表建立任务，不暴露原生句柄/TCB。 |
| 循环尾 `vTaskDelayUntil` / `osDelay` | 固定周期任务声明 PERIODIC/period_ms 并删除外层延时；有限阶段等待用 Delay/DelayUntil。 |
| `vTaskDelete(NULL)` / 自身 Terminate | 返回 STEP_STOP 或 STEP_FAILED；由持有句柄的管理者最终 Delete。 |
| 外部强制终止 | RequestStop + Join + Delete，或有超时的 Delete；超时保留参数/对象。 |
| 原生任务通知 | 事件任务的 ThreadNotify/NotifyFromISR；需要每条数据都保留时用队列/计数信号量，不能把通知当计数。 |
| 原生 semaphore/queue/event API | 使用 Sync.h 的互斥锁、计数信号量、固定大小 FIFO 队列和事件标志。 |

初始化资源前不能使能依赖它们的生产者；回调可在 Create 返回前抢占执行，因此回调内需要自身句柄时用 GetCurrent，不依赖调用者稍后设置的共享变量。业务参数由调用者保留到 Delete 成功；名称/描述复制并不复制 argument 指向的业务数据。

## SDK 迁移与统计

所有 SDK 创建均走私有 KernelThreadNew；服务初始化后由 KernelRunLoop 统一调用 step 和私有 wait。周期传感器/INS 保留原超期重置，Indicators/RC/Referee 保留相对延时；CAN/SPI/I2C/Serial/Log/HostComm 保留通知、队列、超时、批处理；BMI DMA 等处理内等待仍在本轮耗时内。普通 wait 在统计边界外，init/cleanup 不计入稳定周期。SDK 优先级、栈和常驻线程数保留；名称统一 SRNOS_（15 字节以内），Idle/Timer 仅配置为 SRNOS_Idle/SRNOS_Timer，不包装上游内核循环。

`cycle_statistics_enabled` 默认启用，启动冻结，关闭不改变等待策略或 Fault。hook 只记 DWT 整数计数，SystemMonitor 每批最多四槽、批间阻塞一个 tick 后换算/发布。墙钟执行时间包含抢占、同步阻塞和中断，与 CPU 占比分开。事件/相对等待无固定周期时不发布虚构抖动；详见 Diagnostics/Runtime 和 SystemMonitor。基础流 0x0001、ADC 0x0002 不变，新增 0x0003 循环统计。

`Tests/Kernel/run_host_tests.sh` 使用 pthread 假内核运行实际封装，覆盖生命周期、同步、回绕与统计；父项目接入和固件链接由 Tests/Build 检查。这些主机/构建结果不代替板测。已完成本板 Release 负载下的 hook/CPU/临界区、生命周期、INS 与 CDC 短测；外部 CAN 负载、长时间压力和 Debug hook 时序仍待验收，详见 [板端记录](../../Docs/Validation/ManagedTasksBoard-20260928/README.md)。

## 2026-09-28 板端验证

新增 Tests/Board/ManagedTasks 独立父项目板测，覆盖 400 次协作创建/删除、TLS、清理次序、同步删除保护和资源耗尽恢复。统计开关对照、完整 hook 基准和限制见板端验证记录。

公共隔离后的新固件已复测四个完整窗口，每次 400 个生命周期、2846 项检查零失败。两组统计 CPU 差值约 0.99 个百分点，完整 hook 最大 0.68 µs；具体负载和未验收边界见 [当前记录](../../Docs/Validation/SRNOSBoard-20260928/README.md)。

## 公共构建边界

Kernel 的 RTOS/CMSIS、Board 与 Source include 都是 SDK 私有编译依赖。生产应用只链接 Platform 并包含 SDK 公共头；需要原生 RTOS 的验证代码单独编译为测试适配目标。接口独立 C11/C++17、依赖闭包和父项目真实编译命令由 Tests/Build 检查。

## 持久化启动屏障

SystemConfig包含`storage.enabled`（默认true）、`storage.factory_identity_write`（默认false）、`parameter_application_id`与兼容版本（默认1）。AppInit先解析身份和逐IMU校准，再加载业务参数，共享2秒期限；超时或损坏使用规定的默认/降级状态后放行应用。INS等待校准期间继续温控，存储没有新增worker。注册了持久参数的父项目必须声明非零应用ID；HostLink禁用时本地加载/保存仍可用。整片EEPROM维护示例显式设置storage.enabled=false；普通业务不要使用维护模式绕过分区。
