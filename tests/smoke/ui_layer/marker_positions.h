// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <engine_namespace.h>

#include <global/array.h>
#include <global/math/vector_arithmetic.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Fixed marker grids have exact integer coordinates.
// Admit only grids whose casts, products and sums fit FP32's exact nonnegative integer range, so preparing independent positions cannot change rounding or floating flags before the existing ordered paint calls.
template<usize Count, u32 Origin, u32 Step>
[[nodiscard]] inline Array<f32, Count> BuildSmokeMarkerPositions()noexcept{
    constexpr u32 s_ExactIntegerLimit = 1u << 24u;
    static_assert(Count > 0u && Step > 0u);
    static_assert(Origin <= s_ExactIntegerLimit && Step <= s_ExactIntegerLimit);
    static_assert(Count - 1u <= (s_ExactIntegerLimit - Origin) / Step);

    Array<f32, Count> positions{};
    usize index = 0u;
    for(; index < Count; index += 4u){
        // Repeating the last real index pads a partial packet without reading beyond the fixed grid or adding any floating effects.
        const SIMDVectorSingle coordinates = VectorMultiplyAddExpression(
            VectorSet(static_cast<f32>(index), static_cast<f32>(index + 1u < Count ? index + 1u : Count - 1u), static_cast<f32>(index + 2u < Count ? index + 2u : Count - 1u), static_cast<f32>(index + 3u < Count ? index + 3u : Count - 1u)),
            VectorReplicate(static_cast<f32>(Step)), VectorReplicate(static_cast<f32>(Origin))
        );
        positions[index] = VectorGetX(coordinates);
        if(index + 1u < Count)
            positions[index + 1u] = VectorGetY(coordinates);
        if(index + 2u < Count)
            positions[index + 2u] = VectorGetZ(coordinates);
        if(index + 3u < Count)
            positions[index + 3u] = VectorGetW(coordinates);
    }
    return positions;
}

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

