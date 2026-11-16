// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/metascript/adhd/ast/builtin_type_semantics.h>

#include "simd_vector_source_analysis.h"
#include "simd_codegen_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 SimdVectorSourceAnalysis::TryGetStorageVectorLoadInfo(const Expr& srcExpr, const b8 targetIsDouble, MStringView& outLoadFn, u32& outLanes)noexcept{
    if(!srcExpr.resolvedSimdType.empty() && SymbolTable::IsSimdStorageVectorType(srcExpr.resolvedSimdType, outLanes)){
        const BuiltinSimdVectorInfo* vecInfo = FindBuiltinSimdVectorType(srcExpr.resolvedSimdType);
        if(!vecInfo || vecInfo->isDoubleReg != targetIsDouble)
            return false;
        if(vecInfo->isMixed){
            outLoadFn = vecInfo->isDoubleReg ? CodegenSymbols::s_SimdLoadDoubleInt : CodegenSymbols::s_SimdLoadFloatInt;
            outLanes = 3u;
            return true;
        }
        outLoadFn = vecInfo->loadCppFunc;
        return !outLoadFn.empty();
    }
    if(!srcExpr.resolvedUnalignedType.empty()){
        const BuiltinUnalignedVectorInfo* unalignedInfo = FindBuiltinUnalignedVectorType(srcExpr.resolvedUnalignedType);
        if(!unalignedInfo)
            return false;
        outLanes = unalignedInfo->lanes;
        if(unalignedInfo->isHalfFloat){
            outLoadFn = targetIsDouble ? CodegenSymbols::s_SimdLoadDouble : CodegenSymbols::s_SimdLoadHalf;
            return true;
        }
        const BuiltinScalarTypeInfo* laneInfo = BuiltinTypeSemantics::FindBuiltinScalarType(unalignedInfo->laneScalarType);
        const b8 srcIsDouble = laneInfo && laneInfo->byteSize == 8u;
        if(srcIsDouble != targetIsDouble)
            return false;
        const b8 srcIsInt = laneInfo && laneInfo->isInteger;
        outLoadFn = CodegenSymbols::SimdLoadFunc(srcIsDouble, srcIsInt);
        return true;
    }
    return false;
}

[[nodiscard]] b8 SimdVectorSourceAnalysis::TryGetFloatVectorSourceInfo(const Expr& srcExpr, const b8 targetIsDouble, MStringView& outRegSimdType, MStringView& outStorageLoadFn, u32& outEffectiveZeroedAboveLane)noexcept{
    outRegSimdType = {};
    outStorageLoadFn = {};
    outEffectiveZeroedAboveLane = 4u;
    const MStringView expectedRegType = FindFloatSimdRegisterTypeName(targetIsDouble);
    if(srcExpr.resolvedSimdType == expectedRegType){
        outRegSimdType = expectedRegType;
        return true;
    }
    u32 storageLanes = 0u;
    if(!srcExpr.resolvedSimdType.empty() && SymbolTable::IsSimdStorageVectorType(srcExpr.resolvedSimdType, storageLanes)){
        const BuiltinSimdVectorInfo* vecInfo = FindBuiltinSimdVectorType(srcExpr.resolvedSimdType);
        if(!vecInfo || !vecInfo->isFloatReg || vecInfo->isIntReg || vecInfo->isMixed || vecInfo->isDoubleReg != targetIsDouble || vecInfo->loadCppFunc.empty())
            return false;
        outStorageLoadFn = vecInfo->loadCppFunc;
        outEffectiveZeroedAboveLane = storageLanes;
        return true;
    }
    if(!srcExpr.resolvedUnalignedType.empty()){
        const BuiltinUnalignedVectorInfo* unalignedInfo = FindBuiltinUnalignedVectorType(srcExpr.resolvedUnalignedType);
        if(!unalignedInfo || unalignedInfo->isHalfFloat || BuiltinTypeSemantics::IsIntegerScalarType(unalignedInfo->laneScalarType))
            return false;
        const b8 srcIsDouble = (unalignedInfo->laneScalarType == BuiltinTypeSemantics::Float64ScalarTypeName());
        if(srcIsDouble != targetIsDouble)
            return false;
        outStorageLoadFn = CodegenSymbols::SimdLoadFunc(srcIsDouble, false);
        outEffectiveZeroedAboveLane = unalignedInfo->lanes;
        return true;
    }
    return false;
}

[[nodiscard]] b8 SimdVectorSourceAnalysis::TryGetAnyFloatVectorSourceInfo(const Expr& srcExpr, b8& outIsDouble, MStringView& outRegSimdType, MStringView& outStorageLoadFn, u32& outEffectiveZeroedAboveLane)noexcept{
    if(SimdVectorSourceAnalysis::TryGetFloatVectorSourceInfo(srcExpr, false, outRegSimdType, outStorageLoadFn, outEffectiveZeroedAboveLane)){
        outIsDouble = false;
        return true;
    }
    if(SimdVectorSourceAnalysis::TryGetFloatVectorSourceInfo(srcExpr, true, outRegSimdType, outStorageLoadFn, outEffectiveZeroedAboveLane)){
        outIsDouble = true;
        return true;
    }
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

