// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "reflection_emitter.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_reflection_emitter{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] constexpr bool IsFieldReflected(const FieldDecl& field)noexcept{
    return !field.isTransient && (field.access == AccessSpecifier::Public || field.isReflect);
}

[[nodiscard]] constexpr bool IsMethodReflected(const MethodDecl& method)noexcept{
    return !method.isConstructor && !method.isDestructor && (method.access == AccessSpecifier::Public || method.isReflect);
}

void CountReflectedFields(
    const MVector<FieldDecl*>& fields,
    const SymbolTable& symbols,
    usize& outNonTransientCount,
    usize& outAssetRefCount
)noexcept{
    outNonTransientCount = 0u;
    outAssetRefCount = 0u;
    for(const FieldDecl* field : fields){
        if(field && IsFieldReflected(*field)){
            ++outNonTransientCount;
            if(field->type && symbols.resolveBaseTypeName(field->type->name) == "AssetRef")
                ++outAssetRefCount;
        }
    }
}

[[nodiscard]] usize CountReflectedMethods(const MVector<MethodDecl*>& methods)noexcept{
    usize count = 0u;
    for(const MethodDecl* method : methods){
        if(method && IsMethodReflected(*method))
            ++count;
    }
    return count;
}

void EmitMethodNameLookupBody(
    MString& out,
    const MVector<MethodDecl*>& methods,
    const MStringView indent
){
    usize currentIdx = 0u;
    for(const MethodDecl* method : methods){
        if(!method || !IsMethodReflected(*method))
            continue;
        out.append(indent.data(), indent.size());
        out.append("if(index == ");
        AppendAdhdUnsigned(out, currentIdx++);
        out.append("u) return \"");
        out.append(method->name.data(), method->name.size());
        out.append("\";\r\n");
    }
    out.append(indent.data(), indent.size());
    out.append("return {};\r\n");
}

void EmitSerializeBinaryBody(
    MString& out,
    const MVector<FieldDecl*>& fields,
    const MStringView indent
){
    out.append(indent.data(), indent.size());
    out.append("usize offset = 0u;\r\n");
    for(const FieldDecl* field : fields){
        if(!field || field->isTransient)
            continue;
        out.append(indent.data(), indent.size());
        out.append("if(offset + sizeof(this->");
        out.append(field->name.data(), field->name.size());
        out.append(") > bufferCapacity)\r\n");
        out.append(indent.data(), indent.size());
        out.append("    return 0u;\r\n");
        out.append(indent.data(), indent.size());
        out.append("NWB_MEMCPY(outBuffer + offset, bufferCapacity - offset, &this->");
        out.append(field->name.data(), field->name.size());
        out.append(", sizeof(this->");
        out.append(field->name.data(), field->name.size());
        out.append("));\r\n");
        out.append(indent.data(), indent.size());
        out.append("offset += sizeof(this->");
        out.append(field->name.data(), field->name.size());
        out.append(");\r\n");
    }
    out.append(indent.data(), indent.size());
    out.append("return offset;\r\n");
}

void EmitDeserializeBinaryBody(
    MString& out,
    const MVector<FieldDecl*>& fields,
    const MStringView indent
){
    out.append(indent.data(), indent.size());
    out.append("usize offset = 0u;\r\n");
    for(const FieldDecl* field : fields){
        if(!field || field->isTransient)
            continue;
        out.append(indent.data(), indent.size());
        out.append("if(offset + sizeof(this->");
        out.append(field->name.data(), field->name.size());
        out.append(") > bufferSize)\r\n");
        out.append(indent.data(), indent.size());
        out.append("    return false;\r\n");
        out.append(indent.data(), indent.size());
        out.append("NWB_MEMCPY(&this->");
        out.append(field->name.data(), field->name.size());
        out.append(", sizeof(this->");
        out.append(field->name.data(), field->name.size());
        out.append("), inBuffer + offset, sizeof(this->");
        out.append(field->name.data(), field->name.size());
        out.append("));\r\n");
        out.append(indent.data(), indent.size());
        out.append("offset += sizeof(this->");
        out.append(field->name.data(), field->name.size());
        out.append(");\r\n");
    }
    out.append(indent.data(), indent.size());
    out.append("return true;\r\n");
}

