// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "reflection_emitter.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_reflection_emitter_nwb{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] constexpr b8 IsDeserializableClass(const ClassDecl* cls)noexcept{
    return cls && (cls->isSerialize || cls->isReflect || !cls->assetTypeName.empty() || !cls->templateParams.empty());
}

[[nodiscard]] u32 SimdMatrixElementCount(const MStringView matTypeName)noexcept{
    if(const BuiltinSimdMatrixInfo* matInfo = FindBuiltinSimdMatrixType(matTypeName))
        return matInfo->elemCount;
    constexpr u32 s_DefaultSimdMatrixElementCount = 16u;
    return s_DefaultSimdMatrixElementCount;
}

void EmitNwbFieldFindPrefix(MString& out, const MStringView fieldName){
    out.append("        if(const auto* __f = inMap.findField(\"");
    out.append(fieldName.data(), fieldName.size());
    out.append("\"); __f && ");
}

void EmitNwbListSizeContinueGuard(MString& out, const MStringView elemAccessor, const u32 expectedCount){
    out.append("if(!");
    out.append(elemAccessor.data(), elemAccessor.size());
    out.append(".isList() || ");
    out.append(elemAccessor.data(), elemAccessor.size());
    out.append(".asList().size() != ");
    AppendUnsignedDecimal(out, expectedCount);
    out.append("u) continue; ");
}

[[nodiscard]] b8 IsScalarOrStringLikeNwbElement(const MStringView typeName)noexcept{
    return IsScalarType(typeName) || FindBuiltinNwbValueType(typeName) != nullptr;
}

[[nodiscard]] b8 RequiresNwbNameAdmission(const MStringView typeName)noexcept{
    const BuiltinNwbValueTypeInfo* nwbVal = FindBuiltinNwbValueType(typeName);
    return nwbVal && nwbVal->requiresNameAdmission;
}

void EmitNwbValueConversionExpr(MString& out, const MStringView typeName, const MStringView valueAccessor, const TypeRef& typeRef, const SymbolTable& symbols){
    if(const BuiltinNwbValueTypeInfo* nwbVal = FindBuiltinNwbValueType(typeName)){
        if(nwbVal->requiresNameAdmission){
            if(IsAssetHandleType(typeName)){
                const CppTypeEmitter typeEmitter(symbols);
                typeEmitter.emitTypeCpp(out, typeRef);
                out.append("(*__name)");
            }
            else{
                out.append("*__name");
            }
        }
        else{
            out.append(nwbVal->valuePrefix.data(), nwbVal->valuePrefix.size());
            out.append(valueAccessor.data(), valueAccessor.size());
            out.append(nwbVal->nwbAccessorMethod.data(), nwbVal->nwbAccessorMethod.size());
            out.append(nwbVal->valueSuffix.data(), nwbVal->valueSuffix.size());
        }
    }
    else if(IsIntegerScalarType(typeName) || IsFloatScalarType(typeName)){
        out.append("static_cast<");
        out.append(typeName.data(), typeName.size());
        out.append(">(");
        out.append(valueAccessor.data(), valueAccessor.size());
        out.append(IsIntegerScalarType(typeName) ? ".asInteger())" : ".toDouble())");
    }
}

void EmitNwbValueAdmissionGuard(MString& out, const MStringView typeName, const MStringView valueAccessor){
    const BuiltinNwbValueTypeInfo* nwbVal = FindBuiltinNwbValueType(typeName);
    out.append("if(!");
    out.append(valueAccessor.data(), valueAccessor.size());
    if(nwbVal)
        out.append(nwbVal->nwbCheckMethod.data(), nwbVal->nwbCheckMethod.size());
    else if(IsIntegerScalarType(typeName))
        out.append(".isInteger()");
    else if(IsFloatScalarType(typeName))
        out.append(".isNumeric()");
    else
        out.append(".isString()");
    out.append(") continue; ");
    if(nwbVal && nwbVal->requiresNameAdmission){
        out.append("const auto __name = ::Name::FromText(");
        out.append(valueAccessor.data(), valueAccessor.size());
        out.append(nwbVal->nwbAccessorMethod.data(), nwbVal->nwbAccessorMethod.size());
        out.append("); if(!__name) continue; ");
    }
}

