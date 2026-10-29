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

[[nodiscard]] constexpr u32 SimdMatrixElementCount(const MStringView matTypeName)noexcept{
    return (matTypeName == "f64x3x3") ? 9u : ((matTypeName == "f32x3x4" || matTypeName == "f64x3x4") ? 12u : 16u);
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
    AppendAdhdUnsigned(out, expectedCount);
    out.append("u) continue; ");
}

[[nodiscard]] b8 IsScalarOrStringLikeNwbElement(const MStringView typeName, const SymbolTable& symbols)noexcept{
    return typeName == "b8"
        || IsIntegerScalarType(typeName)
        || IsFloatScalarType(typeName)
        || symbols.findEnum(typeName) != nullptr
        || typeName == "EntityID"
        || typeName == "String"
        || typeName == "CompactString"
        || typeName == "Path"
        || typeName == "Name"
        || typeName == "AssetRef";
}

void EmitNwbValueConversionExpr(
    MString& out,
    const MStringView typeName,
    const MStringView valueAccessor,
    const SymbolTable& symbols
){
    if(typeName == "b8"){
        out.push_back('(');
        out.append(valueAccessor.data(), valueAccessor.size());
        out.append(".asInteger() != 0)");
    }
    else if(IsIntegerScalarType(typeName) || IsFloatScalarType(typeName)){
        out.append("static_cast<");
        out.append(typeName.data(), typeName.size());
        out.append(">(");
        out.append(valueAccessor.data(), valueAccessor.size());
        out.append(IsIntegerScalarType(typeName) ? ".asInteger())" : ".toDouble())");
    }
    else if(symbols.findEnum(typeName)){
        out.append("static_cast<");
        out.append(typeName.data(), typeName.size());
        out.append("::Enum>(");
        out.append(valueAccessor.data(), valueAccessor.size());
        out.append(".asInteger())");
    }
    else if(typeName == "String"){
        out.append("AString(");
        out.append(valueAccessor.data(), valueAccessor.size());
        out.append(".asString(), s_ClassArena)");
    }
    else if(typeName == "CompactString"){
        out.append("ACompactString(");
        out.append(valueAccessor.data(), valueAccessor.size());
        out.append(".asString())");
    }
    else if(typeName == "Path"){
        out.append("Path(::ConvertString(");
        out.append(valueAccessor.data(), valueAccessor.size());
        out.append(".asString()))");
    }
    else if(typeName == "EntityID"){
        out.append("::NWB::Core::ECS::EntityID(static_cast<u32>(");
        out.append(valueAccessor.data(), valueAccessor.size());
        out.append(".asInteger()))");
    }
    else if(typeName == "Name"){
        out.append("::Name(");
        out.append(valueAccessor.data(), valueAccessor.size());
        out.append(".asString().data())");
    }
    else{
        out.append("{::Name(");
        out.append(valueAccessor.data(), valueAccessor.size());
        out.append(".asString().data())}");
    }
}

void EmitNwbSimdVectorCtorExpr(
    MString& out,
    const MStringView vecTypeName,
    const u32 simdLanes,
    const MStringView listAccessor
){
    const MStringView cppVecType = SymbolTable::mapBuiltinCppType(vecTypeName);
    const MStringView laneScalar = vecTypeName.substr(0u, 3u);
    const b8 isIntLane = (vecTypeName[0] == 'i' || vecTypeName[0] == 'u');
    const b8 isMixedVec = (vecTypeName == "f32x3_i32" || vecTypeName == "f32x3_u32" || vecTypeName == "f64x3_i64" || vecTypeName == "f64x3_u64");
    out.append(cppVecType.data(), cppVecType.size());
    out.push_back('(');
    for(u32 lane = 0u; lane < simdLanes; ++lane){
        if(lane > 0u)
            out.append(", ");
        const b8 laneIsInt = isIntLane || (isMixedVec && lane == 3u);
        const MStringView curScalar = (isMixedVec && lane == 3u) ? vecTypeName.substr(6u) : laneScalar;
        out.append("static_cast<");
        out.append(curScalar.data(), curScalar.size());
        out.append(">(");
        out.append(listAccessor.data(), listAccessor.size());
        out.push_back('[');
        AppendAdhdUnsigned(out, lane);
        out.append(laneIsInt ? "].asInteger())" : "].toDouble())");
    }
    out.push_back(')');
}

