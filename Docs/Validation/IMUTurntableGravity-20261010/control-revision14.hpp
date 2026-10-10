#pragma once
#include "SirinEmbedOS/Algorithms/Control/pid.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace SRNOS::Examples::Turntable {
constexpr float DegreesPerCount = 360.0f / 8192.0f;
constexpr int Defaults[3] = {350, 4850, 5140};
constexpr int SoftMin = 4200, SoftMax = 2160;
constexpr int ArcCounts = 8192 - SoftMin + SoftMax;
constexpr int BootRecoveryMin = 4000; // Only the confirmed gravity-stop neighbourhood.
// Measured on this turntable: positive ID1 voltage decreases encoder raw.
constexpr int CommandSign[3] = {-1, 1, 1};
constexpr float MaxVoltage = 30000.0f;
constexpr float VoltageLimits[3] = {30000.0f, 30000.0f, 30000.0f}; // Protocol voltage units, NOT torque or amperes.
constexpr float SpeedLimits[3] = {54.0f, 27.0f, 27.0f};
constexpr float PositionKp = 4.0f;
constexpr int WaitingMaxRpm = 5;
constexpr uint32_t SequenceRevision = 14;
// This fixture only: 80% of the pooled run14/15 stationary load fit.
// Angle is the measured ID3 arc position, with raw4200 at zero. Units are
// protocol voltage, not torque. PI retains the residual/friction correction.
inline float gravity_voltage3(float position_deg) {
    const float radians = position_deg * (3.14159265358979323846f / 180.0f);
    return 3120.0f * std::cos(radians) + 4240.0f * std::sin(radians);
}
constexpr float SweepDegrees = 361.0f; // Arrival tolerance must still leave >= one turn.
constexpr int EndMarginCounts = 64;
constexpr float LowerTarget = EndMarginCounts*DegreesPerCount;
constexpr float UpperTarget = (ArcCounts-EndMarginCounts)*DegreesPerCount;
enum class Phase : uint32_t { Waiting, Recover3, Home3, Home1, Home2, Baseline,
                             Sweep1, Dwell, Return1, Hold, Fault, Baseline2, Sweep2, Dwell2, Return2,
                             Baseline3, Lower3, DwellLower3, Upper3, DwellUpper3, Return3 };
enum class Fault : uint32_t { None, Feedback, InitialLimit, Limit, Overspeed,
                             Temperature, Timing, Stalled, Timeout, Stop, Controller, RawOverspeed };
