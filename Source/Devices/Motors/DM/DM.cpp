#include "SirinEmbedOS/Devices/Motors/DM/DM.hpp"
#include "SirinEmbedOS/Core/Endian.hpp"
#include "../state_guard.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace SRNOS::Devices {
namespace {
constexpr uint8_t enable_code = 0xFC;
constexpr uint8_t disable_code = 0xFD;

bool positive(float value) {
    return std::isfinite(value) && value > 0;
}

uint16_t quantize(float fraction, uint16_t maximum) {
    return static_cast<uint16_t>(std::clamp(fraction, 0.f, 1.f) * maximum + .5f);
}

uint16_t encodeSigned(float value, float limit, uint16_t maximum) {
    return quantize((std::clamp(value, -limit, limit) / limit + 1.f) * .5f, maximum);
}

float decodeSigned(uint16_t value, uint16_t maximum, float limit) {
    return (static_cast<float>(2 * value - maximum) / maximum) * limit;
}

void writeFloat(SRNOS_CANFrame& frame, size_t offset, float value) {
    uint32_t bits;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    (void)Endian::storeLE32(frame.data, offset, bits);
}
} // namespace

struct Detail::DMCanAdapter {
    static size_t filterCount(void*) { return 1; }
    static bool filter(void* obj, size_t index, SRNOS_CANRxFilter* out) {
        if (index != 0) return false;
        *out = {static_cast<DMMotor*>(obj)->config_.feedback_sid, 0};
        return true;
    }
    static void receive(void* obj, const SRNOS_CANFrame* frame, uint32_t now_ms) {
        static_cast<DMMotor*>(obj)->receive(*frame, now_ms);
    }
    static size_t poll(void* obj, SRNOS_CANTxKind kind, SRNOS_CANTxItem* out, size_t capacity, uint32_t) {
        if (!capacity) return 0;
        const auto& motor = *static_cast<DMMotor*>(obj);
        StateGuard guard;
        if (kind == SRNOS_CAN_TX_KIND_PERIODIC && motor.command_.dlc) {
            *out = {};
            out->frame = motor.command_;
        } else if (kind == SRNOS_CAN_TX_KIND_URGENT && motor.pending_) {
            *out = {};
            out->frame.id = motor.command_sid_;
            out->frame.dlc = 8;
            std::memset(out->frame.data, 0xFF, 7);
            out->frame.data[7] = motor.pending_;
            out->token = motor.pending_;
        } else {
            return 0;
        }
        out->format = SRNOS_CAN_TX_FORMAT_RAW_FRAME;
        return 1;
    }
    static void commit(void* obj, SRNOS_CANTxKind kind, const SRNOS_CANTxItem* item, SRNOS_CANTxResult result) {
        if (kind != SRNOS_CAN_TX_KIND_URGENT || result != SRNOS_CAN_TX_RESULT_ACCEPTED) return;
        auto& motor = *static_cast<DMMotor*>(obj);
        StateGuard guard;
        if (motor.pending_ == item->token) motor.pending_ = 0;
    }
    static void tick(void* obj, uint32_t now_ms) {
        auto& motor = *static_cast<DMMotor*>(obj);
        StateGuard guard;
        if (feedbackExpired(now_ms, motor.feedback_.last_rx_ms, motor.config_.offline_ms))
            motor.feedback_.online = false;
    }
};

int DMMotor::registerCan(SRNOS_CANChannel channel, const DMConfig& config, uint32_t period_ms) {
    using Adapter = Detail::DMCanAdapter;
    static const SRNOS_CANAdapterOps ops{Adapter::filterCount, Adapter::filter, Adapter::receive,
                                         Adapter::poll, Adapter::commit, Adapter::tick};
    if (channel_) return -2;
    if (!channel || !period_ms || config.model > DMModel::DM8009P || config.mode > DMMode::ForcePosition)
        return -1;
    const auto& lim = config.limits;
    const uint32_t sid = config.can_id + 0x100u * static_cast<unsigned>(config.mode);
    if (sid > 0x7ff || config.feedback_sid > 0x7ff || !positive(lim.pmax_rad) ||
        !positive(lim.vmax_rad_s) || !positive(lim.tmax_nm) || !config.offline_ms ||
        config.offline_ms > INT32_MAX || !std::isfinite(config.feedback_period_rad) ||
        config.feedback_period_rad < 0 || config.feedback_period_rad * .5f > lim.pmax_rad) return -1;

    // Registration is serialized during startup. The router queries filters inside attach.
    config_ = config;
    command_sid_ = static_cast<uint16_t>(sid);
    const int result = SRNOS_CANRouterAttachAdapter(channel, this, &ops, period_ms);
    if (result == 0) channel_ = channel;
    return result;
}

int DMMotor::unregisterCan() {
    if (!channel_) return -1;
    const int result = SRNOS_CANRouterDetachAdapter(channel_, this);
    if (result == 0) {
        channel_ = 0;
        command_ = {};
        pending_ = 0;
        wraps_ = 0;
        feedback_ = {};
    }
    return result;
}

