// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "span.h"
#include "simd/vector_arithmetic.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct OnlineF64Statistics{
    f64 average = 0.0;
    f64 minimum = 0.0;
    f64 maximum = 0.0;
    u32 count = 0u;
};

[[nodiscard]] inline OnlineF64Statistics ComputeOnlineF64Statistics(const Span<const f64> samples)noexcept{
    OnlineF64Statistics result;
    SIMDVectorDouble average = VectorDoubleZero();
    SIMDVectorDouble minimum = VectorDoubleReplicate(samples.front());
    SIMDVectorDouble maximum = minimum;
    for(const f64 sample : samples){
        ++result.count;
        const SIMDVectorDouble sampleVec = VectorDoubleReplicate(sample);
        const SIMDVectorDouble change = VectorDoubleDivide(VectorDoubleSubtract(sampleVec, average), VectorDoubleReplicate(static_cast<f64>(result.count)));
        average = VectorDoubleAdd(average, change);
        minimum = VectorDoubleSelect(sampleVec, minimum, VectorDoubleLess(minimum, sampleVec));
        maximum = VectorDoubleSelect(sampleVec, maximum, VectorDoubleGreater(maximum, sampleVec));
    }
    result.average = VectorDoubleGetX(average);
    result.minimum = VectorDoubleGetX(minimum);
    result.maximum = VectorDoubleGetX(maximum);
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