void EmitNwbSimdVectorCtorExpr(MString& out, const MStringView vecTypeName, const u32 simdLanes, const MStringView listAccessor){
    const MStringView cppVecType = CppTypeEmitter::MapBuiltinCppType(vecTypeName);
    const BuiltinSimdVectorInfo* vecInfo = FindBuiltinSimdVectorType(vecTypeName);
    const BuiltinUnalignedVectorInfo* unalignedInfo = vecInfo ? nullptr : FindBuiltinUnalignedVectorType(vecTypeName);
    const MStringView laneScalar = vecInfo ? vecInfo->xyzScalarType : (unalignedInfo ? unalignedInfo->laneScalarType : vecTypeName.substr(0u, 3u));
    const b8 isMixedVec = vecInfo && vecInfo->isMixed;
    const MStringView wLaneScalar = isMixedVec ? vecInfo->wScalarType : laneScalar;
    out.append(cppVecType.data(), cppVecType.size());
    out.push_back('(');
    for(u32 lane = 0u; lane < simdLanes; ++lane){
        if(lane > 0u)
            out.append(", ");
        if(unalignedInfo && unalignedInfo->isHalfFloat){
            out.append("::ConvertFloatToHalf(static_cast<f32>(");
            out.append(listAccessor.data(), listAccessor.size());
            out.push_back('[');
            AppendUnsignedDecimal(out, lane);
            out.append("].toDouble()))");
            continue;
        }
        const MStringView curScalar = (isMixedVec && lane == 3u) ? wLaneScalar : laneScalar;
        const b8 laneIsInt = IsIntegerScalarType(curScalar);
        out.append("static_cast<");
        out.append(curScalar.data(), curScalar.size());
        out.append(">(");
        out.append(listAccessor.data(), listAccessor.size());
        out.push_back('[');
        AppendUnsignedDecimal(out, lane);
        out.append(laneIsInt ? "].asInteger())" : "].toDouble())");
    }
    out.push_back(')');
}

void EmitNwbMapKeyExpr(MString& out, const MStringView keyName, const b8 isEnumKey = false){
    if(isEnumKey){
        out.append("*__ek");
    }
    else if(RequiresNwbNameAdmission(keyName)){
        out.append("*__key");
    }
    else if(const BuiltinNwbValueTypeInfo* nwbVal = FindBuiltinNwbValueType(keyName)){
        out.append(nwbVal->valuePrefix.data(), nwbVal->valuePrefix.size());
        out.append("__k");
        out.append(nwbVal->valueSuffix.data(), nwbVal->valueSuffix.size());
    }
}

void EmitNwbEnumFieldDeserialize(MString& out, const MStringView fieldName, const MStringView enumName, const b8 isFlag){
    if(!isFlag){
        EmitNwbFieldFindPrefix(out, fieldName);
        out.append("__f->isString()){ if(const auto __e = ");
        out.append(enumName.data(), enumName.size());
        out.append("::parseNamedEnum(__f->asString())) this->");
        out.append(fieldName.data(), fieldName.size());
        out.append(" = *__e; }\r\n");
        return;
    }
    out.append("        if(const auto* __f = inMap.findField(\"");
    out.append(fieldName.data(), fieldName.size());
    out.append("\"); __f){ if(__f->isString()){ if(const auto __e = ");
    out.append(enumName.data(), enumName.size());
    out.append("::parseNamedEnum(__f->asString())) this->");
    out.append(fieldName.data(), fieldName.size());
    out.append(" = *__e; } else if(__f->isList()){ ");
    out.append(enumName.data(), enumName.size());
    out.append("::Enum __flags = static_cast<");
    out.append(enumName.data(), enumName.size());
    out.append("::Enum>(0); for(const auto& __item : __f->asList()){ if(__item.isString()){ if(const auto __e = ");
    out.append(enumName.data(), enumName.size());
    out.append("::parseNamedEnum(__item.asString())) __flags |= *__e; } } this->");
    out.append(fieldName.data(), fieldName.size());
    out.append(" = __flags; } }\r\n");
}