void EmitNwbMapKeyExpr(MString& out, const MStringView keyName){
    if(keyName == "String")
        out.append("AString(__k, s_ClassArena)");
    else if(keyName == "CompactString")
        out.append("ACompactString(__k)");
    else if(keyName == "Name")
        out.append("::Name(__k.data())");
    else
        out.append("Path(::ConvertString(__k))");
}

void EmitNwbEnumFieldDeserialize(MString& out, const MStringView fieldName, const MStringView enumName){
    EmitNwbFieldFindPrefix(out, fieldName);
    out.append("__f->isNumeric()) this->");
    out.append(fieldName.data(), fieldName.size());
    out.append(" = static_cast<");
    out.append(enumName.data(), enumName.size());
    out.append("::Enum>(__f->asInteger());\r\n");
    EmitNwbFieldFindPrefix(out, fieldName);
    out.append("__f->isString()){ if(const auto __e = ");
    out.append(enumName.data(), enumName.size());
    out.append("::parseNamedEnum(__f->asString())) this->");
    out.append(fieldName.data(), fieldName.size());
    out.append(" = *__e; }\r\n");
}

void EmitNwbSimdMatrixLocalInit(
    MString& out,
    const MStringView matTypeName,
    const MStringView listAccessor,
    const MStringView loopVar
){
    const MStringView cppMatType = SymbolTable::mapBuiltinCppType(matTypeName);
    const MStringView elemScalar = matTypeName.substr(0u, 3u);
    const u32 elemCount = SimdMatrixElementCount(matTypeName);
    out.append(cppMatType.data(), cppMatType.size());
    out.append(" __m{}; for(usize ");
    out.append(loopVar.data(), loopVar.size());
    out.append(" = 0u; ");
    out.append(loopVar.data(), loopVar.size());
    out.append(" < ");
    AppendAdhdUnsigned(out, elemCount);
    out.append("u; ++");
    out.append(loopVar.data(), loopVar.size());
    out.append(") __m.raw[");
    out.append(loopVar.data(), loopVar.size());
    out.append("] = static_cast<");
    out.append(elemScalar.data(), elemScalar.size());
    out.append(">(");
    out.append(listAccessor.data(), listAccessor.size());
    out.push_back('[');
    out.append(loopVar.data(), loopVar.size());
    out.append("].toDouble()); ");
}

[[nodiscard]] b8 TryEmitNwbSharedScalarOrVectorFieldDeserialize(
    MString& out,
    const MStringView fieldName,
    const MStringView typeName,
    const SymbolTable& symbols
){
    if(typeName == "b8"){
        EmitNwbFieldFindPrefix(out, fieldName);
        out.append("__f->isNumeric()) this->");
        out.append(fieldName.data(), fieldName.size());
        out.append(" = (__f->asInteger() != 0);\r\n");
        return true;
    }
    if(IsIntegerScalarType(typeName)){
        EmitNwbFieldFindPrefix(out, fieldName);
        out.append("__f->isNumeric()) this->");
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
    if(symbols.findEnum(typeName)){
        EmitNwbEnumFieldDeserialize(out, fieldName, typeName);
        return true;
    }
    if(typeName == "EntityID"){
        EmitNwbFieldFindPrefix(out, fieldName);
        out.append("__f->isNumeric()) this->");
        out.append(fieldName.data(), fieldName.size());
        out.append(" = ::NWB::Core::ECS::EntityID(static_cast<u32>(__f->asInteger()));\r\n");
        return true;
    }
    if(typeName == "Name"){
        EmitNwbFieldFindPrefix(out, fieldName);
        out.append("__f->isString()){ if(const auto __name = ::Name::FromText(__f->asString())) this->");
        out.append(fieldName.data(), fieldName.size());
        out.append(" = *__name; }\r\n");
        return true;
    }
    u32 simdLanes = 0u;
    if((SymbolTable::isSimdVectorType(typeName, simdLanes) || SymbolTable::isUnalignedVectorType(typeName, simdLanes)) && simdLanes >= 2u && simdLanes <= 4u){
        EmitNwbFieldFindPrefix(out, fieldName);
        out.append("__f->isList() && __f->asList().size() == ");
        AppendAdhdUnsigned(out, simdLanes);
        out.append("u) this->");
        out.append(fieldName.data(), fieldName.size());
        out.append(" = ");
        EmitNwbSimdVectorCtorExpr(out, typeName, simdLanes, "__f->asList()");
        out.append(";\r\n");
        return true;
    }
    return false;
}

void EmitNwbListContainerLoopHeader(
    MString& out,
    const MStringView fieldName,
    const b8 isForwardList,
    const b8 isFixedVec,
    const b8 isDynVec
){
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
        out.append(" && __i < this->");
        out.append(fieldName.data(), fieldName.size());
        out.append(isFixedVec ? ".max_size()" : ".size()");
    }
    out.append("; ++__i)");
}

