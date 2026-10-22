// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <global/simd/convert.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(MathDoubleConstant, ComponentConstructionPreservesSubFloatPrecisionAndRawExceptionalBits){
    constexpr u64 s_ComponentBits[]{ 0x3FF0000000000001ull, 0x8000000000000000ull, 0x7FF8123456789ABCull, 0xBFF0000000000001ull };
    const SIMDVectorDoubleConstF constant{ { { BitCast<f64>(s_ComponentBits[0]), BitCast<f64>(s_ComponentBits[1]), BitCast<f64>(s_ComponentBits[2]), BitCast<f64>(s_ComponentBits[3]) } } };
    // Component construction must retain binary64 values rather than narrow through the existing Single wrapper.
    const Double4 fromComponents(constant.f);
    const SIMDVectorDouble native = constant;
    Double4 fromNative{};
    StoreDouble(native, fromNative);

    for(usize component = 0u; component < 4u; ++component){
        EXPECT_EQ(BitCast<u64>(fromComponents.raw[component]), s_ComponentBits[component]) << component;
        EXPECT_EQ(BitCast<u64>(fromNative.raw[component]), s_ComponentBits[component]) << component;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

