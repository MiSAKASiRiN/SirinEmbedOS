#pragma once
// Joint offsets from defaults, in encoder-positive degrees.
namespace SRNOS::Examples::Turntable {
constexpr float CalibrationTargets[][3] = {
    {25.0f,0.0f,0.0f}, // probe_forward pose 0 axis 1
    {0.0f,0.0f,0.0f}, // probe_return pose 0 axis 1
    {0.0f,25.0f,0.0f}, // probe_forward pose 0 axis 2
    {0.0f,0.0f,0.0f}, // probe_return pose 0 axis 2
    {0.0f,0.0f,25.0f}, // probe_forward pose 0 axis 3
    {0.0f,0.0f,0.0f}, // probe_return pose 0 axis 3
    {0.0f,0.0f,45.0f}, // reposition pose 1 axis 3
    {25.0f,0.0f,45.0f}, // probe_forward pose 1 axis 1
    {0.0f,0.0f,45.0f}, // probe_return pose 1 axis 1
    {0.0f,25.0f,45.0f}, // probe_forward pose 1 axis 2
    {0.0f,0.0f,45.0f}, // probe_return pose 1 axis 2
    {0.0f,0.0f,70.0f}, // probe_forward pose 1 axis 3
    {0.0f,0.0f,45.0f}, // probe_return pose 1 axis 3
    {0.0f,45.0f,45.0f}, // reposition pose 2 axis 2
    {0.0f,45.0f,0.0f}, // reposition pose 2 axis 3
    {25.0f,45.0f,0.0f}, // probe_forward pose 2 axis 1
    {0.0f,45.0f,0.0f}, // probe_return pose 2 axis 1
    {0.0f,70.0f,0.0f}, // probe_forward pose 2 axis 2
    {0.0f,45.0f,0.0f}, // probe_return pose 2 axis 2
    {0.0f,45.0f,25.0f}, // probe_forward pose 2 axis 3
    {0.0f,45.0f,0.0f}, // probe_return pose 2 axis 3
    {45.0f,45.0f,0.0f}, // reposition pose 3 axis 1
    {45.0f,0.0f,0.0f}, // reposition pose 3 axis 2
    {70.0f,0.0f,0.0f}, // probe_forward pose 3 axis 1
    {45.0f,0.0f,0.0f}, // probe_return pose 3 axis 1
    {45.0f,25.0f,0.0f}, // probe_forward pose 3 axis 2
    {45.0f,0.0f,0.0f}, // probe_return pose 3 axis 2
    {45.0f,0.0f,25.0f}, // probe_forward pose 3 axis 3
    {45.0f,0.0f,0.0f}, // probe_return pose 3 axis 3
    {20.0f,15.0f,20.0f}, // coupled_validation pose 4 axis 0
    {0.0f,0.0f,0.0f}, // coupled_return pose 4 axis 0
};
constexpr unsigned CalibrationStepCount=sizeof(CalibrationTargets)/sizeof(CalibrationTargets[0]);
}