void EmitNwbListContainerFieldDeserialize(
    MString& out,
    const MStringView fieldName,
    const MStringView containerTypeName,
    const MStringView elemName,
    const SymbolTable& symbols
){
    const b8 isSetContainer = (containerTypeName == "Set" || containerTypeName == "HashSet" || containerTypeName == "ParallelHashSet");
    const b8 isForwardList = (containerTypeName == "ForwardList");
    const b8 isFixedVec = (containerTypeName == "FixedVector");
    const b8 isDynVec = (containerTypeName == "Vector" || containerTypeName == "Deque" || containerTypeName == "ParallelVector" || containerTypeName == "List" || isSetContainer);
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
        out.append("; } else this->");
        out.append(fieldName.data(), fieldName.size());
        out.append(assignOrPushPrefix.data(), assignOrPushPrefix.size());
        EmitNwbValueConversionExpr(out, elemName, elemAccessor, symbols);
        if(isPushContainer)
            out.push_back(')');
        out.append("; } }\r\n");
    }
    else if(IsScalarOrStringLikeNwbElement(elemName, symbols)){
        EmitNwbListContainerLoopHeader(out, fieldName, isForwardList, isFixedVec, isDynVec);
        out.append(" this->");
        out.append(fieldName.data(), fieldName.size());
        out.append(assignOrPushPrefix.data(), assignOrPushPrefix.size());
        EmitNwbValueConversionExpr(out, elemName, elemAccessor, symbols);
        if(isPushContainer)
            out.push_back(')');
        out.append("; }\r\n");
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
    else if(u32 simdLanes = 0u; (SymbolTable::isSimdVectorType(elemName, simdLanes) || SymbolTable::isUnalignedVectorType(elemName, simdLanes)) && simdLanes >= 2u && simdLanes <= 4u){
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
    else if(SymbolTable::isSimdMatrixType(elemName)){
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

void EmitNwbOptionalFieldDeserialize(
    MString& out,
    const MStringView fieldName,
    const MStringView innerName,
    const SymbolTable& symbols
){
    if(TryEmitNwbSharedScalarOrVectorFieldDeserialize(out, fieldName, innerName, symbols))
        return;
    if(innerName == "String"){
        EmitNwbFieldFindPrefix(out, fieldName);
        out.append("__f->isString()) this->");
        out.append(fieldName.data(), fieldName.size());
        out.append(" = AString(__f->asString(), s_ClassArena);\r\n");
    }
    else if(innerName == "CompactString" || innerName == "Path"){
        EmitNwbFieldFindPrefix(out, fieldName);
        out.append("__f->isString()) this->");
        out.append(fieldName.data(), fieldName.size());
        out.append(innerName == "CompactString" ? " = ACompactString(__f->asString());\r\n" : " = Path(::ConvertString(__f->asString()));\r\n");
    }
    else if(innerName == "AssetRef"){
        EmitNwbFieldFindPrefix(out, fieldName);
        out.append("__f->isString()){ if(const auto __name = ::Name::FromText(__f->asString())) this->");
        out.append(fieldName.data(), fieldName.size());
        out.append(" = {*__name}; }\r\n");
    }
    else if(SymbolTable::isSimdMatrixType(innerName)){
        const u32 elemCount = SimdMatrixElementCount(innerName);
        EmitNwbFieldFindPrefix(out, fieldName);
        out.append("__f->isList() && __f->asList().size() == ");
        AppendAdhdUnsigned(out, elemCount);
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

void EmitNwbMapFieldDeserialize(
    MString& out,
    const MStringView fieldName,
    const MStringView keyName,
    const MStringView valName,
    const SymbolTable& symbols
){
    const b8 isStringLikeKey = (keyName == "String" || keyName == "CompactString" || keyName == "Name" || keyName == "Path");
    if(!isStringLikeKey)
        return;

    if(symbols.findEnum(valName)){
        EmitNwbFieldFindPrefix(out, fieldName);
        out.append("__f->isMap()){ this->");
        out.append(fieldName.data(), fieldName.size());
        out.append(".clear(); for(const auto& [__k, __v] : __f->asMap()){ if(__v.isString()){ if(const auto __e = ");
        out.append(valName.data(), valName.size());
        out.append("::parseNamedEnum(__v.asString())) this->");
        out.append(fieldName.data(), fieldName.size());
        out.push_back('[');
        EmitNwbMapKeyExpr(out, keyName);
        out.append("] = *__e; } else this->");
        out.append(fieldName.data(), fieldName.size());
        out.push_back('[');
        EmitNwbMapKeyExpr(out, keyName);
        out.append("] = ");
        EmitNwbValueConversionExpr(out, valName, "__v", symbols);
        out.append("; } }\r\n");
    }
    else if(IsScalarOrStringLikeNwbElement(valName, symbols)){
        EmitNwbFieldFindPrefix(out, fieldName);
        out.append("__f->isMap()){ this->");
        out.append(fieldName.data(), fieldName.size());
        out.append(".clear(); for(const auto& [__k, __v] : __f->asMap()) this->");
        out.append(fieldName.data(), fieldName.size());
        out.push_back('[');
        EmitNwbMapKeyExpr(out, keyName);
        out.append("] = ");
        EmitNwbValueConversionExpr(out, valName, "__v", symbols);
        out.append("; }\r\n");
    }
    else if(IsDeserializableClass(symbols.findClass(valName))){
        EmitNwbFieldFindPrefix(out, fieldName);
        out.append("__f->isMap()){ this->");
        out.append(fieldName.data(), fieldName.size());
        out.append(".clear(); for(const auto& [__k, __v] : __f->asMap()){ if(!__v.isMap() || !this->");
        out.append(fieldName.data(), fieldName.size());
        out.push_back('[');
        EmitNwbMapKeyExpr(out, keyName);
        out.append("].deserializeNwb(__v)) return false; } }\r\n");
    }
    else if(u32 simdLanes = 0u; (SymbolTable::isSimdVectorType(valName, simdLanes) || SymbolTable::isUnalignedVectorType(valName, simdLanes)) && simdLanes >= 2u && simdLanes <= 4u){
        EmitNwbFieldFindPrefix(out, fieldName);
        out.append("__f->isMap()){ this->");
        out.append(fieldName.data(), fieldName.size());
        out.append(".clear(); for(const auto& [__k, __v] : __f->asMap()){ ");
        EmitNwbListSizeContinueGuard(out, "__v", simdLanes);
        out.append("this->");
        out.append(fieldName.data(), fieldName.size());
        out.push_back('[');
        EmitNwbMapKeyExpr(out, keyName);
        out.append("] = ");
        EmitNwbSimdVectorCtorExpr(out, valName, simdLanes, "__v.asList()");
        out.append("; } }\r\n");
    }
    else if(SymbolTable::isSimdMatrixType(valName)){
        const u32 elemCount = SimdMatrixElementCount(valName);
        EmitNwbFieldFindPrefix(out, fieldName);
        out.append("__f->isMap()){ this->");
        out.append(fieldName.data(), fieldName.size());
        out.append(".clear(); for(const auto& [__k, __v] : __f->asMap()){ ");
        EmitNwbListSizeContinueGuard(out, "__v", elemCount);
        EmitNwbSimdMatrixLocalInit(out, valName, "__v.asList()", "__i");
        out.append("this->");
        out.append(fieldName.data(), fieldName.size());
        out.push_back('[');
        EmitNwbMapKeyExpr(out, keyName);
        out.append("] = __m; } }\r\n");
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void ReflectionEmitter::emitDeserializeNwbMethod(MString& out, const MVector<FieldDecl*>& fields)const{
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
        else if(tname == "String" || tname == "CompactString"){
            EmitNwbFieldFindPrefix(out, field->name);
            out.append("__f->isString()) this->");
            out.append(field->name.data(), field->name.size());
            out.append(".assign(__f->asString());\r\n");
        }
        else if(tname == "Path"){
            EmitNwbFieldFindPrefix(out, field->name);
            out.append("__f->isString()) this->");
            out.append(field->name.data(), field->name.size());
            out.append(" = ::ConvertString(__f->asString());\r\n");
        }
        else if(tname == "AssetRef"){
            EmitNwbFieldFindPrefix(out, field->name);
            out.append("__f->isString()){ if(const auto __name = ::Name::FromText(__f->asString())) this->");
            out.append(field->name.data(), field->name.size());
            out.append(".virtualPath = *__name; }\r\n");
        }
        else if(SymbolTable::isSimdMatrixType(tname)){
            const MStringView elemScalar = tname.substr(0u, 3u);
            const u32 elemCount = SimdMatrixElementCount(tname);
            EmitNwbFieldFindPrefix(out, field->name);
            out.append("__f->isList() && __f->asList().size() == ");
            AppendAdhdUnsigned(out, elemCount);
            out.append("u){ for(usize __i = 0u; __i < ");
            AppendAdhdUnsigned(out, elemCount);
            out.append("u; ++__i) this->");
            out.append(field->name.data(), field->name.size());
            out.append(".raw[__i] = static_cast<");
            out.append(elemScalar.data(), elemScalar.size());
            out.append(">(__f->asList()[__i].toDouble()); }\r\n");
        }
        else if(IsDeserializableClass(m_symbols.findClass(tname))){
            EmitNwbFieldFindPrefix(out, field->name);
            out.append("__f->isMap() && !this->");
            out.append(field->name.data(), field->name.size());
            out.append(".deserializeNwb(*__f)) return false;\r\n");
        }
        else if(IsReflectedLinearContainerType(tname)
            && resolvedFieldType && !resolvedFieldType->templateArgs.empty() && resolvedFieldType->templateArgs[0].type)
        {
            const MStringView elemName = m_symbols.resolveBaseTypeName(resolvedFieldType->templateArgs[0].type->name);
            EmitNwbListContainerFieldDeserialize(out, field->name, tname, elemName, m_symbols);
        }
        else if(tname == "Optional"
            && resolvedFieldType && !resolvedFieldType->templateArgs.empty() && resolvedFieldType->templateArgs[0].type)
        {
            const MStringView innerName = m_symbols.resolveBaseTypeName(resolvedFieldType->templateArgs[0].type->name);
            EmitNwbOptionalFieldDeserialize(out, field->name, innerName, m_symbols);
        }
        else if(IsReflectedMapContainerType(tname)
            && resolvedFieldType && resolvedFieldType->templateArgs.size() >= 2u
            && resolvedFieldType->templateArgs[0].type && resolvedFieldType->templateArgs[1].type)
        {
            const MStringView keyName = m_symbols.resolveBaseTypeName(resolvedFieldType->templateArgs[0].type->name);
            const MStringView valName = m_symbols.resolveBaseTypeName(resolvedFieldType->templateArgs[1].type->name);
            EmitNwbMapFieldDeserialize(out, field->name, keyName, valName, m_symbols);
        }
    }
    out.append("        return true;\r\n");
    out.append("    }\r\n");
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
