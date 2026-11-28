// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/metascript/adhd/global.h>

#include <global/array.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct SimdMaskSymbolEntry{
    u8 mask;
    MStringView singleSymbol;
    MStringView doubleSymbol;
};


class CodegenSymbols final{
public:
    inline static constexpr MStringView s_ScratchArenaVar           = "__adhd_scratch";
    inline static constexpr MStringView s_MotherArenaVar            = "__adhd_mother_arena";
    inline static constexpr MStringView s_MemberArenaVar            = "__adhd_arena";
    inline static constexpr MStringView s_AssetCodecRegPrefix       = "__adhd_asset_codec_registrar_";

    inline static constexpr MStringView s_IncludePrefix             = "#include ";

    inline static constexpr MStringView s_ForwardTemplate           = "::Forward";
    inline static constexpr MStringView s_MakeNotNullFunc           = "::MakeNotNull";
    inline static constexpr MStringView s_SqrtFunc                  = "::Sqrt(";
    inline static constexpr MStringView s_StringViewCppType         = "::AStringView";
    inline static constexpr MStringView s_NameCppType               = "::Name";
    inline static constexpr MStringView s_ByteCppType               = "::u8";
    inline static constexpr MStringView s_Float32CppType            = "::f32";
    inline static constexpr MStringView s_Float64CppType            = "::f64";

    inline static constexpr MStringView s_SimdVectorPrefixSingle    = "::Vector";
    inline static constexpr MStringView s_SimdVectorPrefixDouble    = "::VectorDouble";
    inline static constexpr MStringView s_SimdNegateSingle          = "::VectorNegate(";
    inline static constexpr MStringView s_SimdNegateDouble          = "::VectorDoubleNegate(";
    inline static constexpr MStringView s_SimdGetXSingle            = "::VectorGetX(";
    inline static constexpr MStringView s_SimdGetXDouble            = "::VectorDoubleGetX(";
    inline static constexpr MStringView s_SimdGetSinglePrefix       = "::VectorGet";
    inline static constexpr MStringView s_SimdGetDoublePrefix       = "::VectorDoubleGet";
    inline static constexpr MStringView s_SimdGetIntSinglePrefix    = "::VectorGetInt";
    inline static constexpr MStringView s_SimdGetIntDoublePrefix    = "::VectorDoubleGetInt";
    inline static constexpr MStringView s_SimdGetByIndexSuffix      = "ByIndex(";
    inline static constexpr MStringView s_SimdIsFiniteSingle        = "::VectorIsFinite(";
    inline static constexpr MStringView s_SimdIsFiniteDouble        = "::VectorDoubleIsFinite(";
    inline static constexpr MStringView s_SimdLerpSingle            = "::VectorLerp(";
    inline static constexpr MStringView s_SimdLerpDouble            = "::VectorDoubleLerp(";
    inline static constexpr MStringView s_SimdLerpVSingle           = "::VectorLerpV(";
    inline static constexpr MStringView s_SimdLerpVDouble           = "::VectorDoubleLerpV(";
    inline static constexpr MStringView s_SimdResultSingle          = "::SIMDVectorSingleResult{ ";
    inline static constexpr MStringView s_SimdResultDouble          = "::SIMDVectorDoubleResult{ ";
    inline static constexpr MStringView s_SimdSwizzleWPrefixSingle  = "::VectorSwizzle<0u, 1u, 2u, ";
    inline static constexpr MStringView s_SimdSwizzleWPrefixDouble  = "::VectorDoubleSwizzle<0u, 1u, 2u, ";
    inline static constexpr MStringView s_SimdPermuteWPrefixSingle  = "::VectorPermute<0u, 1u, 2u, ";
    inline static constexpr MStringView s_SimdPermuteWPrefixDouble  = "::VectorDoublePermute<0u, 1u, 2u, ";

    inline static constexpr MStringView s_SimdTransformSuffix       = "Transform";
    inline static constexpr MStringView s_MatrixMulSingle           = "::MatrixMultiply(";
    inline static constexpr MStringView s_MatrixMulDouble           = "::MatrixDoubleMultiply(";
    inline static constexpr MStringView s_MatrixMulTransSingle      = "::MatrixMultiplyTranspose(";
    inline static constexpr MStringView s_MatrixMulTransDouble      = "::MatrixDoubleMultiplyTranspose(";
    inline static constexpr MStringView s_MatrixTransSingle         = "::MatrixTranspose(";
    inline static constexpr MStringView s_MatrixTransDouble         = "::MatrixDoubleTranspose(";

    inline static constexpr MStringView s_SimdLoadFloat             = "::LoadFloat";
    inline static constexpr MStringView s_SimdLoadDouble            = "::LoadDouble";
    inline static constexpr MStringView s_SimdLoadInt               = "::LoadInt";
    inline static constexpr MStringView s_SimdLoadDoubleInt         = "::LoadDoubleInt";
    inline static constexpr MStringView s_SimdLoadFloatInt          = "::LoadFloatInt";
    inline static constexpr MStringView s_SimdLoadHalf              = "::LoadHalf";
    inline static constexpr MStringView s_SimdStoreFloat            = "::StoreFloat";
    inline static constexpr MStringView s_SimdStoreDouble           = "::StoreDouble";
    inline static constexpr MStringView s_SimdStoreInt              = "::StoreInt";
    inline static constexpr MStringView s_SimdStoreDoubleInt        = "::StoreDoubleInt";

    inline static constexpr Array<MStringView, 4> s_SingleSplatIntrinsics = {
        "::VectorSplatX(",
        "::VectorSplatY(",
        "::VectorSplatZ(",
        "::VectorSplatW(",
    };

