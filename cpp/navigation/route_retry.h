#pragma once
#include <algorithm>
namespace cs2n {
// Fast transient retry, bounded exponential backoff for a genuinely blocked pose.
inline double routeRetryDelay(unsigned failures){
 return std::min(2.0,.35*double(1u<<std::min(failures>0?failures-1:0u,3u)));
}
}
