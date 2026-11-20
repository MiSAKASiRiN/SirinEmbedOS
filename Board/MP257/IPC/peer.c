#include "peer.h"
#if defined(CORE_CM33)
#include "SirinEmbedOS/IO/MP257/MotorExchange.h"
#endif
#include "stm32mp2xx.h"
#include <string.h>
#include <stdbool.h>

#if defined(CORE_CM33)
#define SRNOS_IPC_CORE SRNOS_IPC_M33
#else
#define SRNOS_IPC_CORE SRNOS_IPC_M0
#endif

static volatile SRNOS_IpcPage page __attribute__((section(".srnos_ipc"), aligned(64), used));
static SRNOS_IpcCommand current;
static unsigned last_mark, accepted, expired, rejected;
static bool active;
static unsigned session, continuity;

static void publish(volatile SRNOS_IpcSlot *slot, const SRNOS_IpcData *src, unsigned count)
{
    unsigned next = (slot->mark + 2u) & ~1u;
    if (!next) next = 2;
    slot->mark = next - 1;
    __DMB();
    for (unsigned i = 0; i < count; ++i) slot->data.words[i] = src->words[i];
    __DMB();
    slot->mark = next;
}

void SRNOS_IpcInit(void)
{
    /* remoteproc zeros this ELF LOAD segment on every start. */
    page.core = SRNOS_IPC_CORE;
    page.version = SRNOS_IPC_VERSION;
    page.size = sizeof(page);
    __DMB();
    page.magic = SRNOS_IPC_MAGIC;
}

void SRNOS_IpcPoll(unsigned now)
{
    page.now_ms = now;
    if (active && now - current.issued_ms >= current.request.ttl_ms) {
        active = false;
        ++expired;
        ++continuity;
    }
    unsigned mark = page.down.mark;
    if (!mark || (mark & 1u) || mark == last_mark) return;
    SRNOS_IpcData local;
    __DMB();
    for (unsigned i = 0; i < sizeof(current)/sizeof(unsigned); ++i)
        local.words[i] = page.down.data.words[i];
    __DMB();
    if (mark != page.down.mark) return;
    last_mark = mark;
    const SRNOS_IpcCommand *c = &local.command;
    if (c->request.op == SRNOS_IPC_HELLO && (c->session[0] || c->session[1])) {
        active = false;
        ++session;
        ++continuity;
        page.session[0] = c->session[0];
        __DMB();
        page.session[1] = c->session[1];
        return;
    }
    unsigned op = SRNOS_IPC_CORE == SRNOS_IPC_M33 ? SRNOS_IPC_TARGET : SRNOS_IPC_UART;
    bool angle = SRNOS_IPC_CORE == SRNOS_IPC_M33 &&
                 (c->request.op == SRNOS_IPC_ANGLE_PREVIEW || c->request.op == SRNOS_IPC_ANGLE_DRIVE);
    unsigned max_ttl = angle ? SRNOS_IPC_CONTROL_MAX_TTL_MS : SRNOS_IPC_MAX_TTL_MS;
    unsigned limit = op == SRNOS_IPC_TARGET ? 0xffffu : sizeof(c->request.data);
    if (!(page.session[0] || page.session[1]) || page.session[0] != c->session[0] ||
        page.session[1] != c->session[1] || (!angle && c->request.op != op) ||
        c->request.mask_or_length > limit || !c->request.ttl_ms ||
        c->request.ttl_ms > max_ttl || now - c->issued_ms >= c->request.ttl_ms) {
        ++rejected;
        return;
    }
    current = *c;
    ++accepted;
    active = true;
}

void SRNOS_IpcPublishRadio(const SRNOS_IpcRadio *radio)
{
    SRNOS_IpcData local;
    local.radio = *radio;
    publish(&page.up, &local, sizeof(*radio)/sizeof(unsigned));
}

#if defined(CORE_CM33)
__attribute__((weak)) void SRNOS_MP257MotorStep(const SRNOS_MP257MotorInput *input,
                                              SRNOS_MP257MotorObservation *out)
{
    (void)input;
    (void)out;
}

void SRNOS_IpcPublishMotor(unsigned now)
{
    SRNOS_IpcData local;
    SRNOS_IpcMotor *m = &local.motor;
    *m = (SRNOS_IpcMotor){.cycle_ms=now, .accepted=accepted, .expired=expired};
    if (active) {
        m->active_mask = current.request.mask_or_length;
        memcpy(m->dry_run_mdeg, current.request.data, sizeof(m->dry_run_mdeg));
    }
    SRNOS_MP257MotorInput in = {.now_ms=now, .session=session, .continuity=continuity};
    if (active && (current.request.op == SRNOS_IPC_ANGLE_PREVIEW || current.request.op == SRNOS_IPC_ANGLE_DRIVE)) {
        in.mode = current.request.op == SRNOS_IPC_ANGLE_DRIVE ? SRNOS_MP257_MOTOR_DRIVE : SRNOS_MP257_MOTOR_PREVIEW;
        in.valid_mask = current.request.mask_or_length;
        memcpy(in.target_mdeg, current.request.data, sizeof(in.target_mdeg));
    }
    SRNOS_MP257MotorObservation out = {0};
    SRNOS_MP257MotorStep(&in, &out);
    m->measured_count = out.measured_count;
    m->profile = out.profile;
    m->state = out.state;
    m->fault = out.fault;
    m->feedback_age_ms = out.feedback_age_ms;
    m->measured_mdeg = (unsigned)out.measured_mdeg;
    m->rpm = (unsigned)out.rpm;
    m->reference_mdeg = (unsigned)out.reference_mdeg;
    m->limited_offset_mdeg = (unsigned)out.limited_offset_mdeg;
    m->output_raw = (unsigned)out.output_raw;
    m->trip_sources = out.speed_trip.sources;
    m->trip_cycle_ms = out.speed_trip.cycle_ms;
    m->trip_rpm = (unsigned)out.speed_trip.rpm;
    m->trip_speed_mdeg_s = (unsigned)out.speed_trip.speed_mdeg_s;
    m->trip_offset_mdeg = (unsigned)out.speed_trip.offset_mdeg;
    m->trip_output_raw = (unsigned)out.speed_trip.output_raw;
    publish(&page.up, &local, sizeof(*m)/sizeof(unsigned));
}
#endif

void SRNOS_IpcPublishStatus(SRNOS_IpcStatus *s)
{
    SRNOS_IpcData local;
    s->accepted = accepted;
    s->expired = expired;
    s->rejected = rejected;
    s->active = active;
    if (SRNOS_IPC_CORE == SRNOS_IPC_M0 && active) {
        s->uart_dry_run_len = current.request.mask_or_length;
        memcpy(s->uart_dry_run_data, current.request.data, sizeof(s->uart_dry_run_data));
    }
    local.status = *s;
    publish(&page.status, &local, sizeof(*s)/sizeof(unsigned));
}
