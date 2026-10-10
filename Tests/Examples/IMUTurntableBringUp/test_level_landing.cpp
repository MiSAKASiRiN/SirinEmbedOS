#include "../../../Examples/IMUTurntableBringUp/control.hpp"
#include <cassert>
#include <cstdio>
using namespace SRNOS::Examples::Turntable;
static void init(Controller &c,Feedback (&f)[3]){for(int k=0;k<2100;++k)c.step(f,.001f);}
int main(){
 Feedback f[3]={{350,true,25},{4764,true,25},{5070,true,25}};
 {Controller c;init(c,f);c.phase=Phase::Level;
  for(int k=0;k<1600;++k)c.step(f,.001f,false,{true,0,0,1});
  assert(c.phase==Phase::Hold);assert(c.axis[0].home==350*DegreesPerCount);}
 {Controller c;init(c,f);c.phase=Phase::Level;
  for(int k=0;k<1600;++k)c.step(f,.001f,false,{true,-.01f,-.01f,.9999f});
  assert(c.axis[1].target<c.axis[1].home);assert(c.axis[2].target<c.axis[2].home);}
 {Controller c;init(c,f);c.phase=Phase::Level;
  for(int k=0;k<91000;++k)c.step(f,.001f);
  assert(c.phase==Phase::LevelHold&&c.fault==Fault::None);}
 // Timer release is deliberately independent of whether the encoder moves.
 for(bool moving:{false,true}) {
  Controller c;init(c,f);c.request_park();c.step(f,.001f);
  for(int k=0;k<7900;++k){if(moving)f[2].raw=5070-k/8;c.step(f,.001f);assert(c.axis[2].command==ParkingVoltage);assert(c.phase==Phase::Landing);}
  for(int k=0;k<110;++k)c.step(f,.001f);
  assert(c.phase==Phase::Landed);for(auto &a:c.axis)assert(a.command==0);
  f[2].raw=5070;
 }
 {Controller c;init(c,f);c.phase=Phase::Hold;
  // Exit the boot exception using the actual home sequence.
  for(int k=0;k<2000;++k)c.step(f,.001f);f[2].raw=4199;c.step(f,.001f);
  assert(c.fault==Fault::Limit);}
 f[2].raw=5070;
 {Controller c;init(c,f);c.request_park();c.step(f,.001f);f[2].online=false;c.step(f,.001f);assert(c.fault==Fault::Feedback);for(auto &a:c.axis)assert(a.command==0);}
 f[2].online=true;
 puts("PASS: gravity direction, level timeout holding, fixed braking timer independent of motion, normal limit and feedback inhibit");
}
