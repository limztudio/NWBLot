// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "reflection_emitter.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_reflection_emitter{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] constexpr bool IsMethodReflected(const MethodDecl& method)noexcept{
    return !method.isConstructor && !method.isDestructor && (method.access == AccessSpecifier::Public || method.isReflect);
}

[[nodiscard]] bool IsAssetDependencyField(const FieldDecl& field, const SymbolTable& symbols)noexcept{
    if(!field.type)
        return false;
    const TypeRef* resolved = symbols.unwrapTypeAlias(field.type);
    if(!resolved)
        return false;
    const MStringView tname = resolved->name;
    if(tname == "AssetRef")
        return true;
    if((tname == "Optional" || tname == "Array" || tname == "FixedVector" || tname == "Vector"
        || tname == "Deque" || tname == "ParallelVector" || tname == "List" || tname == "ForwardList"
        || tname == "Set" || tname == "HashSet" || tname == "ParallelHashSet")
        && !resolved->templateArgs.empty() && resolved->templateArgs[0].type)
    {
        return symbols.resolveBaseTypeName(resolved->templateArgs[0].type->name) == "AssetRef";
    }
    if((tname == "Map" || tname == "HashMap" || tname == "ParallelHashMap")
        && resolved->templateArgs.size() >= 2u && resolved->templateArgs[1].type)
    {
        return symbols.resolveBaseTypeName(resolved->templateArgs[1].type->name) == "AssetRef";
    }
    return false;
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
            if(IsAssetDependencyField(*field, symbols))
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
        if(assetRefOnly && !IsAssetDependencyField(*field, symbols))
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

void ReflectionEmitter::emitClassReflectionDefinitions(MString& out, const ClassDecl& decl)const{
    using namespace __hidden_adhd_reflection_emitter;

    const MVector<FieldDecl*>& activeFields = decl.orderedFields;

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