void EmitNwbSimdMatrixElementLoop(MString& out, const MStringView targetPrefix, const MStringView targetName, const MStringView matTypeName, const MStringView listAccessor, const MStringView loopVar, const u32 elemCount){
    const BuiltinSimdMatrixInfo* matInfo = FindBuiltinSimdMatrixType(matTypeName);
    const MStringView elemScalar = matInfo ? matInfo->elemScalarType : matTypeName.substr(0u, 3u);
    out.append("for(usize ");
    out.append(loopVar.data(), loopVar.size());
    out.append(" = 0u; ");
    out.append(loopVar.data(), loopVar.size());
    out.append(" < ");
    AppendUnsignedDecimal(out, elemCount);
    out.append("u; ++");
    out.append(loopVar.data(), loopVar.size());
    out.append(") ");
    out.append(targetPrefix.data(), targetPrefix.size());
    out.append(targetName.data(), targetName.size());
    out.append(".raw[");
    out.append(loopVar.data(), loopVar.size());
    out.append("] = static_cast<");
    out.append(elemScalar.data(), elemScalar.size());
    out.append(">(");
    out.append(listAccessor.data(), listAccessor.size());
    out.push_back('[');
    out.append(loopVar.data(), loopVar.size());
    out.append("].toDouble());");
}

void EmitNwbSimdMatrixLocalInit(MString& out, const MStringView matTypeName, const MStringView listAccessor, const MStringView loopVar){
    const MStringView cppMatType = CppTypeEmitter::MapBuiltinCppType(matTypeName);
    const u32 elemCount = SimdMatrixElementCount(matTypeName);
    out.append(cppMatType.data(), cppMatType.size());
    out.append(" __m{}; ");
    EmitNwbSimdMatrixElementLoop(out, "", "__m", matTypeName, listAccessor, loopVar, elemCount);
    out.push_back(' ');
}

[[nodiscard]] b8 TryEmitNwbSharedScalarOrVectorFieldDeserialize(MString& out, const MStringView fieldName, const MStringView typeName, const SymbolTable& symbols, const b8 isOptional = false){
    if(const BuiltinNwbValueTypeInfo* nwbVal = FindBuiltinNwbValueType(typeName)){
        EmitNwbFieldFindPrefix(out, fieldName);
        const MStringView prefix = isOptional ? nwbVal->optionalFieldPrefix : nwbVal->directFieldPrefix;
        const MStringView suffix = isOptional ? nwbVal->optionalFieldSuffix : nwbVal->directFieldSuffix;
        out.append(prefix.data(), prefix.size());
        out.append(fieldName.data(), fieldName.size());
        out.append(suffix.data(), suffix.size());
        return true;
    }
    if(IsIntegerScalarType(typeName)){
        EmitNwbFieldFindPrefix(out, fieldName);
        out.append("__f->isInteger()) this->");
        out.append(fieldName.data(), fieldName.size());
        out.append(" = static_cast<");
        out.append(typeName.data(), typeName.size());
        out.append(">(__f->asInteger());\r\n");
        return true;
    }
    if(IsFloatScalarType(typeName)){
        EmitNwbFieldFindPrefix(out, fieldName);
        out.append("__f->isNumeric()) this->");
        out.append(fieldName.data(), fieldName.size());
        out.append(" = static_cast<");
        out.append(typeName.data(), typeName.size());
        out.append(">(__f->toDouble());\r\n");
        return true;
    }
    if(const EnumDecl* ed = symbols.findEnum(typeName)){
        EmitNwbEnumFieldDeserialize(out, fieldName, typeName, ed->isFlag);
        return true;
    }
    u32 simdLanes = 0u;
    if((SymbolTable::IsSimdVectorType(typeName, simdLanes) || SymbolTable::IsUnalignedVectorType(typeName, simdLanes)) && simdLanes >= 2u && simdLanes <= 4u){
        EmitNwbFieldFindPrefix(out, fieldName);
        out.append("__f->isList() && __f->asList().size() == ");
        AppendUnsignedDecimal(out, simdLanes);
        out.append("u) this->");
        out.append(fieldName.data(), fieldName.size());
        out.append(" = ");
        EmitNwbSimdVectorCtorExpr(out, typeName, simdLanes, "__f->asList()");
        out.append(";\r\n");
        return true;
    }
    return false;
}

