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

void EmitNwbDependencyFieldFindPrefix(MString& out, const MStringView fieldName){
    out.append("        if(const NwbValue* __f = inMap.findField(\"");
    out.append(fieldName.data(), fieldName.size());
    out.append("\"); __f && ");
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool IsAssetDependencyType(const TypeRef* const type, const SymbolTable& symbols, const u32 depth)noexcept{
    if(!type || depth > 8u)
        return false;
    const TypeRef* resolved = symbols.unwrapTypeAlias(type);
    if(!resolved)
        return false;
    const MStringView tname = resolved->name;
    if(tname == "AssetRef")
        return true;
    if(const ClassDecl* cls = symbols.findClass(tname)){
        for(const FieldDecl* f : cls->orderedFields){
            if(f && IsFieldReflected(*f) && IsAssetDependencyField(*f, symbols, depth + 1u))
                return true;
        }
    }
    if((tname == "Optional" || IsReflectedLinearContainerType(tname))
        && !resolved->templateArgs.empty() && resolved->templateArgs[0].type)
    {
        return IsAssetDependencyType(resolved->templateArgs[0].type, symbols, depth + 1u);
    }
    if(IsReflectedMapContainerType(tname)
        && resolved->templateArgs.size() >= 2u && resolved->templateArgs[1].type)
    {
        return IsAssetDependencyType(resolved->templateArgs[1].type, symbols, depth + 1u);
    }
    return false;
}

bool IsAssetDependencyField(const FieldDecl& field, const SymbolTable& symbols, const u32 depth)noexcept{
    return IsAssetDependencyType(field.type, symbols, depth);
}

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
        emitAssetDependencyMethods(out, activeFields);
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
    emitAssetDependencyMethods(out, fields);
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

void ReflectionEmitter::emitAssetDependencyMethods(MString& out, const MVector<FieldDecl*>& fields)const{
    using namespace __hidden_adhd_reflection_emitter;

    out.append("    template<typename DependencyCallback>\r\n");
    out.append("    void forEachAssetDependency([[maybe_unused]] DependencyCallback&& callback)const{\r\n");
    for(const FieldDecl* field : fields){
        if(!field || !IsFieldReflected(*field) || !field->type || !IsAssetDependencyField(*field, m_symbols))
            continue;
        const TypeRef* resolvedFieldType = m_symbols.unwrapTypeAlias(field->type);
        const MStringView tname = resolvedFieldType ? resolvedFieldType->name : field->type->name;
        if(tname == "AssetRef"){
            out.append("        if(this->");
            out.append(field->name.data(), field->name.size());
            out.append(".valid()) callback(this->");
            out.append(field->name.data(), field->name.size());
            out.append(".virtualPath);\r\n");
        }
        else if(m_symbols.findClass(tname)){
            out.append("        this->");
            out.append(field->name.data(), field->name.size());
            out.append(".forEachAssetDependency(callback);\r\n");
        }
        else if(tname == "Optional"
            && resolvedFieldType && !resolvedFieldType->templateArgs.empty() && resolvedFieldType->templateArgs[0].type)
        {
            const MStringView innerName = m_symbols.resolveBaseTypeName(resolvedFieldType->templateArgs[0].type->name);
            if(innerName == "AssetRef"){
                out.append("        if(this->");
                out.append(field->name.data(), field->name.size());
                out.append(".has_value() && this->");
                out.append(field->name.data(), field->name.size());
                out.append("->valid()) callback(this->");
                out.append(field->name.data(), field->name.size());
                out.append("->virtualPath);\r\n");
            }
            else if(m_symbols.findClass(innerName)){
                out.append("        if(this->");
                out.append(field->name.data(), field->name.size());
                out.append(".has_value()) this->");
                out.append(field->name.data(), field->name.size());
                out.append("->forEachAssetDependency(callback);\r\n");
            }
        }
        else if(IsReflectedLinearContainerType(tname)
            && resolvedFieldType && !resolvedFieldType->templateArgs.empty() && resolvedFieldType->templateArgs[0].type)
        {
            const MStringView elemName = m_symbols.resolveBaseTypeName(resolvedFieldType->templateArgs[0].type->name);
            if(elemName == "AssetRef"){
                out.append("        for(const auto& __dep : this->");
                out.append(field->name.data(), field->name.size());
                out.append("){ if(__dep.valid()) callback(__dep.virtualPath); }\r\n");
            }
            else if(m_symbols.findClass(elemName)){
                out.append("        for(const auto& __item : this->");
                out.append(field->name.data(), field->name.size());
                out.append(") __item.forEachAssetDependency(callback);\r\n");
            }
        }
        else if(IsReflectedMapContainerType(tname)
            && resolvedFieldType && resolvedFieldType->templateArgs.size() >= 2u && resolvedFieldType->templateArgs[1].type)
        {
            const MStringView valName = m_symbols.resolveBaseTypeName(resolvedFieldType->templateArgs[1].type->name);
            if(valName == "AssetRef"){
                out.append("        for(const auto& [__k, __dep] : this->");
                out.append(field->name.data(), field->name.size());
                out.append("){ (void)__k; if(__dep.valid()) callback(__dep.virtualPath); }\r\n");
            }
            else if(m_symbols.findClass(valName)){
                out.append("        for(const auto& [__k, __item] : this->");
                out.append(field->name.data(), field->name.size());
                out.append("){ (void)__k; __item.forEachAssetDependency(callback); }\r\n");
            }
        }
    }
    out.append("    }\r\n");

    out.append("    template<typename NwbValue, typename DependencyCallback>\r\n");
    out.append("    [[nodiscard]] static bool extractNwbAssetDependencies(const NwbValue& inMap, [[maybe_unused]] DependencyCallback&& callback)noexcept{\r\n");
    out.append("        if(!inMap.isMap())\r\n");
    out.append("            return false;\r\n");
    for(const FieldDecl* field : fields){
        if(!field || !IsFieldReflected(*field) || !field->type || !IsAssetDependencyField(*field, m_symbols))
            continue;
        const TypeRef* resolvedFieldType = m_symbols.unwrapTypeAlias(field->type);
        const MStringView tname = resolvedFieldType ? resolvedFieldType->name : field->type->name;
        if(tname == "AssetRef"){
            EmitNwbDependencyFieldFindPrefix(out, field->name);
            out.append("__f->isString()){ if(const auto __name = ::Name::FromText(__f->asString()); __name && *__name) callback(*__name); }\r\n");
        }
        else if(m_symbols.findClass(tname)){
            EmitNwbDependencyFieldFindPrefix(out, field->name);
            out.append("__f->isMap() && !");
            out.append(tname.data(), tname.size());
            out.append("::extractNwbAssetDependencies(*__f, callback)) return false;\r\n");
        }
        else if(tname == "Optional"
            && resolvedFieldType && !resolvedFieldType->templateArgs.empty() && resolvedFieldType->templateArgs[0].type)
        {
            const MStringView innerName = m_symbols.resolveBaseTypeName(resolvedFieldType->templateArgs[0].type->name);
            if(innerName == "AssetRef"){
                EmitNwbDependencyFieldFindPrefix(out, field->name);
                out.append("__f->isString()){ if(const auto __name = ::Name::FromText(__f->asString()); __name && *__name) callback(*__name); }\r\n");
            }
            else if(m_symbols.findClass(innerName)){
                EmitNwbDependencyFieldFindPrefix(out, field->name);
                out.append("__f->isMap() && !");
                out.append(innerName.data(), innerName.size());
                out.append("::extractNwbAssetDependencies(*__f, callback)) return false;\r\n");
            }
        }
        else if(IsReflectedLinearContainerType(tname)
            && resolvedFieldType && !resolvedFieldType->templateArgs.empty() && resolvedFieldType->templateArgs[0].type)
        {
            const MStringView elemName = m_symbols.resolveBaseTypeName(resolvedFieldType->templateArgs[0].type->name);
            if(elemName == "AssetRef"){
                EmitNwbDependencyFieldFindPrefix(out, field->name);
                out.append("__f->isList()){ for(usize __i = 0u; __i < __f->asList().size(); ++__i){ if(__f->asList()[__i].isString()){ if(const auto __name = ::Name::FromText(__f->asList()[__i].asString()); __name && *__name) callback(*__name); } } }\r\n");
            }
            else if(m_symbols.findClass(elemName)){
                EmitNwbDependencyFieldFindPrefix(out, field->name);
                out.append("__f->isList()){ for(usize __i = 0u; __i < __f->asList().size(); ++__i){ if(!__f->asList()[__i].isMap() || !");
                out.append(elemName.data(), elemName.size());
                out.append("::extractNwbAssetDependencies(__f->asList()[__i], callback)) return false; } }\r\n");
            }
        }
        else if(IsReflectedMapContainerType(tname)
            && resolvedFieldType && resolvedFieldType->templateArgs.size() >= 2u && resolvedFieldType->templateArgs[1].type)
        {
            const MStringView valName = m_symbols.resolveBaseTypeName(resolvedFieldType->templateArgs[1].type->name);
            if(valName == "AssetRef"){
                EmitNwbDependencyFieldFindPrefix(out, field->name);
                out.append("__f->isMap()){ for(const auto& [__k, __v] : __f->asMap()){ (void)__k; if(__v.isString()){ if(const auto __name = ::Name::FromText(__v.asString()); __name && *__name) callback(*__name); } } }\r\n");
            }
            else if(m_symbols.findClass(valName)){
                EmitNwbDependencyFieldFindPrefix(out, field->name);
                out.append("__f->isMap()){ for(const auto& [__k, __v] : __f->asMap()){ (void)__k; if(!__v.isMap() || !");
                out.append(valName.data(), valName.size());
                out.append("::extractNwbAssetDependencies(__v, callback)) return false; } }\r\n");
            }
        }
    }
    out.append("        return true;\r\n");
    out.append("    }\r\n");
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