    inline static constexpr Array<MStringView, 4> s_DoubleSplatIntrinsics = {
        "::VectorDoubleSplatX(",
        "::VectorDoubleSplatY(",
        "::VectorDoubleSplatZ(",
        "::VectorDoubleSplatW(",
    };

    inline static constexpr Array<SimdMaskSymbolEntry, 7> s_InPlaceLaneMasks = {{
        { 0x1u, "::s_SIMDMaskX",      "::s_DoubleSIMDMaskX" },
        { 0x2u, "::s_SIMDMaskY",      "::s_DoubleSIMDMaskY" },
        { 0x4u, "::s_SIMDMaskZ",      "::s_DoubleSIMDMaskZ" },
        { 0x8u, "::s_SIMDMaskW",      "::s_DoubleSIMDMaskW" },
        { 0x3u, "::s_SIMDMaskXY",     "::s_DoubleSIMDMaskXY" },
        { 0x7u, "::s_SIMDMask3",      "::s_DoubleSIMDMask3" },
        { 0xAu, "::s_SIMDSelect0101", "::s_DoubleSIMDSelect0101" },
    }};

    inline static constexpr Array<SimdMaskSymbolEntry, 5> s_SignFlipMasks = {{
        { 0x7u, "::s_SIMDNegate3", "::s_DoubleSIMDNegate3" },
        { 0x2u, "::s_SIMDFlipY",   "" },
        { 0x4u, "::s_SIMDFlipZ",   "" },
        { 0x8u, "::s_SIMDFlipW",   "" },
        { 0xAu, "::s_SIMDFlipYW",  "" },
    }};

    inline static constexpr Array<SimdMaskSymbolEntry, 3> s_ComponentMasks = {{
        { 0x3u, "::VectorComponentMask::s_XY",   "::VectorDoubleComponentMask::s_DoubleXY" },
        { 0x7u, "::VectorComponentMask::s_XYZ",  "::VectorDoubleComponentMask::s_DoubleXYZ" },
        { 0xFu, "::VectorComponentMask::s_XYZW", "::VectorDoubleComponentMask::s_DoubleXYZW" },
    }};

    inline static constexpr MStringView s_ScalarOpNegate   = "Negate";
    inline static constexpr MStringView s_ScalarOpAdd      = "Add";
    inline static constexpr MStringView s_ScalarOpSubtract = "Subtract";
    inline static constexpr MStringView s_ScalarOpMultiply = "Multiply";
    inline static constexpr MStringView s_ScalarOpDivide   = "Divide";
    inline static constexpr MStringView s_ScalarParamA     = "__a";
    inline static constexpr MStringView s_ScalarParamB     = "__b";
    inline static constexpr MStringView s_ScalarParamLhs   = "__lhs";
    inline static constexpr MStringView s_ScalarParamRhs   = "__rhs";
    inline static constexpr MStringView s_ScalarUnitOne    = "1";

    inline static constexpr Array<MStringView, 5> s_UnitFloatLiterals = {
        "1.0f", "1.0", "1.f", "1.0F", "1.F"
    };
    inline static constexpr Array<MStringView, 5> s_NegativeUnitFloatLiterals = {
        "-1.0f", "-1.0", "-1.f", "-1.0F", "-1.F"
    };
    inline static constexpr Array<MStringView, 6> s_HalfFloatLiterals = {
        "0.5f", "0.5", ".5f", ".5", "0.5F", ".5F"
    };

    inline static constexpr MStringView s_SimdOneSingle     = "::s_SIMDOne";
    inline static constexpr MStringView s_SimdOneDouble     = "::s_DoubleSIMDOne";
    inline static constexpr MStringView s_SimdNegOneSingle  = "::s_SIMDNegativeOne";
    inline static constexpr MStringView s_SimdNegOneDouble  = "::s_DoubleSIMDNegativeOne";
    inline static constexpr MStringView s_SimdHalfSingle    = "::s_SIMDOneHalf";
    inline static constexpr MStringView s_SimdHalfDouble    = "::s_DoubleSIMDOneHalf";


public:
    [[nodiscard]] static constexpr MStringView SimdPrefix(const b8 isDouble)noexcept{
        return isDouble ? s_SimdVectorPrefixDouble : s_SimdVectorPrefixSingle;
    }

    [[nodiscard]] static constexpr MStringView SimdScalarCppType(const b8 isDouble)noexcept{
        return isDouble ? s_Float64CppType : s_Float32CppType;
    }

    [[nodiscard]] static constexpr MStringView SimdLoadFunc(const b8 isDouble, const b8 isInt)noexcept{
        return isDouble
            ? (isInt ? s_SimdLoadDoubleInt : s_SimdLoadDouble)
            : (isInt ? s_SimdLoadInt : s_SimdLoadFloat);
    }

    [[nodiscard]] static constexpr MStringView SimdStoreFunc(const b8 isDouble, const b8 isInt)noexcept{
        return isDouble
            ? (isInt ? s_SimdStoreDoubleInt : s_SimdStoreDouble)
            : (isInt ? s_SimdStoreInt : s_SimdStoreFloat);
    }

    template<usize N>
    [[nodiscard]] static constexpr MStringView LookupMaskSymbol(const Array<SimdMaskSymbolEntry, N>& table, const u8 mask, const b8 isDouble)noexcept{
        for(const SimdMaskSymbolEntry& entry : table){
            if(entry.mask == mask)
                return isDouble ? entry.doubleSymbol : entry.singleSymbol;
        }
        return {};
    }

    template<usize N>
    [[nodiscard]] static constexpr b8 ContainsLiteralText(const Array<MStringView, N>& table, const MStringView text)noexcept{
        for(const MStringView entry : table){
            if(entry == text)
                return true;
        }
        return false;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