void EmitFieldNameLookupBody(
    MString& out,
    const MVector<FieldDecl*>& fields,
    const SymbolTable& symbols,
    const bool assetRefOnly,
    const MStringView indent
){
    usize currentIdx = 0u;
    for(const FieldDecl* field : fields){
        if(!field || !IsFieldReflected(*field))
            continue;
        if(assetRefOnly && (!field->type || symbols.resolveBaseTypeName(field->type->name) != "AssetRef"))
            continue;
        out.append(indent.data(), indent.size());
        out.append("if(index == ");
        AppendAdhdUnsigned(out, currentIdx++);
        out.append("u) return \"");
        out.append(field->name.data(), field->name.size());
        out.append("\";\r\n");
    }
    out.append(indent.data(), indent.size());
    out.append("return {};\r\n");
}

void EmitAssetTypeNameDecl(MString& out, const ClassDecl& decl){
    if(!decl.assetTypeName.empty()){
        out.append("    inline static constexpr AStringView s_AssetTypeName = \"");
        out.append(decl.assetTypeName.data(), decl.assetTypeName.size());
        out.append("\";\r\n");
    }
}

void EmitReflectedCountMethod(MString& out, const MStringView methodName, const usize count){
    out.append("    [[nodiscard]] static constexpr usize ");
    out.append(methodName.data(), methodName.size());
    out.append("()noexcept{ return ");
    AppendAdhdUnsigned(out, count);
    out.append("u; }\r\n");
}

