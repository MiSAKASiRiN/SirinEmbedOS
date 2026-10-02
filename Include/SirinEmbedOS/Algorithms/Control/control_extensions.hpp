#ifndef SRNOS_CONTROL_EXTENSIONS_HPP
#define SRNOS_CONTROL_EXTENSIONS_HPP

#include <array>

#include "SirinEmbedOS/Algorithms/Math/ols.hpp"
#include "SirinEmbedOS/Algorithms/Control/pid.hpp"
#include "SirinEmbedOS/Algorithms/Math/signal.hpp"

namespace SRNOS::Control {

struct FuzzySchedulerConfig {
    PidGains base_gains {};
    float kp_ratio {0.0f};
    float ki_ratio {0.0f};
    float kd_ratio {0.0f};
    float error_step {1.0f};
    float error_rate_step {1.0f};
};

class FuzzyGainScheduler {
public:
    bool configure(const FuzzySchedulerConfig&) noexcept;
    void reset(float error = 0.0f) noexcept;
    PidGains step(float error, float dt_s) noexcept;

private:
    static float interpolate(const std::array<float, 49>& table,
                             float error_index,
                             float rate_index) noexcept;

    FuzzySchedulerConfig config_ {};
    float previous_error_ {0.0f};
    bool configured_ {false};
    bool initialized_ {false};
};

class FuzzyPid {
public:
    PidStatus configure(const PidConfig&, const FuzzySchedulerConfig&) noexcept;
    void reset(float measurement = 0.0f, float tracked_output = 0.0f) noexcept;
    void track(float setpoint, float measurement, float actual_output) noexcept;
    PidResult step(const PidInput&) noexcept;
    const PidResult& lastResult() const noexcept { return pid_.lastResult(); }

private:
    Pid pid_ {};
    FuzzyGainScheduler scheduler_ {};
};

struct FeedforwardConfig {
    std::array<float, 3> coefficients {}; // c0*x + c1*dx + c2*ddx
    float output_min {-1.0f};
    float output_max {1.0f};
    float derivative_cutoff_hz {0.0f};
};

class Feedforward {
public:
    bool configure(const FeedforwardConfig&) noexcept;
    void reset(float reference = 0.0f) noexcept;
    float step(float reference, float dt_s) noexcept;

private:
    FeedforwardConfig config_ {};
    SRNOS::Math::OlsWindow<5> reference_history_ {};
    SRNOS::Math::OlsWindow<5> derivative_history_ {};
    SRNOS::Math::FirstOrderLowPass derivative_filter_ {};
    bool configured_ {false};
};

struct LinearDisturbanceObserverConfig {
    std::array<float, 3> coefficients {}; // c0*y + c1*dy + c2*ddy
    float disturbance_min {-1.0f};
    float disturbance_max {1.0f};
    float deadband {0.0f};
    float cutoff_hz {0.0f};
};

class LinearDisturbanceObserver {
public:
    bool configure(const LinearDisturbanceObserverConfig&) noexcept;
    void reset(float measurement = 0.0f) noexcept;
    float step(float measurement, float plant_input, float dt_s) noexcept;

private:
    LinearDisturbanceObserverConfig config_ {};
    SRNOS::Math::OlsWindow<5> measurement_history_ {};
    SRNOS::Math::OlsWindow<5> derivative_history_ {};
    SRNOS::Math::FirstOrderLowPass output_filter_ {};
    bool configured_ {false};
};

struct TrackingDifferentiatorConfig {
    float response {1.0f};
    float acceleration_limit {1.0f};
};

struct TrackingDifferentiatorResult {
    float value {0.0f};
    float derivative {0.0f};
    float acceleration {0.0f};
};

class TrackingDifferentiator {
public:
    bool configure(const TrackingDifferentiatorConfig&) noexcept;
    void reset(float value = 0.0f) noexcept;
    TrackingDifferentiatorResult step(float input, float dt_s) noexcept;
    const TrackingDifferentiatorResult& lastResult() const noexcept { return result_; }

private:
    TrackingDifferentiatorConfig config_ {};
    TrackingDifferentiatorResult result_ {};
    bool configured_ {false};
};

} // namespace SRNOS::Control

#endif
