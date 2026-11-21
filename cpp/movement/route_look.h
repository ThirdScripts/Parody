#pragma once
#include <algorithm>
#include <cmath>
#include "core/types.h"
namespace cs2n {
inline bool routeHeadingReady(double errorRadians){
 return std::isfinite(errorRadians)&&std::abs(errorRadians)<=35*kPi/180;
}
// Route steering has no retained angular momentum. Delayed pose samples must
// not make a previous turn continue after the route changes direction.
inline double routeLookCounts(double measured,double desired,double dt,double scale){
 if(!std::isfinite(measured)||!std::isfinite(desired)||!std::isfinite(dt)||
    !std::isfinite(scale)||dt<=0||dt>.1||scale<=0)return 0;
 const double error=wrapAngle(desired-measured);
 if(std::abs(error)<.15*kPi/180)return 0;
 const double delta=error*(-std::expm1(-8*dt));
 const double limit=180*kPi/180*dt;
 return std::clamp(delta,-limit,limit)/scale;
}
}
