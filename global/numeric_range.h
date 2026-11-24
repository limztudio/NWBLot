// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "expected.h"
#include "simd/vector_arithmetic.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace NumericRangeDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] FOUNDATION_INLINE SIMDVectorDouble FOUNDATION_SIMD_CALL ClampOrderedDouble(const SIMDVectorDouble value, const SIMDVectorDouble minimum, const SIMDVectorDouble maximum)noexcept{
    return VectorDoubleSelect(minimum, VectorDoubleSelect(value, maximum, VectorDoubleGreater(value, maximum)), VectorDoubleGreater(value, minimum));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline f64 StepF64Bounded(const f64 value, const f64 minimum, const f64 maximum, const f64 step, const b8 increasing)noexcept{
    const SIMDVectorDouble minVec = VectorDoubleReplicate(minimum);
    const SIMDVectorDouble maxVec = VectorDoubleReplicate(maximum);
    const SIMDVectorDouble stepVec = VectorDoubleReplicate(step);
    const SIMDVectorDouble current = NumericRangeDetail::ClampOrderedDouble(VectorDoubleReplicate(value), minVec, maxVec);
    if(increasing){
        const f64 distance = VectorDoubleGetX(VectorDoubleSubtract(maxVec, current));
        if(step >= distance)
            return maximum;
        return VectorDoubleGetX(NumericRangeDetail::ClampOrderedDouble(VectorDoubleAdd(current, stepVec), minVec, maxVec));
    }
    const f64 distance = VectorDoubleGetX(VectorDoubleSubtract(current, minVec));
    if(step >= distance)
        return minimum;
    return VectorDoubleGetX(NumericRangeDetail::ClampOrderedDouble(VectorDoubleSubtract(current, stepVec), minVec, maxVec));
}

[[nodiscard]] inline Expected<f64> NormalizeFiniteRange(const f64 minimum, const f64 maximum, const f64 value)noexcept{
    const SIMDVectorDouble rangeVec = VectorDoubleSet(minimum, maximum, value, 0.0);
    if(!VectorDoubleIsFinite(rangeVec, VectorDoubleComponentMask::s_DoubleXYZ) || minimum > maximum)
        return MakeUnexpected(Failure{});
    f64 normalizedValue = 0.0;
    if(minimum != maximum && value > minimum){
        if(value >= maximum)
            normalizedValue = 1.0;
        else{
            const f64 span = VectorDoubleGetX(VectorDoubleSubtract(VectorDoubleReplicate(maximum), VectorDoubleReplicate(minimum)));
            if(VectorDoubleIsFinite(VectorDoubleReplicate(span), VectorDoubleComponentMask::s_DoubleXYZW))
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
    const SIMDVectorDouble normalizedVec = VectorDoubleReplicate(normalizedValue);
    if(!VectorDoubleIsFinite(normalizedVec, VectorDoubleComponentMask::s_DoubleXYZW))
        return MakeUnexpected(Failure{});
    if(normalizedValue > 0.0){
        const SIMDVectorDouble oneVec = VectorDoubleSplatOne();
        return VectorDoubleGetX(VectorDoubleSelect(normalizedVec, oneVec, VectorDoubleLess(oneVec, normalizedVec)));
    }
    return 0.0;
}

[[nodiscard]] inline Expected<f64> InterpolateFiniteRange(const f64 minimum, const f64 maximum, const f64 normalized)noexcept{
    const SIMDVectorDouble inputs = VectorDoubleSet(minimum, maximum, normalized, 0.0);
    if(!VectorDoubleIsFinite(inputs, VectorDoubleComponentMask::s_DoubleXYZ) || minimum > maximum || normalized < 0.0 || normalized > 1.0)
        return MakeUnexpected(Failure{});
    f64 value = minimum;
    if(normalized == 1.0)
        value = maximum;
    else if(normalized > 0.0 && minimum != maximum){
        SIMDVectorDouble valueVec{};
        if(minimum < 0.0 && maximum > 0.0){
            const f64 lowerWeight = VectorDoubleGetX(VectorDoubleSubtract(VectorDoubleSplatOne(), VectorDoubleReplicate(normalized)));
            const SIMDVectorDouble terms = VectorDoubleMultiply(VectorDoubleSet(minimum, maximum, minimum, maximum), VectorDoubleSet(lowerWeight, normalized, lowerWeight, normalized));
            valueVec = VectorDoubleAdd(VectorDoubleSplatX(terms), VectorDoubleSplatY(terms));
        }
        else{
            const f64 span = VectorDoubleGetX(VectorDoubleSubtract(VectorDoubleReplicate(maximum), VectorDoubleReplicate(minimum)));
            valueVec = VectorDoubleAdd(VectorDoubleReplicate(minimum), VectorDoubleMultiply(VectorDoubleReplicate(span), VectorDoubleReplicate(normalized)));
        }
        value = VectorDoubleGetX(NumericRangeDetail::ClampOrderedDouble(valueVec, VectorDoubleReplicate(minimum), VectorDoubleReplicate(maximum)));
    }
    if(!VectorDoubleIsFinite(VectorDoubleReplicate(value), VectorDoubleComponentMask::s_DoubleXYZW))
        return MakeUnexpected(Failure{});
    return value;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

