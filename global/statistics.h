// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "simplemath.h"
#include "span.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct OnlineF64Statistics{
    f64 average = 0.0;
    f64 minimum = 0.0;
    f64 maximum = 0.0;
    u32 count = 0u;
};

// The input must contain between one and u32 maximum samples.
[[nodiscard]] inline OnlineF64Statistics ComputeOnlineF64Statistics(const Span<const f64> samples)noexcept{
    OnlineF64Statistics result;
    result.minimum = samples.front();
    result.maximum = result.minimum;
    for(const f64 sample : samples){
        ++result.count;
        result.average += (sample - result.average) / static_cast<f64>(result.count);
        result.minimum = Min(result.minimum, sample);
        result.maximum = Max(result.maximum, sample);
    }
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

