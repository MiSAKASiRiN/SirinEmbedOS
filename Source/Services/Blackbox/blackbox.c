#include "blackbox_internal.h"
#include "blackbox_ring.h"
#include "Devices/NOR/nor_internal.h"
#include "Kernel/thread_internal.h"
#include "queue.h"
#include "stm32h7xx.h"
#include <stddef.h>
#include <string.h>

typedef enum {
    SRNOS_BLACKBOX_READ_IDLE, SRNOS_BLACKBOX_READ_QUEUED,
    SRNOS_BLACKBOX_READ_RUNNING, SRNOS_BLACKBOX_READ_DONE
} SRNOS_BlackboxReadState;
typedef struct {
    SRNOS_BlackboxRing ring;
    QueueHandle_t queue;
    StaticQueue_t queue_control;
    SRNOS_BlackboxRecord work_record, read_record;
    SRNOS_BlackboxReadState read_state;
    SRNOS_BlackboxStatus read_status;
    uint32_t ticket, last_ticket;
    uint32_t scan_operations;
    uint64_t after_sequence;
    uint8_t queue_storage[];
} SRNOS_BlackboxService;
_Static_assert(sizeof(SRNOS_BlackboxService)<=SRNOS_BLACKBOX_STATE_BUDGET,
    "Blackbox state exceeds its startup heap reservation");

static SRNOS_BlackboxConfig configuration;
static SRNOS_BlackboxSnapshot published;
static SRNOS_BlackboxService *service;
static TaskHandle_t worker;
static bool prepared;

static bool task_context(void)
{
    return SRNOS_KernelTaskContext() && !__get_PRIMASK() && !__get_BASEPRI();
}

static SRNOS_BlackboxStatus availability(void)
{
    if (!prepared || !configuration.enabled) return SRNOS_BLACKBOX_DISABLED;
    if (published.state==SRNOS_BLACKBOX_STATE_FAULT) return published.last_status;
    return published.state==SRNOS_BLACKBOX_STATE_READY ? SRNOS_BLACKBOX_OK : SRNOS_BLACKBOX_NOT_READY;
}
static bool read_flash(void *context,uint32_t address,void *data,size_t length)
{ (void)context; return SRNOS_NORRead(address,data,length)==SRNOS_NOR_OK; }
static bool program_flash(void *context,uint32_t address,const void *data,size_t length)
{ (void)context; return SRNOS_NORProgram(address,data,length)==SRNOS_NOR_OK; }
static bool erase_flash(void *context,uint32_t address)
{ (void)context; return SRNOS_NOREraseSector(address)==SRNOS_NOR_OK; }
static void yield_scan(void *context)
{
    (void)context;
    /* A full first-use erased-media scan is 49152 pages. Yield in bounded
     * batches, not 1 ms per page; program/erase WIP waits yield separately. */
    if ((++service->scan_operations % 32u)==0u) osDelay(1u);
}