void EmitNwbFieldFindPrefix(MString& out, const MStringView fieldName){
    out.append("        if(const auto* __f = inMap.findField(\"");
    out.append(fieldName.data(), fieldName.size());
    out.append("\"); __f && ");
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void ReflectionEmitter::emitClassReflectionDeclarations(MString& out, const ClassDecl& decl)const{
    using namespace __hidden_adhd_reflection_emitter;

    out.append("public:\r\n");
    EmitAssetTypeNameDecl(out, decl);

    if(decl.isSerialize){
        out.append("    [[nodiscard]] usize serializeBinary(u8* outBuffer, usize bufferCapacity)const noexcept;\r\n");
        out.append("    [[nodiscard]] bool deserializeBinary(const u8* inBuffer, usize bufferSize)noexcept;\r\n");
    }

    if(decl.isSerialize || decl.isReflect || !decl.assetTypeName.empty()){
        const MVector<FieldDecl*>& activeFields = decl.orderedFields;
        usize nonTransientCount = 0u;
        usize assetRefCount = 0u;
        CountReflectedFields(activeFields, m_symbols, nonTransientCount, assetRefCount);
        const usize methodCount = CountReflectedMethods(decl.methods);

        EmitReflectedCountMethod(out, "reflectedFieldCount", nonTransientCount);
        out.append("    [[nodiscard]] static AStringView reflectedFieldName(usize index)noexcept;\r\n");
        EmitReflectedCountMethod(out, "reflectedMethodCount", methodCount);
        out.append("    [[nodiscard]] static AStringView reflectedMethodName(usize index)noexcept;\r\n");
        EmitReflectedCountMethod(out, "assetDependencyFieldCount", assetRefCount);
        out.append("    [[nodiscard]] static AStringView assetDependencyFieldName(usize index)noexcept;\r\n");
        emitDeserializeNwbMethod(out, activeFields);
    }
}

void ReflectionEmitter::emitInlineClassReflection(MString& out, const ClassDecl& decl, const MVector<FieldDecl*>& fields)const{
    using namespace __hidden_adhd_reflection_emitter;

    out.append("public:\r\n");
    EmitAssetTypeNameDecl(out, decl);

    if(decl.isSerialize){
        out.append("    [[nodiscard]] usize serializeBinary(u8* outBuffer, const usize bufferCapacity)const noexcept{\r\n");
        EmitSerializeBinaryBody(out, fields, "        ");
        out.append("    }\r\n");

        out.append("    [[nodiscard]] bool deserializeBinary(const u8* inBuffer, const usize bufferSize)noexcept{\r\n");
        EmitDeserializeBinaryBody(out, fields, "        ");
        out.append("    }\r\n");
    }

    usize nonTransientCount = 0u;
    usize assetRefCount = 0u;
    CountReflectedFields(fields, m_symbols, nonTransientCount, assetRefCount);
    const usize methodCount = CountReflectedMethods(decl.methods);

    EmitReflectedCountMethod(out, "reflectedFieldCount", nonTransientCount);
    out.append("    [[nodiscard]] static AStringView reflectedFieldName([[maybe_unused]] const usize index)noexcept{\r\n");
    EmitFieldNameLookupBody(out, fields, m_symbols, false, "        ");
    out.append("    }\r\n");

    EmitReflectedCountMethod(out, "reflectedMethodCount", methodCount);
    out.append("    [[nodiscard]] static AStringView reflectedMethodName([[maybe_unused]] const usize index)noexcept{\r\n");
    EmitMethodNameLookupBody(out, decl.methods, "        ");
    out.append("    }\r\n");

    EmitReflectedCountMethod(out, "assetDependencyFieldCount", assetRefCount);
    out.append("    [[nodiscard]] static AStringView assetDependencyFieldName([[maybe_unused]] const usize index)noexcept{\r\n");
    EmitFieldNameLookupBody(out, fields, m_symbols, true, "        ");
    out.append("    }\r\n");

    emitDeserializeNwbMethod(out, fields);
}

void ReflectionEmitter::emitDeserializeNwbMethod(MString& out, const MVector<FieldDecl*>& fields)const{
    out.append("    template<typename NwbValue>\r\n");
    out.append("    [[nodiscard]] bool deserializeNwb(const NwbValue& inMap)noexcept{\r\n");
    out.append("        if(!inMap.isMap())\r\n");
    out.append("            return false;\r\n");
    for(const FieldDecl* field : fields){
        using namespace __hidden_adhd_reflection_emitter;
        if(!field || !IsFieldReflected(*field) || !field->type)
            continue;
        const TypeRef* resolvedFieldType = m_symbols.unwrapTypeAlias(field->type);
        const MStringView tname = resolvedFieldType ? resolvedFieldType->name : field->type->name;
        if(tname == "bool"){
            EmitNwbFieldFindPrefix(out, field->name);
            out.append("__f->isNumeric()) this->");
            out.append(field->name.data(), field->name.size());
            out.append(" = (__f->asInteger() != 0);\r\n");
        }
        else if(tname == "i8" || tname == "u8" || tname == "i16" || tname == "u16"
            || tname == "i32" || tname == "u32" || tname == "i64" || tname == "u64"
            || tname == "isize" || tname == "usize")
        {
            EmitNwbFieldFindPrefix(out, field->name);
            out.append("__f->isNumeric()) this->");
            out.append(field->name.data(), field->name.size());
            out.append(" = static_cast<");
            out.append(tname.data(), tname.size());
            out.append(">(__f->asInteger());\r\n");
        }
        else if(tname == "f32" || tname == "f64"){
            EmitNwbFieldFindPrefix(out, field->name);
            out.append("__f->isNumeric()) this->");
            out.append(field->name.data(), field->name.size());
            out.append(" = static_cast<");
            out.append(tname.data(), tname.size());
            out.append(">(__f->toDouble());\r\n");
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
        else if(tname == "Name"){
            EmitNwbFieldFindPrefix(out, field->name);
            out.append("__f->isString()){ if(const auto __name = ::Name::FromText(__f->asString())) this->");
            out.append(field->name.data(), field->name.size());
            out.append(" = *__name; }\r\n");
        }
        else if(tname == "AssetRef"){
            EmitNwbFieldFindPrefix(out, field->name);
            out.append("__f->isString()){ if(const auto __name = ::Name::FromText(__f->asString())) this->");
            out.append(field->name.data(), field->name.size());
            out.append(".virtualPath = *__name; }\r\n");
        }
        else if(u32 simdLanes = 0u; SymbolTable::isSimdVectorType(tname, simdLanes) && simdLanes >= 2u && simdLanes <= 4u){
            const MStringView cppVecType = SymbolTable::mapBuiltinCppType(tname);
            const MStringView laneScalar = tname.substr(0u, 3u);
            const bool isIntLane = (tname[0] == 'i' || tname[0] == 'u');
            const bool isMixedVec = (tname == "f32x3_i32" || tname == "f32x3_u32" || tname == "f64x3_i64" || tname == "f64x3_u64");
            EmitNwbFieldFindPrefix(out, field->name);
            out.append("__f->isList() && __f->asList().size() == ");
            AppendAdhdUnsigned(out, simdLanes);
            out.append("u) this->");
            out.append(field->name.data(), field->name.size());
            out.append(" = ");
            out.append(cppVecType.data(), cppVecType.size());
            out.push_back('(');
            for(u32 lane = 0u; lane < simdLanes; ++lane){
                if(lane > 0u)
                    out.append(", ");
                const bool laneIsInt = isIntLane || (isMixedVec && lane == 3u);
                const MStringView curScalar = (isMixedVec && lane == 3u) ? tname.substr(6u) : laneScalar;
                out.append("static_cast<");
                out.append(curScalar.data(), curScalar.size());
                out.append(">(__f->asList()[");
                AppendAdhdUnsigned(out, lane);
                out.append(laneIsInt ? "].asInteger())" : "].toDouble())");
            }
            out.append(");\r\n");
        }
        else if(SymbolTable::isSimdMatrixType(tname)){
            const MStringView elemScalar = tname.substr(0u, 3u);
            const u32 elemCount = (tname == "f64x3x3") ? 9u : ((tname == "f32x3x4" || tname == "f64x3x4") ? 12u : 16u);
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
        else if(m_symbols.findEnum(tname)){
            EmitNwbFieldFindPrefix(out, field->name);
            out.append("__f->isNumeric()) this->");
            out.append(field->name.data(), field->name.size());
            out.append(" = static_cast<");
            out.append(tname.data(), tname.size());
            out.append("::Enum>(__f->asInteger());\r\n");
        }
        else if(const ClassDecl* nestedCls = m_symbols.findClass(tname); nestedCls && (nestedCls->isSerialize || nestedCls->isReflect || !nestedCls->assetTypeName.empty() || !nestedCls->templateParams.empty())){
            EmitNwbFieldFindPrefix(out, field->name);
            out.append("__f->isMap() && !this->");
            out.append(field->name.data(), field->name.size());
            out.append(".deserializeNwb(*__f)) return false;\r\n");
        }
        else if((tname == "Array" || tname == "FixedVector" || tname == "Vector")
            && resolvedFieldType && !resolvedFieldType->templateArgs.empty() && resolvedFieldType->templateArgs[0].type)
        {
            const MStringView elemName = m_symbols.resolveBaseTypeName(resolvedFieldType->templateArgs[0].type->name);
            const bool isFixedVec = (tname == "FixedVector");
            const bool isDynVec = (tname == "Vector");
            const bool isIntElem = elemName == "i8" || elemName == "u8" || elemName == "i16" || elemName == "u16"
                || elemName == "i32" || elemName == "u32" || elemName == "i64" || elemName == "u64"
                || elemName == "isize" || elemName == "usize";
            const bool isFloatElem = elemName == "f32" || elemName == "f64";
            const bool isEnumElem = m_symbols.findEnum(elemName) != nullptr;
            const ClassDecl* elemCls = m_symbols.findClass(elemName);
            const bool isClassElem = elemCls && (elemCls->isSerialize || elemCls->isReflect || !elemCls->assetTypeName.empty() || !elemCls->templateParams.empty());
            if(elemName == "bool" || isIntElem || isFloatElem || isEnumElem
                || elemName == "String" || elemName == "CompactString" || elemName == "Path" || elemName == "Name" || elemName == "AssetRef")
            {
                EmitNwbFieldFindPrefix(out, field->name);
                out.append("__f->isList()){ ");
                if(isFixedVec || isDynVec){
                    out.append("this->");
                    out.append(field->name.data(), field->name.size());
                    out.append(".clear(); ");
                }
                out.append("for(usize __i = 0u; __i < __f->asList().size()");
                if(!isDynVec){
                    out.append(" && __i < this->");
                    out.append(field->name.data(), field->name.size());
                    out.append(isFixedVec ? ".max_size()" : ".size()");
                }
                out.append("; ++__i) ");
                if(isFixedVec || isDynVec){
                    out.append("this->");
                    out.append(field->name.data(), field->name.size());
                    out.append(".push_back(");
                }
                else{
                    out.append("this->");
                    out.append(field->name.data(), field->name.size());
                    out.append("[__i] = ");
                }
                if(elemName == "bool"){
                    out.append("(__f->asList()[__i].asInteger() != 0)");
                }
                else if(isIntElem || isFloatElem){
                    out.append("static_cast<");
                    out.append(elemName.data(), elemName.size());
                    out.append(isIntElem ? ">(__f->asList()[__i].asInteger())" : ">(__f->asList()[__i].toDouble())");
                }
                else if(isEnumElem){
                    out.append("static_cast<");
                    out.append(elemName.data(), elemName.size());
                    out.append("::Enum>(__f->asList()[__i].asInteger())");
                }
                else if(elemName == "String"){
                    out.append("AString(__f->asList()[__i].asString(), s_ClassArena)");
                }
                else if(elemName == "CompactString"){
                    out.append("ACompactString(__f->asList()[__i].asString())");
                }
                else if(elemName == "Path"){
                    out.append("Path(::ConvertString(__f->asList()[__i].asString()))");
                }
                else if(elemName == "Name"){
                    out.append("::Name(__f->asList()[__i].asString().data())");
                }
                else{
                    out.append("{::Name(__f->asList()[__i].asString().data())}");
                }
                if(isFixedVec || isDynVec)
                    out.push_back(')');
                out.append("; }\r\n");
            }
            else if(isClassElem){
                EmitNwbFieldFindPrefix(out, field->name);
                out.append("__f->isList()){ ");
                if(isFixedVec || isDynVec){
                    out.append("this->");
                    out.append(field->name.data(), field->name.size());
                    out.append(".clear(); ");
                }
                out.append("for(usize __i = 0u; __i < __f->asList().size()");
                if(!isDynVec){
                    out.append(" && __i < this->");
                    out.append(field->name.data(), field->name.size());
                    out.append(isFixedVec ? ".max_size()" : ".size()");
                }
                out.append("; ++__i){ if(!__f->asList()[__i].isMap()) return false; ");
                if(isFixedVec || isDynVec){
                    out.append("this->");
                    out.append(field->name.data(), field->name.size());
                    out.append(".emplace_back(); if(!this->");
                    out.append(field->name.data(), field->name.size());
                    out.append(".back().deserializeNwb(__f->asList()[__i])) return false; } }\r\n");
                }
                else{
                    out.append("if(!this->");
                    out.append(field->name.data(), field->name.size());
                    out.append("[__i].deserializeNwb(__f->asList()[__i])) return false; } }\r\n");
                }
            }
        }
        else if(tname == "Optional"
            && resolvedFieldType && !resolvedFieldType->templateArgs.empty() && resolvedFieldType->templateArgs[0].type)
        {
            const MStringView innerName = m_symbols.resolveBaseTypeName(resolvedFieldType->templateArgs[0].type->name);
            const bool isIntInner = innerName == "i8" || innerName == "u8" || innerName == "i16" || innerName == "u16"
                || innerName == "i32" || innerName == "u32" || innerName == "i64" || innerName == "u64"
                || innerName == "isize" || innerName == "usize";
            if(innerName == "bool"){
                EmitNwbFieldFindPrefix(out, field->name);
                out.append("__f->isNumeric()) this->");
                out.append(field->name.data(), field->name.size());
                out.append(" = (__f->asInteger() != 0);\r\n");
            }
            else if(isIntInner){
                EmitNwbFieldFindPrefix(out, field->name);
                out.append("__f->isNumeric()) this->");
                out.append(field->name.data(), field->name.size());
                out.append(" = static_cast<");
                out.append(innerName.data(), innerName.size());
                out.append(">(__f->asInteger());\r\n");
            }
            else if(innerName == "f32" || innerName == "f64"){
                EmitNwbFieldFindPrefix(out, field->name);
                out.append("__f->isNumeric()) this->");
                out.append(field->name.data(), field->name.size());
                out.append(" = static_cast<");
                out.append(innerName.data(), innerName.size());
                out.append(">(__f->toDouble());\r\n");
            }
            else if(m_symbols.findEnum(innerName)){
                EmitNwbFieldFindPrefix(out, field->name);
                out.append("__f->isNumeric()) this->");
                out.append(field->name.data(), field->name.size());
                out.append(" = static_cast<");
                out.append(innerName.data(), innerName.size());
                out.append("::Enum>(__f->asInteger());\r\n");
            }
            else if(innerName == "String"){
                EmitNwbFieldFindPrefix(out, field->name);
                out.append("__f->isString()) this->");
                out.append(field->name.data(), field->name.size());
                out.append(" = AString(__f->asString(), s_ClassArena);\r\n");
            }
            else if(innerName == "CompactString" || innerName == "Path"){
                EmitNwbFieldFindPrefix(out, field->name);
                out.append("__f->isString()) this->");
                out.append(field->name.data(), field->name.size());
                out.append(innerName == "CompactString" ? " = ACompactString(__f->asString());\r\n" : " = Path(::ConvertString(__f->asString()));\r\n");
            }
            else if(innerName == "Name"){
                EmitNwbFieldFindPrefix(out, field->name);
                out.append("__f->isString()){ if(const auto __name = ::Name::FromText(__f->asString())) this->");
                out.append(field->name.data(), field->name.size());
                out.append(" = *__name; }\r\n");
            }
            else if(innerName == "AssetRef"){
                EmitNwbFieldFindPrefix(out, field->name);
                out.append("__f->isString()){ if(const auto __name = ::Name::FromText(__f->asString())) this->");
                out.append(field->name.data(), field->name.size());
                out.append(" = {*__name}; }\r\n");
            }
            else if(u32 simdLanes = 0u; SymbolTable::isSimdVectorType(innerName, simdLanes) && simdLanes >= 2u && simdLanes <= 4u){
                const MStringView cppVecType = SymbolTable::mapBuiltinCppType(innerName);
                const MStringView laneScalar = innerName.substr(0u, 3u);
                const bool isIntLane = (innerName[0] == 'i' || innerName[0] == 'u');
                const bool isMixedVec = (innerName == "f32x3_i32" || innerName == "f32x3_u32" || innerName == "f64x3_i64" || innerName == "f64x3_u64");
                EmitNwbFieldFindPrefix(out, field->name);
                out.append("__f->isList() && __f->asList().size() == ");
                AppendAdhdUnsigned(out, simdLanes);
                out.append("u) this->");
                out.append(field->name.data(), field->name.size());
                out.append(" = ");
                out.append(cppVecType.data(), cppVecType.size());
                out.push_back('(');
                for(u32 lane = 0u; lane < simdLanes; ++lane){
                    if(lane > 0u)
                        out.append(", ");
                    const bool laneIsInt = isIntLane || (isMixedVec && lane == 3u);
                    const MStringView curScalar = (isMixedVec && lane == 3u) ? innerName.substr(6u) : laneScalar;
                    out.append("static_cast<");
                    out.append(curScalar.data(), curScalar.size());
                    out.append(">(__f->asList()[");
                    AppendAdhdUnsigned(out, lane);
                    out.append(laneIsInt ? "].asInteger())" : "].toDouble())");
                }
                out.append(");\r\n");
            }
            else if(const ClassDecl* innerCls = m_symbols.findClass(innerName); innerCls && (innerCls->isSerialize || innerCls->isReflect || !innerCls->assetTypeName.empty() || !innerCls->templateParams.empty())){
                EmitNwbFieldFindPrefix(out, field->name);
                out.append("__f->isMap()){ this->");
                out.append(field->name.data(), field->name.size());
                out.append(".emplace(); if(!this->");
                out.append(field->name.data(), field->name.size());
                out.append("->deserializeNwb(*__f)){ this->");
                out.append(field->name.data(), field->name.size());
                out.append(".reset(); return false; } }\r\n");
            }
        }
    }
    out.append("        return true;\r\n");
    out.append("    }\r\n");
}

void ReflectionEmitter::emitClassReflectionDefinitions(MString& out, const ClassDecl& decl)const{
    using namespace __hidden_adhd_reflection_emitter;

    const MVector<FieldDecl*>& activeFields = !decl.orderedFields.empty() ? decl.orderedFields : decl.fields;

    if(decl.isSerialize){
        out.append("usize ");
        out.append(decl.name.data(), decl.name.size());
        out.append("::serializeBinary(u8* outBuffer, const usize bufferCapacity)const noexcept{\r\n");
        EmitSerializeBinaryBody(out, activeFields, "    ");
        out.append("}\r\n\r\n");

        out.append("bool ");
        out.append(decl.name.data(), decl.name.size());
        out.append("::deserializeBinary(const u8* inBuffer, const usize bufferSize)noexcept{\r\n");
        EmitDeserializeBinaryBody(out, activeFields, "    ");
        out.append("}\r\n\r\n");
    }

    if(decl.isSerialize || decl.isReflect || !decl.assetTypeName.empty()){
        out.append("AStringView ");
        out.append(decl.name.data(), decl.name.size());
        out.append("::reflectedFieldName([[maybe_unused]] const usize index)noexcept{\r\n");
        EmitFieldNameLookupBody(out, activeFields, m_symbols, false, "    ");
        out.append("}\r\n\r\n");

        out.append("AStringView ");
        out.append(decl.name.data(), decl.name.size());
        out.append("::reflectedMethodName([[maybe_unused]] const usize index)noexcept{\r\n");
        EmitMethodNameLookupBody(out, decl.methods, "    ");
        out.append("}\r\n\r\n");

        out.append("AStringView ");
        out.append(decl.name.data(), decl.name.size());
        out.append("::assetDependencyFieldName([[maybe_unused]] const usize index)noexcept{\r\n");
        EmitFieldNameLookupBody(out, activeFields, m_symbols, true, "    ");
        out.append("}\r\n\r\n");
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

