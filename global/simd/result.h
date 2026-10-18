// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../expected.h"
#include "type.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Explicit record alignment keeps SIMD values aligned inside Expected.
struct alignas(SIMDVectorSingle) SIMDVectorSingleResult{
    SIMDVectorSingle value;
};

struct alignas(SIMDVectorDouble) SIMDVectorDoubleResult{
    SIMDVectorDouble value;
};

static_assert(alignof(Expected<SIMDVectorSingleResult>) >= alignof(SIMDVectorSingle));
static_assert(alignof(Expected<SIMDVectorDoubleResult>) >= alignof(SIMDVectorDouble));
static_assert(alignof(Expected<SIMDMatrix>) >= alignof(SIMDMatrix));
static_assert(alignof(Expected<SIMDMatrixDouble>) >= alignof(SIMDMatrixDouble));


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