void EmitNwbListContainerLoopHeader(MString& out, const MStringView fieldName, const b8 isForwardList, const b8 isFixedVec, const b8 isDynVec){
    EmitNwbFieldFindPrefix(out, fieldName);
    out.append("__f->isList()){ ");
    if(isForwardList || isFixedVec || isDynVec){
        out.append("this->");
        out.append(fieldName.data(), fieldName.size());
        out.append(".clear(); ");
    }
    if(isForwardList){
        out.append("for(usize __i = __f->asList().size(); __i > 0u; --__i)");
        return;
    }
    out.append("for(usize __i = 0u; __i < __f->asList().size()");
    if(!isDynVec){
        if(isFixedVec){
            out.append(" && this->");
            out.append(fieldName.data(), fieldName.size());
            out.append(".size() < this->");
            out.append(fieldName.data(), fieldName.size());
            out.append(".max_size()");
        }
        else{
            out.append(" && __i < this->");
            out.append(fieldName.data(), fieldName.size());
            out.append(".size()");
        }
    }
    out.append("; ++__i)");
}

void EmitNwbListContainerFieldDeserialize(MString& out, const MStringView fieldName, const MStringView containerTypeName, const MStringView elemName, const TypeRef& elemType, const SymbolTable& symbols){
    const BuiltinGenericTypeInfo* genericInfo = FindBuiltinGenericType(containerTypeName);
    const ReflectedLinearKind::Enum linearKind = genericInfo ? genericInfo->reflectedLinearKind : ReflectedLinearKind::None;
    const b8 isSetContainer = (linearKind == ReflectedLinearKind::Set);
    const b8 isForwardList = (linearKind == ReflectedLinearKind::ForwardList);
    const b8 isFixedVec = (linearKind == ReflectedLinearKind::FixedVector);
    const b8 isDynVec = (linearKind == ReflectedLinearKind::DynamicBack || isSetContainer);
    const b8 isPushContainer = isForwardList || isFixedVec || isDynVec;
    const MStringView elemAccessor = isForwardList ? "__f->asList()[__i - 1u]" : "__f->asList()[__i]";
    const MStringView elemListAccessor = isForwardList ? "__f->asList()[__i - 1u].asList()" : "__f->asList()[__i].asList()";
    const MStringView assignOrPushPrefix = isSetContainer ? ".insert(" : (isForwardList ? ".push_front(" : (isPushContainer ? ".push_back(" : "[__i] = "));

    if(symbols.findEnum(elemName)){
        EmitNwbListContainerLoopHeader(out, fieldName, isForwardList, isFixedVec, isDynVec);
        out.append("{ if(");
        out.append(elemAccessor.data(), elemAccessor.size());
        out.append(".isString()){ if(const auto __e = ");
        out.append(elemName.data(), elemName.size());
        out.append("::parseNamedEnum(");
        out.append(elemAccessor.data(), elemAccessor.size());
        out.append(".asString())) this->");
        out.append(fieldName.data(), fieldName.size());
        out.append(assignOrPushPrefix.data(), assignOrPushPrefix.size());
        out.append("*__e");
        if(isPushContainer)
            out.push_back(')');
        out.append("; } } }\r\n");
    }
    else if(IsScalarOrStringLikeNwbElement(elemName)){
        EmitNwbListContainerLoopHeader(out, fieldName, isForwardList, isFixedVec, isDynVec);
        out.append("{ ");
        EmitNwbValueAdmissionGuard(out, elemName, elemAccessor);
        out.append("this->");
        out.append(fieldName.data(), fieldName.size());
        out.append(assignOrPushPrefix.data(), assignOrPushPrefix.size());
        EmitNwbValueConversionExpr(out, elemName, elemAccessor, elemType, symbols);
        if(isPushContainer)
            out.push_back(')');
        out.append("; } }\r\n");
    }
    else if(isSetContainer){
        return;
    }
    else if(IsDeserializableClass(symbols.findClass(elemName))){
        EmitNwbListContainerLoopHeader(out, fieldName, isForwardList, isFixedVec, isDynVec);
        out.append("{ if(!");
        out.append(elemAccessor.data(), elemAccessor.size());
        out.append(".isMap()) return false; ");
        if(isPushContainer){
            out.append("this->");
            out.append(fieldName.data(), fieldName.size());
            out.append(isForwardList ? ".emplace_front(); if(!this->" : ".emplace_back(); if(!this->");
            out.append(fieldName.data(), fieldName.size());
            out.append(isForwardList ? ".front().deserializeNwb(" : ".back().deserializeNwb(");
            out.append(elemAccessor.data(), elemAccessor.size());
            out.append(")) return false; } }\r\n");
        }
        else{
            out.append("if(!this->");
            out.append(fieldName.data(), fieldName.size());
            out.append("[__i].deserializeNwb(");
            out.append(elemAccessor.data(), elemAccessor.size());
            out.append(")) return false; } }\r\n");
        }
    }
    else if(u32 simdLanes = 0u; (SymbolTable::IsSimdVectorType(elemName, simdLanes) || SymbolTable::IsUnalignedVectorType(elemName, simdLanes)) && simdLanes >= 2u && simdLanes <= 4u){
        EmitNwbListContainerLoopHeader(out, fieldName, isForwardList, isFixedVec, isDynVec);
        out.append("{ ");
        EmitNwbListSizeContinueGuard(out, elemAccessor, simdLanes);
        out.append("this->");
        out.append(fieldName.data(), fieldName.size());
        out.append(assignOrPushPrefix.data(), assignOrPushPrefix.size());
        EmitNwbSimdVectorCtorExpr(out, elemName, simdLanes, elemListAccessor);
        if(isPushContainer)
            out.push_back(')');
        out.append("; } }\r\n");
    }
    else if(SymbolTable::IsSimdMatrixType(elemName)){
        const u32 elemCount = SimdMatrixElementCount(elemName);
        EmitNwbListContainerLoopHeader(out, fieldName, isForwardList, isFixedVec, isDynVec);
        out.append("{ ");
        EmitNwbListSizeContinueGuard(out, elemAccessor, elemCount);
        EmitNwbSimdMatrixLocalInit(out, elemName, elemListAccessor, "__k");
        out.append("this->");
        out.append(fieldName.data(), fieldName.size());
        out.append(isForwardList ? ".push_front(__m); } }\r\n" : (isPushContainer ? ".push_back(__m); } }\r\n" : "[__i] = __m; } }\r\n"));
    }
}

