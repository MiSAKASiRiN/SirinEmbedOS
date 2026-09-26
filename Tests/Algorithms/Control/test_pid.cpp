#include <cassert>
#include <cmath>
#include <limits>

#include "SirinEmbedOS/Algorithms/Control/control_extensions.hpp"
#include "SirinEmbedOS/Algorithms/Control/pid.hpp"

using namespace omx::control;

namespace {

bool near(float left, float right, float tolerance = 1.0e-4f)
{
    return std::fabs(left - right) <= tolerance;
}

PidConfig baseConfig()
{
    PidConfig config {};
    config.gains = {2.0f, 1.0f, 0.0f};
    config.output_min = -10.0f;
    config.output_max = 10.0f;
    config.integral_min = -5.0f;
    config.integral_max = 5.0f;
    config.min_dt_s = 0.001f;
    config.max_dt_s = 0.2f;
    config.fail_output = -1.0f;
    return config;
}

void testConfigurationAndInstances()
{
    Pid invalid;
    PidConfig bad = baseConfig();
    bad.output_min = bad.output_max;
    assert(invalid.configure(bad) == PidStatus::InvalidConfig);
    assert(invalid.step({0, 0, 0, 0.01f}).status == PidStatus::NotConfigured);

    Pid left;
    Pid right;
    assert(left.configure(baseConfig()) == PidStatus::Ok);
    assert(right.configure(baseConfig()) == PidStatus::Ok);
    assert(near(left.step({2, 0, 0, 0.1f}).output, 4.2f));
    assert(near(right.step({-1, 0, 0, 0.1f}).output, -2.1f));
    left.track(2.0f, 1.0f, 3.0f);
    assert(near(left.step({2, 1, 0, 0.1f}).output, 3.1f));
    left.reset(1.0f, 0.0f);
    assert(near(left.step({2, 1, 0, 0.1f}).d, 0.0f));
}

void testIncrementalAndVariableDt()
{
    PidConfig config = baseConfig();
    config.form = PidForm::Incremental;
    Pid pid;
    assert(pid.configure(config) == PidStatus::Ok);
    const auto first = pid.step({2, 0, 0, 0.1f});
    assert(near(first.output, 4.2f));
    assert(near(pid.step({2, 0, 0, 0.05f}).output, 4.3f));

    const PidResult before = pid.lastResult();
    assert(pid.step({2, 0, 0, 0.0f}).status == PidStatus::InvalidDt);
    assert(pid.step({2, 0, 0, 0.3f}).status == PidStatus::InvalidDt);
    assert(pid.step({2, std::numeric_limits<float>::quiet_NaN(), 0, 0.1f}).status ==
           PidStatus::InvalidInput);
    const PidResult infinite = pid.step(
        {std::numeric_limits<float>::infinity(), 0, 0, 0.1f});
    assert(infinite.status == PidStatus::InvalidInput);
    assert(near(infinite.output, -1.0f));
    assert(near(before.output, 4.3f));
}

void testDerivativeAndPeriodicError()
{
    PidConfig config = baseConfig();
    config.gains = {1.0f, 0.0f, 1.0f};
    config.output_min = -1000.0f;
    config.output_max = 1000.0f;
    config.integral_min = 0.0f;
    config.integral_max = 0.0f;
    config.error_period = 360.0f;
    config.derivative_mode = DerivativeMode::OnMeasurement;
    Pid pid;
    assert(pid.configure(config) == PidStatus::Ok);
    pid.reset(179.0f);
    const PidResult first = pid.step({-179.0f, 179.0f, 0.0f, 0.01f});
    assert(near(first.error, 2.0f));
    assert(near(first.d, 0.0f));
    const PidResult crossing = pid.step({-179.0f, -179.0f, 0.0f, 0.01f});
    assert(near(crossing.d, -200.0f));

    config.derivative_mode = DerivativeMode::OnError;
    config.derivative_cutoff_hz = 5.0f;
    assert(pid.configure(config) == PidStatus::Ok);
    pid.step({0, 0, 0, 0.01f});
    const PidResult filtered = pid.step({10, 0, 0, 0.01f});
    assert(filtered.d > 0.0f && filtered.d < 1000.0f);
}

void testLimitsFiltersAndFeedforward()
{
    PidConfig config = baseConfig();
    config.gains = {5.0f, 10.0f, 0.0f};
    config.output_min = -2.0f;
    config.output_max = 3.0f;
    config.integral_min = -0.5f;
    config.integral_max = 0.5f;
    config.anti_windup = AntiWindupMode::Conditional;
    Pid pid;
    assert(pid.configure(config) == PidStatus::Ok);
    PidResult result = pid.step({10, 0, 0, 0.1f});
    assert(result.saturated && near(result.output, 3.0f));
    assert(near(result.i, 0.0f));

    config.anti_windup = AntiWindupMode::BackCalculation;
    config.back_calculation_gain = 2.0f;
    assert(pid.configure(config) == PidStatus::Ok);
    result = pid.step({10, 0, 0, 0.1f});
    assert(result.i < 0.5f);

    config.gains = {1.0f, 0.0f, 0.0f};
    config.anti_windup = AntiWindupMode::Clamp;
    config.output_min = -100.0f;
    config.output_max = 100.0f;
    config.output_cutoff_hz = 2.0f;
    config.slew_up_per_s = 1.0f;
    config.slew_down_per_s = 2.0f;
    config.error_deadband = 0.5f;
    config.integral_zone = 1.0f;
    assert(pid.configure(config) == PidStatus::Ok);
    result = pid.step({0.4f, 0, 2.0f, 0.1f});
    assert(near(result.error, 0.0f));
    assert(result.slew_limited);
    assert(near(result.output, 0.1f));
    assert(near(result.feedforward, 2.0f));

    config.output_cutoff_hz = 0.0f;
    config.slew_up_per_s = 0.0f;
    config.slew_down_per_s = 0.0f;
    config.error_deadband = 0.0f;
    config.gains = {0.0f, 10.0f, 0.0f};
    config.integral_zone = 1.0f;
    assert(pid.configure(config) == PidStatus::Ok);
    result = pid.step({2.0f, 0.0f, 0.0f, 0.1f});
    assert(near(result.i, 0.0f));
}

void testLegacyIncrementalScaling()
{
    PidConfig config = baseConfig();
    config.form = PidForm::Incremental;
    config.gains = {100.0f, 50000.0f, 0.01f};
    config.derivative_mode = DerivativeMode::OnError;
    config.output_min = -1.0e6f;
    config.output_max = 1.0e6f;
    config.integral_min = -1.0e6f;
    config.integral_max = 1.0e6f;
    Pid pid;
    assert(pid.configure(config) == PidStatus::Ok);
    const float dt = 0.001f;
    const PidResult first = pid.step({1.0f, 0.0f, 0.0f, dt});
    assert(near(first.output, 150.0f)); // 100*e + (50/1ms)*e*dt; first D is zero.
    const PidResult second = pid.step({2.0f, 0.0f, 0.0f, dt});
    assert(near(second.output, 360.0f));
    assert(near(second.d, 10.0f));
    const PidResult third = pid.step({4.0f, 0.0f, 0.0f, dt});
    // delta-D = 0.01/0.001*(4 - 2*2 + 1) = 10, equal to old Kd=10.
    assert(near(third.output, 770.0f));
}

void testControllerExtensions()
{
    FuzzySchedulerConfig fuzzy_config {};
    fuzzy_config.base_gains = {1.0f, 2.0f, 3.0f};
    fuzzy_config.kp_ratio = 0.1f;
    fuzzy_config.error_step = 1.0f;
    fuzzy_config.error_rate_step = 10.0f;
    FuzzyGainScheduler scheduler;
    assert(scheduler.configure(fuzzy_config));
    const PidGains scheduled = scheduler.step(1.0f, 0.01f);
    assert(std::isfinite(scheduled.kp) && scheduled.kp >= 1.0f);

    Feedforward feedforward;
    FeedforwardConfig feedforward_config {};
    feedforward_config.coefficients = {0.0f, 1.0f, 0.0f};
    feedforward_config.output_min = -10.0f;
    feedforward_config.output_max = 10.0f;
    assert(feedforward.configure(feedforward_config));
    feedforward.reset(0.0f);
    float feedforward_output = 0.0f;
    for (int index = 1; index <= 8; ++index) {
        feedforward_output = feedforward.step(0.1f * static_cast<float>(index),
                                              0.1f);
    }
    assert(near(feedforward_output, 1.0f, 0.01f));

    LinearDisturbanceObserver observer;
    LinearDisturbanceObserverConfig observer_config {};
    observer_config.coefficients = {1.0f, 0.0f, 0.0f};
    observer_config.disturbance_min = -10.0f;
    observer_config.disturbance_max = 10.0f;
    assert(observer.configure(observer_config));
    assert(near(observer.step(2.0f, 0.5f, 0.01f), 1.5f));

    TrackingDifferentiator differentiator;
    assert(differentiator.configure({5.0f, 20.0f}));
    const auto td = differentiator.step(1.0f, 0.01f);
    assert(td.value > 0.0f && td.derivative > 0.0f && td.acceleration > 0.0f);
}

} // namespace

int main()
{
    testConfigurationAndInstances();
    testIncrementalAndVariableDt();
    testDerivativeAndPeriodicError();
    testLimitsFiltersAndFeedforward();
    testLegacyIncrementalScaling();
    testControllerExtensions();
    return 0;
}