bool DMMotor::publish(const SRNOS_CANFrame& frame) {
    Detail::StateGuard guard;
    command_ = frame;
    command_.id = command_sid_;
    return true;
}

bool DMMotor::setMIT(float p, float v, float kp, float kd, float torque) {
    if (!channel_ || config_.mode != DMMode::MIT || !std::isfinite(p) || !std::isfinite(v) ||
        !std::isfinite(kp) || !std::isfinite(kd) || !std::isfinite(torque)) return false;
    const auto& lim = config_.limits;
    const uint16_t pos = encodeSigned(p, lim.pmax_rad, 65535);
    const uint16_t vel = encodeSigned(v, lim.vmax_rad_s, 4095);
    const uint16_t k = quantize(kp / 500.f, 4095);
    const uint16_t d = quantize(kd / 5.f, 4095);
    const uint16_t t = encodeSigned(torque, lim.tmax_nm, 4095);
    SRNOS_CANFrame frame{};
    frame.dlc = 8;
    (void)Endian::storeBE16(frame.data, 0, pos);
    frame.data[2] = static_cast<uint8_t>(vel >> 4);
    frame.data[3] = static_cast<uint8_t>((vel << 4) | (k >> 8));
    frame.data[4] = static_cast<uint8_t>(k);
    frame.data[5] = static_cast<uint8_t>(d >> 4);
    frame.data[6] = static_cast<uint8_t>((d << 4) | (t >> 8));
    frame.data[7] = static_cast<uint8_t>(t);
    // These bit patterns are interpreted as management commands by the drive.
    if (pos == 65535 && vel == 4095 && k == 4095 && d == 4095 && t >= 0xFFBu && t <= 0xFFEu)
        return false;
    return publish(frame);
}

bool DMMotor::setPosVel(float p, float vmax) {
    if (!channel_ || config_.mode != DMMode::PosVel || !std::isfinite(p) ||
        !std::isfinite(vmax) || vmax < 0) return false;
    SRNOS_CANFrame frame{};
    frame.dlc = 8;
    writeFloat(frame, 0, p);
    writeFloat(frame, 4, vmax);
    return publish(frame);
}

bool DMMotor::setVelocity(float v) {
    if (!channel_ || config_.mode != DMMode::Velocity || !std::isfinite(v)) return false;
    SRNOS_CANFrame frame{};
    frame.dlc = 4;
    writeFloat(frame, 0, v);
    return publish(frame);
}

bool DMMotor::setForcePosition(float p, float vmax, float current) {
    if (!channel_ || config_.mode != DMMode::ForcePosition || !std::isfinite(p) ||
        !std::isfinite(vmax) || !std::isfinite(current)) return false;
    SRNOS_CANFrame frame{};
    frame.dlc = 8;
    writeFloat(frame, 0, p);
    (void)Endian::storeLE16(frame.data, 4, static_cast<uint16_t>(std::clamp(vmax, 0.f, 100.f) * 100.f + .5f));
    (void)Endian::storeLE16(frame.data, 6, quantize(current, 10000));
    return publish(frame);
}

bool DMMotor::requestEnable() {
    if (!channel_) return false;
    Detail::StateGuard guard;
    pending_ = enable_code;
    return true;
}

bool DMMotor::requestDisable() {
    if (!channel_) return false;
    Detail::StateGuard guard;
    pending_ = disable_code;
    return true;
}

void DMMotor::receive(const SRNOS_CANFrame& frame, uint32_t now_ms) {
    if (frame.flags || frame.dlc != 8 || frame.id != config_.feedback_sid ||
        (frame.data[0] & 0x0f) != (config_.can_id & 0x0f)) return;
    const auto& data = frame.data;
    DMFeedback next{};
    (void)Endian::loadBE16(data, 1, next.position_raw);
    next.velocity_raw = (data[3] << 4) | (data[4] >> 4);
    next.torque_raw = ((data[4] & 0x0f) << 8) | data[5];
    next.position_rad = decodeSigned(next.position_raw, 65535, config_.limits.pmax_rad);
    next.velocity_rad_s = decodeSigned(next.velocity_raw, 4095, config_.limits.vmax_rad_s);
    next.torque_nm = decodeSigned(next.torque_raw, 4095, config_.limits.tmax_nm);
    next.drive_status = data[0] >> 4;
    next.mos_temp_c = data[6];
    next.rotor_temp_c = data[7];
    next.last_rx_ms = now_ms;
    next.online = true;
    const double period = config_.feedback_period_rad > 0 ? config_.feedback_period_rad : 2.0 * config_.limits.pmax_rad;

    Detail::StateGuard guard;
    if (!feedback_.online || now_ms - feedback_.last_rx_ms > config_.offline_ms) {
        wraps_ = 0;
    } else {
        const double delta = static_cast<double>(next.position_rad) - feedback_.position_rad;
        if (delta < -.5 * period) ++wraps_;
        else if (delta > .5 * period) --wraps_;
    }
    next.continuous_position_rad = next.position_rad + static_cast<double>(wraps_) * period;
    feedback_ = next;
}

DMFeedback DMMotor::feedback() const {
    Detail::StateGuard guard;
    return feedback_;
}
} // namespace SRNOS::Devices