void EmitNwbOptionalFieldDeserialize(MString& out, const MStringView fieldName, const MStringView innerName, const SymbolTable& symbols){
    if(TryEmitNwbSharedScalarOrVectorFieldDeserialize(out, fieldName, innerName, symbols, true))
        return;
    if(SymbolTable::IsSimdMatrixType(innerName)){
        const u32 elemCount = SimdMatrixElementCount(innerName);
        EmitNwbFieldFindPrefix(out, fieldName);
        out.append("__f->isList() && __f->asList().size() == ");
        AppendUnsignedDecimal(out, elemCount);
        out.append("u){ ");
        EmitNwbSimdMatrixLocalInit(out, innerName, "__f->asList()", "__i");
        out.append("this->");
        out.append(fieldName.data(), fieldName.size());
        out.append(" = __m; }\r\n");
    }
    else if(IsDeserializableClass(symbols.findClass(innerName))){
        EmitNwbFieldFindPrefix(out, fieldName);
        out.append("__f->isMap()){ this->");
        out.append(fieldName.data(), fieldName.size());
        out.append(".emplace(); if(!this->");
        out.append(fieldName.data(), fieldName.size());
        out.append("->deserializeNwb(*__f)){ this->");
        out.append(fieldName.data(), fieldName.size());
        out.append(".reset(); return false; } }\r\n");
    }
}

