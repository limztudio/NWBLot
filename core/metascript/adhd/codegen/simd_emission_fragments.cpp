// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "simd_emission_fragments.h"
#include "simd_codegen_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


void SimdEmissionFragments::emitSimdMatrixBinaryRowInitSuffix(MString& out, const MStringView matRegType, const MStringView fnPrefix){
    out.append("; return ");
    out.append(matRegType.data(), matRegType.size());
    out.append("{ .v = { ");
    for(u32 i = 0u; i < 4u; ++i){
        if(i > 0u)
            out.append(", ");
        out.append(fnPrefix.data(), fnPrefix.size());
        out.append("__a.v[");
        AppendUnsignedDecimal(out, i);
        out.append("], __b.v[");
        AppendUnsignedDecimal(out, i);
        out.append("])");
    }
    out.append(" } }; }())");
}

void SimdEmissionFragments::emitSimdMatrixUnaryRowInitSuffix(MString& out, const MStringView matRegType, const MStringView fnPrefix, const MStringView fnSuffix){
    out.append("; return ");
    out.append(matRegType.data(), matRegType.size());
    out.append("{ .v = { ");
    for(u32 i = 0u; i < 4u; ++i){
        if(i > 0u)
            out.append(", ");
        out.append(fnPrefix.data(), fnPrefix.size());
        out.append("__m.v[");
        AppendUnsignedDecimal(out, i);
        out.push_back(']');
        out.append(fnSuffix.data(), fnSuffix.size());
    }
    out.append(" } }; }())");
}

void SimdEmissionFragments::emitSimdIntTwosComplementNegatePrefix(MString& out, const b8 isDouble){
    out.append(isDouble ? "::VectorDoubleAddInt(::VectorDoubleXorInt(" : "::VectorAddInt(::VectorXorInt(");
}

void SimdEmissionFragments::emitSimdIntTwosComplementNegateSuffix(MString& out, const b8 isDouble){
    out.append(isDouble ? ", ::VectorDoubleTrueInt()), ::VectorDoubleReplicateInt(1ull))" : ", ::VectorTrueInt()), ::VectorReplicateInt(1u))");
}




NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
