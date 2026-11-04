// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "expected.h"
#include "simplemath.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline f64 StepF64Bounded(const f64 value, const f64 minimum, const f64 maximum, const f64 step, const b8 increasing)noexcept{
    const f64 current = Clamp(value, minimum, maximum);
    if(increasing){
        const f64 distance = maximum - current;
        if(step >= distance)
            return maximum;
        return Clamp(current + step, minimum, maximum);
    }
    const f64 distance = current - minimum;
    if(step >= distance)
        return minimum;
    return Clamp(current - step, minimum, maximum);
}

[[nodiscard]] inline Expected<f64> NormalizeFiniteRange(const f64 minimum, const f64 maximum, const f64 value)noexcept{
    if((!IsFinite(minimum) || !IsFinite(maximum) || minimum > maximum) || !IsFinite(value))
        return MakeUnexpected(Failure{});
    f64 normalizedValue = 0.0;
    if(minimum != maximum && value > minimum){
        if(value >= maximum)
            normalizedValue = 1.0;
        else{
            const f64 span = maximum - minimum;
            if(IsFinite(span))
                normalizedValue = (value - minimum) / span;
            else{
                const f64 scaledValue = value * 0.5;
                const f64 scaledMin = minimum * 0.5;
                const f64 scaledMax = maximum * 0.5;
                normalizedValue = (scaledValue - scaledMin) / (scaledMax - scaledMin);
            }
        }
    }
    if(!IsFinite(normalizedValue))
        return MakeUnexpected(Failure{});
    if(normalizedValue > 0.0)
        return Min(1.0, normalizedValue);
    return 0.0;
}

[[nodiscard]] inline Expected<f64> InterpolateFiniteRange(const f64 minimum, const f64 maximum, const f64 normalized)noexcept{
    if((!IsFinite(minimum) || !IsFinite(maximum) || minimum > maximum) || !IsFinite(normalized) || normalized < 0.0 || normalized > 1.0)
        return MakeUnexpected(Failure{});
    f64 value = minimum;
    if(normalized == 1.0)
        value = maximum;
    else if(normalized > 0.0 && minimum != maximum){
        if(minimum < 0.0 && maximum > 0.0){
            const f64 lowerWeight = 1.0 - normalized;
            value = minimum * lowerWeight + maximum * normalized;
        }
        else{
            const f64 span = maximum - minimum;
            value = minimum + span * normalized;
        }
        value = Clamp(value, minimum, maximum);
    }
    if(!IsFinite(value))
        return MakeUnexpected(Failure{});
    return value;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