void EmitNwbMapFieldDeserialize(MString& out, const MStringView fieldName, const MStringView keyName, const MStringView valName, const TypeRef& valType, const SymbolTable& symbols){
    const b8 isStringLikeKey = IsNwbMapKeyType(keyName);
    const b8 isEnumKey = symbols.findEnum(keyName) != nullptr;
    if(!isStringLikeKey && !isEnumKey)
        return;

    const auto emitKeyGuardOpen = [&](){
        if(isEnumKey){
            out.append("if(const auto __ek = ");
            out.append(keyName.data(), keyName.size());
            out.append("::parseNamedEnum(__k)){ ");
        }
        else if(RequiresNwbNameAdmission(keyName))
            out.append("if(const auto __key = ::Name::FromText(AStringView(__k.data(), __k.size()))){ ");
    };
    const auto emitKeyGuardClose = [&](){
        if(isEnumKey || RequiresNwbNameAdmission(keyName))
            out.append(" }");
    };

    if(symbols.findEnum(valName)){
        EmitNwbFieldFindPrefix(out, fieldName);
        out.append("__f->isMap()){ this->");
        out.append(fieldName.data(), fieldName.size());
        out.append(".clear(); for(const auto& [__k, __v] : __f->asMap()){ ");
        emitKeyGuardOpen();
        out.append("if(__v.isString()){ if(const auto __e = ");
        out.append(valName.data(), valName.size());
        out.append("::parseNamedEnum(__v.asString())) this->");
        out.append(fieldName.data(), fieldName.size());
        out.push_back('[');
        EmitNwbMapKeyExpr(out, keyName, isEnumKey);
        out.append("] = *__e; }");
        emitKeyGuardClose();
        out.append(" } }\r\n");
    }
    else if(IsScalarOrStringLikeNwbElement(valName)){
        EmitNwbFieldFindPrefix(out, fieldName);
        out.append("__f->isMap()){ this->");
        out.append(fieldName.data(), fieldName.size());
        out.append(".clear(); for(const auto& [__k, __v] : __f->asMap()){ ");
        emitKeyGuardOpen();
        EmitNwbValueAdmissionGuard(out, valName, "__v");
        out.append("this->");
        out.append(fieldName.data(), fieldName.size());
        out.push_back('[');
        EmitNwbMapKeyExpr(out, keyName, isEnumKey);
        out.append("] = ");
        EmitNwbValueConversionExpr(out, valName, "__v", valType, symbols);
        out.push_back(';');
        emitKeyGuardClose();
        out.append(" } }\r\n");
    }
    else if(IsDeserializableClass(symbols.findClass(valName))){
        EmitNwbFieldFindPrefix(out, fieldName);
        out.append("__f->isMap()){ this->");
        out.append(fieldName.data(), fieldName.size());
        out.append(".clear(); for(const auto& [__k, __v] : __f->asMap()){ ");
        emitKeyGuardOpen();
        out.append("if(!__v.isMap() || !this->");
        out.append(fieldName.data(), fieldName.size());
        out.push_back('[');
        EmitNwbMapKeyExpr(out, keyName, isEnumKey);
        out.append("].deserializeNwb(__v)) return false;");
        emitKeyGuardClose();
        out.append(" } }\r\n");
    }
    else if(u32 simdLanes = 0u; (SymbolTable::IsSimdVectorType(valName, simdLanes) || SymbolTable::IsUnalignedVectorType(valName, simdLanes)) && simdLanes >= 2u && simdLanes <= 4u){
        EmitNwbFieldFindPrefix(out, fieldName);
        out.append("__f->isMap()){ this->");
        out.append(fieldName.data(), fieldName.size());
        out.append(".clear(); for(const auto& [__k, __v] : __f->asMap()){ ");
        emitKeyGuardOpen();
        EmitNwbListSizeContinueGuard(out, "__v", simdLanes);
        out.append("this->");
        out.append(fieldName.data(), fieldName.size());
        out.push_back('[');
        EmitNwbMapKeyExpr(out, keyName, isEnumKey);
        out.append("] = ");
        EmitNwbSimdVectorCtorExpr(out, valName, simdLanes, "__v.asList()");
        out.push_back(';');
        emitKeyGuardClose();
        out.append(" } }\r\n");
    }
    else if(SymbolTable::IsSimdMatrixType(valName)){
        const u32 elemCount = SimdMatrixElementCount(valName);
        EmitNwbFieldFindPrefix(out, fieldName);
        out.append("__f->isMap()){ this->");
        out.append(fieldName.data(), fieldName.size());
        out.append(".clear(); for(const auto& [__k, __v] : __f->asMap()){ ");
        emitKeyGuardOpen();
        EmitNwbListSizeContinueGuard(out, "__v", elemCount);
        EmitNwbSimdMatrixLocalInit(out, valName, "__v.asList()", "__i");
        out.append("this->");
        out.append(fieldName.data(), fieldName.size());
        out.push_back('[');
        EmitNwbMapKeyExpr(out, keyName, isEnumKey);
        out.append("] = __m;");
        emitKeyGuardClose();
        out.append(" } }\r\n");
    }
}