struct Feedback { uint16_t raw; bool online; uint8_t temperature; int16_t rpm = 0; };
inline int wrap_delta(int delta) {
    if (delta > 4096) delta -= 8192;
    if (delta < -4096) delta += 8192;
    return delta;
}
inline bool allowed3(int raw) { return raw >= SoftMin || raw <= SoftMax; }
inline int arc3(int raw) { return raw >= SoftMin ? raw-SoftMin : raw+8192-SoftMin; }
struct Axis {
    int previous_raw = 0, total_raw = 0;
    float position = 0, speed = 0, control_speed = 0, target = 0, home = 0;
    float stall_seconds = 0, stall_start = 0;
    int16_t command = 0;
    bool enabled = false;
    SRNOS::Control::Pid pid;
};
class Controller {
    bool initialized_ = false, waiting_seeded_ = false, boot_recovery_ = false;
    int waiting_raw_[3]{};
    float waiting_ = 0, phase_seconds_ = 0, settled_ = 0;
    float recovery_floor_ = 0;
    void transition(Phase next) { phase = next; phase_seconds_ = settled_ = 0; }
    bool arrived(unsigned index, float dt) {
        const Axis &a = axis[index];
        if (std::fabs(a.position-a.target) <= 8*DegreesPerCount && std::fabs(a.speed) < .5f)
            settled_ += dt;
        else settled_ = 0;
        return settled_ >= 1.0f;
    }
public:
    Axis axis[3];
    Phase phase = Phase::Waiting;
    Fault fault = Fault::None;
    uint32_t fault_axis = 0;
    float fault_measurement = 0;
    void stop(Fault reason, uint32_t which_axis=0, float measured=0) {
        if (fault == Fault::None) {fault=reason;fault_axis=which_axis;fault_measurement=measured;}
        phase = Phase::Fault;
        for (auto &a:axis) { a.command=0; a.enabled=false; }
    }
    void step(const Feedback (&feedback)[3], float dt, bool requested_stop=false) {
        if (requested_stop) stop(Fault::Stop);
        if (phase == Phase::Fault) return;
        if (!(dt > 0 && dt <= .010f)) { stop(Fault::Timing); return; }
        bool ready = true;
        for (unsigned index=0;index<3;++index) {
            const auto &f=feedback[index];
            ready &= f.online && f.raw < 8192;
            const int rpm=std::abs(int(f.rpm));
            if (!initialized_ && rpm>=WaitingMaxRpm) {
                waiting_=0;waiting_seeded_=false;return;
            }
            if (f.temperature >= 65) { stop(Fault::Temperature,index+1,f.temperature); return; }
        }
        if (!ready) {
            if (initialized_) stop(Fault::Feedback);
            else {waiting_=0;waiting_seeded_=false;}
            return;
        }
        if (!initialized_) {
            if(!waiting_seeded_) {
                for(unsigned i=0;i<3;++i)waiting_raw_[i]=feedback[i].raw;
                waiting_seeded_=true;
            }
            for(unsigned i=0;i<3;++i) {
                if(std::abs(wrap_delta(int(feedback[i].raw)-waiting_raw_[i]))>8) {
                    waiting_=0;
                    for(unsigned j=0;j<3;++j)waiting_raw_[j]=feedback[j].raw;
                    return;
                }
            }
            waiting_ += dt;
            if (waiting_ < 2) return;
            const int raw3=feedback[2].raw;
            const bool recovery = !allowed3(raw3) && raw3 >= BootRecoveryMin && raw3 < SoftMin;
            if (!allowed3(raw3) && !recovery) { stop(Fault::InitialLimit,3,raw3); return; }
            for (unsigned i=0;i<3;++i) {
                auto &a=axis[i];
                a.previous_raw=feedback[i].raw;
                a.total_raw=i==2 ? (recovery ? raw3-SoftMin : arc3(raw3)) : feedback[i].raw;
                a.position=a.total_raw*DegreesPerCount;
                a.home=i==2 ? (Defaults[i]-SoftMin)*DegreesPerCount :
                    a.position+wrap_delta(Defaults[i]-feedback[i].raw)*DegreesPerCount;
                // Hold the other axes at their starting position while ID3 lifts.
                a.target=i==2 ? a.home : a.position;
                a.enabled=true;
                SRNOS::Control::PidConfig c;
                c.gains={i==1 ? 200.0f : 100.0f,100.0f,0};
                c.output_min=c.integral_min=-VoltageLimits[i];
                c.output_max=c.integral_max=VoltageLimits[i];
                c.anti_windup=i==2 ? SRNOS::Control::AntiWindupMode::BackCalculation :
                                    SRNOS::Control::AntiWindupMode::Conditional;
                c.back_calculation_gain=i==2 ? 5.0f : 0.0f;
                // Algebraic positive output also brakes negative motion.
                // Track slew-limited output so ID3's integrator cannot run ahead.
                c.slew_up_per_s=40000;
                c.slew_down_per_s=40000;
                c.max_dt_s=.010f;
                if (a.pid.configure(c)!=SRNOS::Control::PidStatus::Ok) {stop(Fault::Controller);return;}
            }
            axis[2].enabled=true;
            recovery_floor_=axis[2].position-12*DegreesPerCount;
            initialized_=true;
            boot_recovery_=recovery;
            transition(recovery ? Phase::Recover3 : Phase::Home3);
        }
        phase_seconds_ += dt;
        if (phase != Phase::Hold && phase_seconds_ > 240) {stop(Fault::Timeout);return;}
        for (unsigned i=0;i<3;++i) {
            auto &a=axis[i];
            const int delta=wrap_delta(int(feedback[i].raw)-a.previous_raw);
            a.previous_raw=feedback[i].raw;
            a.total_raw+=delta;
            a.position=a.total_raw*DegreesPerCount;
            a.speed+=(delta*DegreesPerCount/dt-a.speed)*dt/(.100f+dt);
            // Short feedback delay on ID3 damps the direct-loop load oscillation.
            a.control_speed+=(delta*DegreesPerCount/dt-a.control_speed)*dt/((i==1 ? .100f : .020f)+dt);
        }
        if (boot_recovery_) {
            // The user-defined startup exception lasts until ID3 reaches home.
            if (axis[2].position<recovery_floor_ || (feedback[2].raw<BootRecoveryMin && axis[2].position<0) || axis[2].position>ArcCounts*DegreesPerCount) {
                stop(Fault::Limit,3,feedback[2].raw);return;
            }
            if (phase==Phase::Recover3 && axis[2].position >= 0) transition(Phase::Home3);
        } else if (axis[2].position<0 || axis[2].position>ArcCounts*DegreesPerCount) {
            stop(Fault::Limit,3,feedback[2].raw);return;
        }
        for (unsigned i=0;i<3;++i) {
            auto &a=axis[i];
            if (!a.enabled) {a.command=0;continue;}
            // Direct cascade: position P -> bounded speed target -> speed PI -> voltage.
            const float requested=std::clamp(PositionKp*(a.target-a.position),-SpeedLimits[i],SpeedLimits[i]);
            const float feedforward=i==2 ? gravity_voltage3(a.position) : 0.0f;
            const auto result=a.pid.step({requested,a.control_speed,feedforward,dt});
            if(result.status!=SRNOS::Control::PidStatus::Ok){stop(Fault::Controller);return;}
            a.command=static_cast<int16_t>(CommandSign[i]*result.output);
            // Encoder quantization/noise must not continually reset a blocked-axis timer.
            if (std::fabs(result.output)>.95f*VoltageLimits[i] && std::fabs(a.target-a.position)>1) {
                if (a.stall_seconds==0) a.stall_start=a.position;
                a.stall_seconds+=dt;
                if (a.stall_seconds>=3) {
                    const float progress=std::fabs(a.position-a.stall_start);
                    if (progress<.6f) {stop(Fault::Stalled,i+1,progress);return;}
                    a.stall_seconds=0;
                }
            } else a.stall_seconds=0;
        }
        if (phase==Phase::Recover3 && axis[2].command<0) axis[2].command=0;
        if (phase!=Phase::Recover3 &&
            ((axis[2].position<32*DegreesPerCount && axis[2].command<0) ||
             (axis[2].position>(ArcCounts-32)*DegreesPerCount && axis[2].command>0))) axis[2].command=0;
        switch(phase) {
        case Phase::Home3: if(arrived(2,dt)){boot_recovery_=false;axis[0].target=axis[0].home;transition(Phase::Home1);} break;
        case Phase::Home1: if(arrived(0,dt)){axis[1].target=axis[1].home;transition(Phase::Home2);} break;
        case Phase::Home2: if(arrived(1,dt))transition(Phase::Baseline); break;
        case Phase::Baseline: if(phase_seconds_>=5){axis[0].target=axis[0].home+SweepDegrees;transition(Phase::Sweep1);} break;
        case Phase::Sweep1: if(arrived(0,dt))transition(Phase::Dwell); break;
        case Phase::Dwell: if(phase_seconds_>=3){axis[0].target=axis[0].home;transition(Phase::Return1);} break;
        case Phase::Return1: if(arrived(0,dt))transition(Phase::Baseline2); break;
        case Phase::Baseline2: if(phase_seconds_>=5){axis[1].target=axis[1].home+SweepDegrees;transition(Phase::Sweep2);} break;
        case Phase::Sweep2: if(arrived(1,dt))transition(Phase::Dwell2); break;
        case Phase::Dwell2: if(phase_seconds_>=3){axis[1].target=axis[1].home;transition(Phase::Return2);} break;
        case Phase::Return2: if(arrived(1,dt))transition(Phase::Baseline3); break;
        case Phase::Baseline3: if(phase_seconds_>=5){axis[2].target=LowerTarget;transition(Phase::Lower3);} break;
        case Phase::Lower3: if(arrived(2,dt))transition(Phase::DwellLower3); break;
        case Phase::DwellLower3: if(phase_seconds_>=3){axis[2].target=UpperTarget;transition(Phase::Upper3);} break;
        case Phase::Upper3: if(arrived(2,dt))transition(Phase::DwellUpper3); break;
        case Phase::DwellUpper3: if(phase_seconds_>=3){axis[2].target=axis[2].home;transition(Phase::Return3);} break;
        case Phase::Return3: if(arrived(2,dt))transition(Phase::Hold); break;
        default: break;
        }
    }
};
} // namespace SRNOS::Examples::Turntable