static void publish_ring(void)
{
    SRNOS_BlackboxRingSnapshot snapshot;
    SRNOS_BlackboxRingGetSnapshot(&service->ring,&snapshot);
    taskENTER_CRITICAL();
    published.capacity_records=snapshot.capacity_records;
    published.oldest_sequence=snapshot.oldest_sequence;
    published.newest_sequence=snapshot.newest_sequence;
    taskEXIT_CRITICAL();
}
static void fail_service(SRNOS_BlackboxStatus status,bool failed_record)
{
    taskENTER_CRITICAL();
    published.state=SRNOS_BLACKBOX_STATE_FAULT;
    published.last_status=status;
    published.discarded_fault+=(uint32_t)uxQueueMessagesWaiting(service->queue)+(failed_record?1u:0u);
    (void)xQueueReset(service->queue);
    if (service->read_state==SRNOS_BLACKBOX_READ_QUEUED ||
        service->read_state==SRNOS_BLACKBOX_READ_RUNNING) {
        service->read_status=status;
        service->read_state=SRNOS_BLACKBOX_READ_DONE;
    }
    taskEXIT_CRITICAL();
}
static void initialize(void)
{
    uint32_t id=0;
    const SRNOS_NORStatus device=SRNOS_NORInitialize(&id);
    taskENTER_CRITICAL(); published.jedec_id=id; taskEXIT_CRITICAL();
    SRNOS_BlackboxStatus result=device==SRNOS_NOR_UNSUPPORTED ? SRNOS_BLACKBOX_INCOMPATIBLE : SRNOS_BLACKBOX_IO_ERROR;
    if (device==SRNOS_NOR_OK) {
        const SRNOS_BlackboxRingIO io={NULL,read_flash,program_flash,erase_flash,yield_scan};
        const SRNOS_BlackboxRingConfig config={SRNOS_BLACKBOX_PARTITION_BASE,SRNOS_BLACKBOX_PARTITION_BYTES};
        result=SRNOS_BlackboxRingMount(&service->ring,&io,&config);
    }
    if (result!=SRNOS_BLACKBOX_OK) { fail_service(result,false); return; }
    publish_ring();
    taskENTER_CRITICAL();
    published.last_status=SRNOS_BLACKBOX_OK;
    published.state=SRNOS_BLACKBOX_STATE_READY;
    taskEXIT_CRITICAL();
}
static void step(void *context)
{
    (void)context;
    if (published.state!=SRNOS_BLACKBOX_STATE_READY) return;
    if (xQueueReceive(service->queue,&service->work_record,0)==pdPASS) {
        SRNOS_BlackboxRecord *record=&service->work_record;
        uint64_t sequence=0;
        SRNOS_BlackboxStatus result=SRNOS_BlackboxRingAppend(&service->ring,
            record->type,record->timestamp_us,record->data,record->length,&sequence);
        taskENTER_CRITICAL();
        if (result==SRNOS_BLACKBOX_OK) ++published.committed;
        else ++published.write_errors;
        taskEXIT_CRITICAL();
        if (result!=SRNOS_BLACKBOX_OK) { fail_service(result,true); return; }
        publish_ring();
    }

    /* One read after at most one write. Neither caller pointers nor callbacks
     * are retained. Ticket checking makes cancellation safe during NOR waits. */
    uint32_t ticket=0;
    uint64_t cursor=0;
    taskENTER_CRITICAL();
    if (service->read_state==SRNOS_BLACKBOX_READ_QUEUED) {
        ticket=service->ticket;
        cursor=service->after_sequence;
        service->read_state=SRNOS_BLACKBOX_READ_RUNNING;
    }
    taskEXIT_CRITICAL();
    if (!ticket) return;
    SRNOS_BlackboxStatus result=SRNOS_BlackboxRingReadAfter(&service->ring,cursor,&service->work_record);
    taskENTER_CRITICAL();
    if (result==SRNOS_BLACKBOX_OK || result==SRNOS_BLACKBOX_EMPTY) ++published.read_completed;
    else ++published.read_errors;
    if (service->ticket==ticket && service->read_state==SRNOS_BLACKBOX_READ_RUNNING) {
        if (result==SRNOS_BLACKBOX_OK) service->read_record=service->work_record;
        service->read_status=result;
        service->read_state=SRNOS_BLACKBOX_READ_DONE;
    }
    taskEXIT_CRITICAL();
    if (result!=SRNOS_BLACKBOX_OK && result!=SRNOS_BLACKBOX_EMPTY) fail_service(result,false);
}
static bool wait_next(void *context)
{
    (void)context;
    taskENTER_CRITICAL();
    bool work=published.state==SRNOS_BLACKBOX_STATE_READY &&
        (uxQueueMessagesWaiting(service->queue) || service->read_state==SRNOS_BLACKBOX_READ_QUEUED);
    taskEXIT_CRITICAL();
    if (!work) (void)ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(20u));
    return true;
}
static void entry(void *context)
{
    (void)context;
    initialize();
    const SRNOS_InternalLoop loop={step,wait_next,NULL,0,false};
    SRNOS_KernelRunLoop(&loop);
}

