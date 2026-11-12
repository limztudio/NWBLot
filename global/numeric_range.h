// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "expected.h"
#include "simplemath.h"
#include "simd/vector_arithmetic.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline f64 StepF64Bounded(const f64 value, const f64 minimum, const f64 maximum, const f64 step, const b8 increasing)noexcept{
    const f64 current = Clamp(value, minimum, maximum);
    if(increasing){
        const f64 distance = VectorDoubleGetX(VectorDoubleSubtract(VectorDoubleReplicate(maximum), VectorDoubleReplicate(current)));
        if(step >= distance)
            return maximum;
        return Clamp(VectorDoubleGetX(VectorDoubleAdd(VectorDoubleReplicate(current), VectorDoubleReplicate(step))), minimum, maximum);
    }
    const f64 distance = VectorDoubleGetX(VectorDoubleSubtract(VectorDoubleReplicate(current), VectorDoubleReplicate(minimum)));
    if(step >= distance)
        return minimum;
    return Clamp(VectorDoubleGetX(VectorDoubleSubtract(VectorDoubleReplicate(current), VectorDoubleReplicate(step))), minimum, maximum);
}

[[nodiscard]] inline Expected<f64> NormalizeFiniteRange(const f64 minimum, const f64 maximum, const f64 value)noexcept{
    if((!IsFinite(minimum) || !IsFinite(maximum) || minimum > maximum) || !IsFinite(value))
        return MakeUnexpected(Failure{});
    f64 normalizedValue = 0.0;
    if(minimum != maximum && value > minimum){
        if(value >= maximum)
            normalizedValue = 1.0;
        else{
            const f64 span = VectorDoubleGetX(VectorDoubleSubtract(VectorDoubleReplicate(maximum), VectorDoubleReplicate(minimum)));
            if(IsFinite(span))
                normalizedValue = VectorDoubleGetX(VectorDoubleDivide(VectorDoubleSubtract(VectorDoubleReplicate(value), VectorDoubleReplicate(minimum)), VectorDoubleReplicate(span)));
            else{
                const SIMDVectorDouble scaled = VectorDoubleMultiply(VectorDoubleSet(value, minimum, maximum, value), VectorDoubleReplicate(0.5));
                const f64 scaledValue = VectorDoubleGetX(scaled);
                const f64 scaledMin = VectorDoubleGetY(scaled);
                const f64 scaledMax = VectorDoubleGetZ(scaled);
                const SIMDVectorDouble differences = VectorDoubleSubtract(VectorDoubleSet(scaledValue, scaledMax, scaledValue, scaledMax), VectorDoubleReplicate(scaledMin));
                normalizedValue = VectorDoubleGetX(VectorDoubleDivide(VectorDoubleSplatX(differences), VectorDoubleSplatY(differences)));
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
            const f64 lowerWeight = VectorDoubleGetX(VectorDoubleSubtract(VectorDoubleReplicate(1.0), VectorDoubleReplicate(normalized)));
            const SIMDVectorDouble terms = VectorDoubleMultiply(VectorDoubleSet(minimum, maximum, minimum, maximum), VectorDoubleSet(lowerWeight, normalized, lowerWeight, normalized));
            value = VectorDoubleGetX(VectorDoubleAdd(VectorDoubleSplatX(terms), VectorDoubleSplatY(terms)));
        }
        else{
            const f64 span = VectorDoubleGetX(VectorDoubleSubtract(VectorDoubleReplicate(maximum), VectorDoubleReplicate(minimum)));
            value = VectorDoubleGetX(VectorDoubleAdd(VectorDoubleReplicate(minimum), VectorDoubleMultiply(VectorDoubleReplicate(span), VectorDoubleReplicate(normalized))));
        }
        value = Clamp(value, minimum, maximum);
    }
    if(!IsFinite(value))
        return MakeUnexpected(Failure{});
    return value;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