void EmitCppTypeOrBaseName(MString& out, const TypeRef* typeRef, const MStringView baseName, const SymbolTable& symbols){
    if(typeRef){
        const CppTypeEmitter typeEmitter(symbols);
        typeEmitter.emitTypeCpp(out, *typeRef);
    }
    else{
        out.append(baseName.data(), baseName.size());
    }
}

void EmitNwbContainerElementDependencyExtract(MString& out, const TypeRef* itemTypeRef, const MStringView itemBaseName, const MStringView itemExpr, const SymbolTable& symbols){
    out.append("if(");
    out.append(itemExpr.data(), itemExpr.size());
    if(IsAssetHandleType(itemBaseName)){
        out.append(".isString()){ if(const auto __name = ::Name::FromText(");
        out.append(itemExpr.data(), itemExpr.size());
        out.append(".asString()); __name && *__name) callback(*__name); }");
    }
    else{
        out.append(".isMap() && !");
        EmitCppTypeOrBaseName(out, itemTypeRef, itemBaseName, symbols);
        out.append("::extractNwbAssetDependencies(");
        out.append(itemExpr.data(), itemExpr.size());
        out.append(", callback)) return false;");
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void ReflectionEmitter::emitDeserializeNwbMethod(MString& out, const Span<const FieldDecl* const> fields)const{
    using namespace __hidden_adhd_reflection_emitter_nwb;

    out.append("    template<typename NwbValue>\r\n");
    out.append("    [[nodiscard]] b8 deserializeNwb(const NwbValue& inMap)noexcept{\r\n");
    out.append("        if(!inMap.isMap())\r\n");
    out.append("            return false;\r\n");
    for(const FieldDecl* field : fields){
        if(!field || !IsFieldReflected(*field) || !field->type)
            continue;
        const TypeRef* resolvedFieldType = m_symbols.unwrapTypeAlias(field->type);
        const MStringView tname = resolvedFieldType ? resolvedFieldType->name : field->type->name;
        if(TryEmitNwbSharedScalarOrVectorFieldDeserialize(out, field->name, tname, m_symbols)){
            continue;
        }
        else if(SymbolTable::IsSimdMatrixType(tname)){
            const u32 elemCount = SimdMatrixElementCount(tname);
            EmitNwbFieldFindPrefix(out, field->name);
            out.append("__f->isList() && __f->asList().size() == ");
            AppendUnsignedDecimal(out, elemCount);
            out.append("u){ ");
            EmitNwbSimdMatrixElementLoop(out, "this->", field->name, tname, "__f->asList()", "__i", elemCount);
            out.append(" }\r\n");
        }
        else if(IsDeserializableClass(m_symbols.findClass(tname))){
            EmitNwbFieldFindPrefix(out, field->name);
            out.append("__f->isMap() && !this->");
            out.append(field->name.data(), field->name.size());
            out.append(".deserializeNwb(*__f)) return false;\r\n");
        }
        else if(IsReflectedLinearContainerType(tname) && resolvedFieldType && !resolvedFieldType->templateArgs.empty() && resolvedFieldType->templateArgs[0].type){
            const MStringView elemName = m_symbols.resolveBaseTypeName(resolvedFieldType->templateArgs[0].type->name);
            EmitNwbListContainerFieldDeserialize(out, field->name, tname, elemName, *m_symbols.unwrapTypeAlias(resolvedFieldType->templateArgs[0].type), m_symbols);
        }
        else if(IsOptionalContainerType(tname) && resolvedFieldType && !resolvedFieldType->templateArgs.empty() && resolvedFieldType->templateArgs[0].type){
            const MStringView innerName = m_symbols.resolveBaseTypeName(resolvedFieldType->templateArgs[0].type->name);
            EmitNwbOptionalFieldDeserialize(out, field->name, innerName, m_symbols);
        }
        else if(IsReflectedMapContainerType(tname) && resolvedFieldType && resolvedFieldType->templateArgs.size() >= 2u && resolvedFieldType->templateArgs[0].type && resolvedFieldType->templateArgs[1].type){
            const MStringView keyName = m_symbols.resolveBaseTypeName(resolvedFieldType->templateArgs[0].type->name);
            const MStringView valName = m_symbols.resolveBaseTypeName(resolvedFieldType->templateArgs[1].type->name);
            EmitNwbMapFieldDeserialize(out, field->name, keyName, valName, *m_symbols.unwrapTypeAlias(resolvedFieldType->templateArgs[1].type), m_symbols);
        }
    }
    out.append("        return true;\r\n");
    out.append("    }\r\n");
}

void ReflectionEmitter::emitExtractNwbAssetDependenciesMethod(MString& out, const Span<const FieldDecl* const> fields)const{
    using namespace __hidden_adhd_reflection_emitter_nwb;

    out.append("    template<typename NwbValue, typename DependencyCallback>\r\n");
    out.append("    [[nodiscard]] static b8 extractNwbAssetDependencies(const NwbValue& inMap, [[maybe_unused]] DependencyCallback&& callback)noexcept{\r\n");
    out.append("        if(!inMap.isMap())\r\n");
    out.append("            return false;\r\n");
    for(const FieldDecl* field : fields){
        if(!field || !IsFieldReflected(*field) || !field->type || !IsAssetDependencyType(field->type, m_symbols))
            continue;
        b8 isOptional = false;
        MStringView effectiveScalarOrClassType;
        const TypeRef* effectiveTypeRef = UnwrapOptionalFieldType(field->type, m_symbols, isOptional, effectiveScalarOrClassType);

        if(IsAssetHandleType(effectiveScalarOrClassType)){
            EmitNwbFieldFindPrefix(out, field->name);
            out.append("__f->isString()){ if(const auto __name = ::Name::FromText(__f->asString()); __name && *__name) callback(*__name); }\r\n");
        }
        else if(m_symbols.findClass(effectiveScalarOrClassType)){
            EmitNwbFieldFindPrefix(out, field->name);
            out.append("__f->isMap() && !");
            EmitCppTypeOrBaseName(out, effectiveTypeRef, effectiveScalarOrClassType, m_symbols);
            out.append("::extractNwbAssetDependencies(*__f, callback)) return false;\r\n");
        }
        else if(IsReflectedLinearContainerType(effectiveScalarOrClassType) && effectiveTypeRef && !effectiveTypeRef->templateArgs.empty() && effectiveTypeRef->templateArgs[0].type){
            const TypeRef* elemTypeRef = m_symbols.unwrapTypeAlias(effectiveTypeRef->templateArgs[0].type);
            const MStringView elemName = m_symbols.resolveBaseTypeName(effectiveTypeRef->templateArgs[0].type->name);
            if(IsAssetHandleType(elemName) || m_symbols.findClass(elemName)){
                EmitNwbFieldFindPrefix(out, field->name);
                out.append("__f->isList()){ for(usize __i = 0u; __i < __f->asList().size(); ++__i){ ");
                EmitNwbContainerElementDependencyExtract(out, elemTypeRef, elemName, "__f->asList()[__i]", m_symbols);
                out.append(" } }\r\n");
            }
        }
        else if(IsReflectedMapContainerType(effectiveScalarOrClassType) && effectiveTypeRef && effectiveTypeRef->templateArgs.size() >= 2u && effectiveTypeRef->templateArgs[1].type){
            const TypeRef* valTypeRef = m_symbols.unwrapTypeAlias(effectiveTypeRef->templateArgs[1].type);
            const MStringView valName = m_symbols.resolveBaseTypeName(effectiveTypeRef->templateArgs[1].type->name);
            if(IsAssetHandleType(valName) || m_symbols.findClass(valName)){
                EmitNwbFieldFindPrefix(out, field->name);
                out.append("__f->isMap()){ for([[maybe_unused]] const auto& [__k, __v] : __f->asMap()){ ");
                if(effectiveTypeRef->templateArgs[0].type){
                    const MStringView keyName = m_symbols.resolveBaseTypeName(effectiveTypeRef->templateArgs[0].type->name);
                    if(RequiresNwbNameAdmission(keyName))
                        out.append("if(!::Name::FromText(AStringView(__k.data(), __k.size()))) continue; ");
                    else if(m_symbols.findEnum(keyName)){
                        out.append("if(!");
                        out.append(keyName.data(), keyName.size());
                        out.append("::parseNamedEnum(__k)) continue; ");
                    }
                }
                EmitNwbContainerElementDependencyExtract(out, valTypeRef, valName, "__v", m_symbols);
                out.append(" } }\r\n");
            }
        }
    }
    out.append("        return true;\r\n");
    out.append("    }\r\n");
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

