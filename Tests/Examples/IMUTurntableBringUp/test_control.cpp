#include "../../../Examples/IMUTurntableBringUp/control.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace SRNOS::Examples::Turntable;
static void initialize(Controller &c, Feedback (&f)[3]) {
    for(int i=0;i<2001;++i)c.step(f,.001f);
}
int main(int argc,char **argv) {
    const bool guards_only=argc>1 && std::strcmp(argv[1],"--guards-only")==0;
    static_assert(SpeedLimits[0]==54 && SpeedLimits[1]==27 && SpeedLimits[2]==27);
    static_assert(VoltageLimits[0]==30000 && VoltageLimits[1]==30000 && VoltageLimits[2]==30000);
    assert(allowed3(4200)&&allowed3(8191)&&allowed3(0)&&allowed3(2160));
    assert(!allowed3(4199)&&!allowed3(2161));
    assert(wrap_delta(10-8190)==12&&wrap_delta(8190-10)==-12);
    {Controller c;Feedback f[3]={{350,true,25},{4850,true,25},{3000,true,25}};
     initialize(c,f);assert(c.fault==Fault::InitialLimit);}
    {Controller c;Feedback f[3]={{350,true,25},{4850,true,25},{4044,true,25}};
     initialize(c,f);assert(c.phase==Phase::Recover3);
     for(int raw=4044;raw<=4200;++raw){f[2].raw=raw;for(int t=0;t<50;++t)c.step(f,.001f);}
     assert(c.phase==Phase::Home3&&c.fault==Fault::None);
     f[2].raw=4199;c.step(f,.001f);assert(c.fault==Fault::None);
     for(int raw=4200;raw<=5140;++raw){f[2].raw=raw;for(int t=0;t<50;++t)c.step(f,.001f);}
     for(int t=0;t<2000;++t)c.step(f,.001f);
     assert(c.fault==Fault::None&&c.phase!=Phase::Home3);
     f[2].raw=4199;c.step(f,.001f);assert(c.fault==Fault::Limit&&c.fault_axis==3);
     for(auto &a:c.axis)assert(a.command==0);
     f[2].raw=5140;c.step(f,.001f);assert(c.phase==Phase::Fault);}
    {Controller c;Feedback f[3]={{350,true,25},{4850,true,25},{5140,true,25}};
     initialize(c,f);f[0].online=false;c.step(f,.001f);assert(c.fault==Fault::Feedback);}
    {Controller c;Feedback f[3]={{350,true,25},{4850,true,25},{5140,true,25}};
     initialize(c,f);c.step(f,.030f);assert(c.fault==Fault::Timing);}
    {Controller c;Feedback f[3]={{350,true,25},{4850,true,25},{5140,true,25}};
     initialize(c,f);f[1].temperature=65;c.step(f,.001f);assert(c.fault==Fault::Temperature);}
    {Controller c;Feedback f[3]={{350,true,25},{4850,true,25},{5140,true,25}};
     initialize(c,f);f[0].raw+=300;c.step(f,.001f);assert(c.fault==Fault::None);}
    {Controller c;Feedback f[3]={{350,true,25},{4850,true,25},{4044,true,25}};
     for(int t=0;t<45000;++t)c.step(f,.001f);
     assert(c.fault==Fault::Stalled);for(auto &a:c.axis)assert(a.command==0);}
    {Controller c;Feedback f[3]={{350,true,25,6},{4850,true,25},{4044,true,25}};
     initialize(c,f);assert(c.phase==Phase::Waiting&&c.fault==Fault::None);
     for(auto &a:c.axis)assert(a.command==0);
     f[0].rpm=0;initialize(c,f);assert(c.phase==Phase::Recover3);}
    {Controller c;Feedback f[3]={{350,true,25},{4850,true,25},{4044,true,25}};
     for(int i=0;i<1000;++i)c.step(f,.001f);
     f[0].raw=370;c.step(f,.001f);
     for(int i=0;i<1000;++i)c.step(f,.001f);assert(c.phase==Phase::Waiting);
     initialize(c,f);assert(c.phase==Phase::Recover3);}
    {Controller c;Feedback f[3]={{350,true,25},{4850,true,25},{4044,true,25}};
     initialize(c,f);assert(c.axis[0].enabled&&c.axis[1].enabled&&c.axis[2].enabled);
     assert(c.axis[0].target==c.axis[0].position&&c.axis[1].target==c.axis[1].position);
     f[1].rpm=5;c.step(f,.001f);assert(c.fault==Fault::None);
     f[1].rpm=-7;for(int t=0;t<51;++t)c.step(f,.001f);
     assert(c.fault==Fault::None);
     c.stop(Fault::Stop);assert(c.fault==Fault::Stop);
     for(auto &axis:c.axis)assert(axis.command==0);}
    // Runtime raw-RPM overspeed stops are disabled, including large excursions.
    {Controller c;Feedback f[3]={{350,true,25},{4850,true,25},{5140,true,25}};
     initialize(c,f);
     for(int t=0;t<1000;++t){f[0].rpm=t%20<10 ? 14 : 0;c.step(f,.001f);assert(c.fault==Fault::None);}
     f[0].rpm=24;c.step(f,.001f);assert(c.fault==Fault::None);}
    // Filtered speed no longer causes either confirmed or immediate overspeed stops.
    {Controller c;Feedback f[3]={{350,true,25},{4850,true,25},{5140,true,25}};
     initialize(c,f);
     for(int t=0;t<100;++t){c.axis[0].speed=t%20<10 ? 74 : 0;c.step(f,.001f);assert(c.fault==Fault::None);}
     for(int t=0;t<51;++t){c.axis[0].speed=74;c.step(f,.001f);}
     assert(c.fault==Fault::None);}
    {Controller c;Feedback f[3]={{350,true,25},{4850,true,25},{5140,true,25}};
     initialize(c,f);c.axis[0].speed=120;c.step(f,.001f);assert(c.fault==Fault::None);}
    // A blocked axis with +/-1 count jitter previously reset the speed-based timer.
    {Controller c;Feedback f[3]={{350,true,25},{4850,true,25},{4044,true,25}};
     for(int t=0;t<45000&&c.fault==Fault::None;++t){
       f[2].raw=4044+(t%2 ? 1 : -1);c.step(f,.001f);
     }
     assert(c.fault==Fault::Stalled&&c.fault_axis==3&&c.fault_measurement<.6f);
     for(auto &axis:c.axis)assert(axis.command==0);}
    // Direct outer-loop saturation and unsaturated position error, with no trajectory state.
    {Controller c;Feedback f[3]={{350,true,25},{4850,true,25},{5140,true,25}};
     initialize(c,f);c.axis[0].target=c.axis[0].position+100;c.step(f,.001f);
     assert(std::fabs(c.axis[0].pid.lastResult().error-54)<.01f);
     c.axis[0].target=c.axis[0].position+.25f;c.step(f,.001f);
     assert(std::fabs(c.axis[0].pid.lastResult().error-1)<.01f);}
    if (guards_only) {std::puts("PASS: direct cascade targets and safety guards; plant simulation skipped");return 0;}
    // Illustrative plant: voltage-to-speed response plus gravity load. This
    // checks the full state-machine trajectory, not actual hardware tuning.
    {Controller c;Feedback f[3]={{381,true,25},{4850,true,25},{4044,true,25}};
     float p[3]={381*DegreesPerCount,4850*DegreesPerCount,4044*DegreesPerCount};
     float v[3]={};bool swept=false,returned=false,swept2=false,returned2=false,lower3=false,upper3=false,returned3=false;float peak=0;
     for(int tick=0;tick<1200000;++tick){
        for(unsigned i=0;i<3;++i){
            const float gravity=i==2 ? 3500.f : 0.f;
            const float desired=.064f*((i==0 ? -1.f : 1.f)*c.axis[i].command-gravity);
            v[i]+=(desired-v[i])*.001f/.04f;
            p[i]+=v[i]*.001f;
            if(i==2&&p[i]<4044*DegreesPerCount){p[i]=4044*DegreesPerCount;v[i]=0;}
            const int raw=int(std::lround(p[i]/DegreesPerCount));f[i].raw=(raw%8192+8192)%8192;
        }
        c.step(f,.001f);
        if(c.phase==Phase::Fault){std::printf("plant fault %u tick %d speed %.2f/%.2f/%.2f\n",unsigned(c.fault),tick,c.axis[0].speed,c.axis[1].speed,c.axis[2].speed);break;}
        swept|=c.phase==Phase::Sweep1;returned|=c.phase==Phase::Return1;
        swept2|=c.phase==Phase::Sweep2;returned2|=c.phase==Phase::Return2;
        lower3|=c.phase==Phase::Lower3;upper3|=c.phase==Phase::Upper3;returned3|=c.phase==Phase::Return3;
        if(c.phase==Phase::Dwell)assert(c.axis[0].position-c.axis[0].home>=360);
        if(c.phase==Phase::Dwell2)assert(c.axis[1].position-c.axis[1].home>=360);
        if(c.phase==Phase::DwellLower3)assert(std::fabs(c.axis[2].position-LowerTarget)<.36f);
        if(c.phase==Phase::DwellUpper3)assert(std::fabs(c.axis[2].position-UpperTarget)<.36f);
        for(auto &a:c.axis){assert(std::abs(a.command)<=MaxVoltage);peak=std::max(peak,std::fabs(a.speed));}
        if(c.phase==Phase::Hold){for(unsigned i=0;i<3;++i)assert(std::fabs(c.axis[i].position-c.axis[i].home)<.36f);break;}
     }
     std::printf("phase %u position %.3f/%.3f/%.3f home %.3f/%.3f/%.3f speed %.3f/%.3f/%.3f\n",unsigned(c.phase),c.axis[0].position,c.axis[1].position,c.axis[2].position,c.axis[0].home,c.axis[1].home,c.axis[2].home,c.axis[0].speed,c.axis[1].speed,c.axis[2].speed);
     assert(c.fault==Fault::None&&c.phase==Phase::Hold&&swept&&returned&&swept2&&returned2&&lower3&&upper3&&returned3);
     std::printf("PASS: simulated homing, both 361deg sweeps, wrapped ID3 end-to-end, all return/hold; peak %.2f deg/s\n",peak);
    }
    std::puts("PASS: wrapped soft limits, boot-only recovery, latched faults and bounded commands");
}