bool SRNOS_BlackboxPrepare(const SRNOS_BlackboxConfig *config)
{
    if (prepared || !SRNOS_BlackboxConfigValid(config)) return false;
    configuration=*config;
    published.state=config->enabled ? SRNOS_BLACKBOX_STATE_STARTING : SRNOS_BLACKBOX_STATE_DISABLED;
    published.last_status=config->enabled ? SRNOS_BLACKBOX_NOT_READY : SRNOS_BLACKBOX_DISABLED;
    published.queue_capacity=config->enabled ? config->queue_depth : 0u;
    prepared=true;
    return true;
}
bool SRNOS_BlackboxStart(void)
{
    if (!prepared) return false;
    if (!configuration.enabled) return true;
    if (service || worker) return false;
    size_t bytes=sizeof(*service)+configuration.queue_depth*sizeof(SRNOS_BlackboxRecord);
    service=pvPortMalloc(bytes);
    if (!service) return false;
    memset(service,0,bytes);
    service->queue=xQueueCreateStatic(configuration.queue_depth,sizeof(SRNOS_BlackboxRecord),
        service->queue_storage,&service->queue_control);
    if (!service->queue) { vPortFree(service); service=NULL; return false; }
    const osThreadAttr_t attributes={.name="SRNOS_Blackbox",.stack_size=SRNOS_BLACKBOX_STACK_BYTES,.priority=osPriorityLow};
    worker=(TaskHandle_t)SRNOS_KernelThreadNew(entry,NULL,&attributes);
    if (!worker) { vQueueDelete(service->queue); vPortFree(service); service=NULL; return false; }
    return true;
}
SRNOS_BlackboxStatus SRNOS_BlackboxAppend(uint16_t type,uint64_t timestamp_us,const void *data,size_t length)
{
    if (!task_context()) return SRNOS_BLACKBOX_CONTEXT;
    if (!type || !data || !length || length>SRNOS_BLACKBOX_PAYLOAD_MAX) return SRNOS_BLACKBOX_INVALID;
    SRNOS_BlackboxRecord record={.timestamp_us=timestamp_us,.type=type,.length=(uint16_t)length};
    memcpy(record.data,data,length);
    taskENTER_CRITICAL();
    SRNOS_BlackboxStatus result=availability();
    if (result!=SRNOS_BLACKBOX_OK) ++published.rejected_not_ready;
    else if (xQueueSend(service->queue,&record,0)!=pdPASS) {
        ++published.dropped_full;
        result=SRNOS_BLACKBOX_FULL;
    } else {
        ++published.accepted;
        uint16_t count=(uint16_t)uxQueueMessagesWaiting(service->queue);
        if (count>published.queue_high_water) published.queue_high_water=count;
    }
    taskEXIT_CRITICAL();
    if (result==SRNOS_BLACKBOX_OK) xTaskNotifyGive(worker);
    return result;
}
SRNOS_BlackboxStatus SRNOS_BlackboxRequestRead(uint64_t after_sequence,uint32_t *ticket)
{
    if (!task_context()) return SRNOS_BLACKBOX_CONTEXT;
    if (!ticket) return SRNOS_BLACKBOX_INVALID;
    *ticket=0;
    taskENTER_CRITICAL();
    SRNOS_BlackboxStatus result=availability();
    if (result==SRNOS_BLACKBOX_OK) {
        if (service->read_state!=SRNOS_BLACKBOX_READ_IDLE) result=SRNOS_BLACKBOX_BUSY;
        else if (service->last_ticket==UINT32_MAX) result=SRNOS_BLACKBOX_FULL;
        else {
            service->ticket=++service->last_ticket;
            service->after_sequence=after_sequence;
            service->read_state=SRNOS_BLACKBOX_READ_QUEUED;
            *ticket=service->ticket;
        }
    }
    taskEXIT_CRITICAL();
    if (result==SRNOS_BLACKBOX_OK) xTaskNotifyGive(worker);
    return result;
}
SRNOS_BlackboxStatus SRNOS_BlackboxPollRead(uint32_t ticket,SRNOS_BlackboxRecord *record)
{
    if (!task_context()) return SRNOS_BLACKBOX_CONTEXT;
    if (!ticket || !record) return SRNOS_BLACKBOX_INVALID;
    taskENTER_CRITICAL();
    SRNOS_BlackboxStatus result=SRNOS_BLACKBOX_INVALID;
    if (service && service->ticket==ticket && service->read_state!=SRNOS_BLACKBOX_READ_IDLE) {
        result=SRNOS_BLACKBOX_BUSY;
        if (service->read_state==SRNOS_BLACKBOX_READ_DONE) {
            result=service->read_status;
            if (result==SRNOS_BLACKBOX_OK) *record=service->read_record;
            service->read_state=SRNOS_BLACKBOX_READ_IDLE;
        }
    }
    taskEXIT_CRITICAL();
    return result;
}
SRNOS_BlackboxStatus SRNOS_BlackboxCancelRead(uint32_t ticket)
{
    if (!task_context()) return SRNOS_BLACKBOX_CONTEXT;
    if (!ticket) return SRNOS_BLACKBOX_INVALID;
    taskENTER_CRITICAL();
    SRNOS_BlackboxStatus result=SRNOS_BLACKBOX_INVALID;
    if (service && service->ticket==ticket && service->read_state!=SRNOS_BLACKBOX_READ_IDLE) {
        service->read_state=SRNOS_BLACKBOX_READ_IDLE;
        result=SRNOS_BLACKBOX_OK;
    }
    taskEXIT_CRITICAL();
    return result;
}
bool SRNOS_BlackboxGetSnapshot(SRNOS_BlackboxSnapshot *out)
{
    if (!out || !task_context()) return false;
    taskENTER_CRITICAL();
    *out=published;
    out->queued=service ? (uint16_t)uxQueueMessagesWaiting(service->queue) : 0u;
    taskEXIT_CRITICAL();
    return true;
}
