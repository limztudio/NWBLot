// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "reflection_emitter.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_reflection_emitter{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CountReflectedFields(const Span<const FieldDecl* const> fields, const SymbolTable& symbols, usize& outNonTransientCount, usize& outAssetRefCount)noexcept{
    outNonTransientCount = 0u;
    outAssetRefCount = 0u;
    for(const FieldDecl* field : fields){
        if(!field || !IsFieldReflected(*field))
            continue;
        ++outNonTransientCount;
        if(IsAssetDependencyType(field->type, symbols))
            ++outAssetRefCount;
    }
}

[[nodiscard]] usize CountReflectedMethods(const Span<const MethodDecl* const> methods)noexcept{
    usize methodCount = 0u;
    for(const MethodDecl* method : methods){
        if(method && IsMethodReflected(*method))
            ++methodCount;
    }
    return methodCount;
}

void EmitAssetTypeNameDecl(MString& out, const MStringView assetTypeName, const MStringView prefix = "    "){
    if(assetTypeName.empty())
        return;

    out.append(prefix.data(), prefix.size());
    out.append("inline static constexpr AStringView s_AssetTypeName = \"");
    out.append(assetTypeName.data(), assetTypeName.size());
    out.append("\";\r\n");
}

void EmitReflectedCountMethod(MString& out, const MStringView methodName, const usize count){
    out.append("    [[nodiscard]] static constexpr usize ");
    out.append(methodName.data(), methodName.size());
    out.append("()noexcept{ return ");
    AppendAdhdUnsigned(out, count);
    out.append("u; }\r\n");
}

struct BinaryFieldClassInfo{
    MStringView elemName;
    const TypeRef* elemType = nullptr;
    b8 isNameOrAssetRef = false;
    b8 isAssetRef = false;
    b8 isSerializeSubObject = false;
    b8 isArray = false;
};

[[nodiscard]] BinaryFieldClassInfo ClassifyBinarySerializedField(const FieldDecl& field, const SymbolTable& symbols)noexcept{
    BinaryFieldClassInfo info;
    const TypeRef* resolved = symbols.unwrapTypeAlias(field.type);
    const MStringView tname = resolved ? resolved->name : field.type->name;
    if(tname == "Name" || tname == "AssetRef"){
        info.isNameOrAssetRef = true;
        info.isAssetRef = (tname == "AssetRef");
        return info;
    }
    if(const ClassDecl* subCls = symbols.findClass(tname); subCls && subCls->isSerialize){
        info.isSerializeSubObject = true;
        return info;
    }
    if(tname == "Array" && resolved && !resolved->templateArgs.empty() && resolved->templateArgs[0].type){
        info.elemType = symbols.unwrapTypeAlias(resolved->templateArgs[0].type);
        info.elemName = info.elemType ? info.elemType->name : resolved->templateArgs[0].type->name;
        if(info.elemName == "Name" || info.elemName == "AssetRef"){
            info.isArray = true;
            info.isNameOrAssetRef = true;
            info.isAssetRef = (info.elemName == "AssetRef");
            return info;
        }
        if(const ClassDecl* elemCls = symbols.findClass(info.elemName); elemCls && elemCls->isSerialize){
            info.isArray = true;
            info.isSerializeSubObject = true;
            return info;
        }
    }
    return info;
}

void EmitSerializedBinarySizeMethod(MString& out, const Span<const FieldDecl* const> fields, const SymbolTable& symbols){
    out.append("    [[nodiscard]] static constexpr usize serializedBinarySize()noexcept{ return 0u");
    for(const FieldDecl* field : fields){
        if(!field || field->isTransient || !field->type)
            continue;
        const BinaryFieldClassInfo info = ClassifyBinarySerializedField(*field, symbols);
        if(!info.isArray && info.isNameOrAssetRef){
            out.append(" + sizeof(::NameHash)");
            continue;
        }
        if(!info.isArray && info.isSerializeSubObject){
            out.append(" + decltype(");
            out.append(field->name.data(), field->name.size());
            out.append(")::serializedBinarySize()");
            continue;
        }
        if(info.isArray && info.isNameOrAssetRef){
            out.append(" + (sizeof(");
            out.append(field->name.data(), field->name.size());
            out.append(") / sizeof(");
            out.append(field->name.data(), field->name.size());
            out.append("[0])) * sizeof(::NameHash)");
            continue;
        }
        if(info.isArray && info.isSerializeSubObject){
            out.append(" + (sizeof(");
            out.append(field->name.data(), field->name.size());
            out.append(") / sizeof(");
            out.append(field->name.data(), field->name.size());
            out.append("[0])) * ");
            if(info.elemType && !info.elemType->templateArgs.empty()){
                out.append("::RemoveCvRef_T<decltype(");
                out.append(field->name.data(), field->name.size());
                out.append("[0])>::serializedBinarySize()");
            }
            else{
                out.append(info.elemName.data(), info.elemName.size());
                out.append("::serializedBinarySize()");
            }
            continue;
        }
        out.append(" + sizeof(");
        out.append(field->name.data(), field->name.size());
        out.push_back(')');
    }
    out.append("; }\r\n");
}

void EmitSerializeNameOrAssetRefStep(MString& out, const MStringView indent, const MStringView targetPrefix, const MStringView targetName, const b8 isAssetRef, const b8 singleLineReturn){
    out.append(indent.data(), indent.size());
    out.append(singleLineReturn
        ? "if(offset + sizeof(::NameHash) > bufferCapacity) return 0u;\r\n"
        : "if(offset + sizeof(::NameHash) > bufferCapacity)\r\n");
    if(!singleLineReturn){
        out.append(indent.data(), indent.size());
        out.append("    return 0u;\r\n");
    }
    out.append(indent.data(), indent.size());
    out.append("NWB_MEMCPY(outBuffer + offset, bufferCapacity - offset, &");
    out.append(targetPrefix.data(), targetPrefix.size());
    out.append(targetName.data(), targetName.size());
    if(isAssetRef)
        out.append(".virtualPath");
    out.append(".identityHash(), sizeof(::NameHash));\r\n");
    out.append(indent.data(), indent.size());
    out.append("offset += sizeof(::NameHash);\r\n");
}

void EmitSerializeSubObjectStep(MString& out, const MStringView indent, const MStringView targetPrefix, const MStringView targetName, const b8 singleLineReturn){
    out.append(indent.data(), indent.size());
    out.append("if(offset + ");
    out.append(targetPrefix.data(), targetPrefix.size());
    out.append(targetName.data(), targetName.size());
    out.append(singleLineReturn
        ? ".serializedBinarySize() > bufferCapacity) return 0u;\r\n"
        : ".serializedBinarySize() > bufferCapacity)\r\n");
    if(!singleLineReturn){
        out.append(indent.data(), indent.size());
        out.append("    return 0u;\r\n");
    }
    out.append(indent.data(), indent.size());
    out.append("if(");
    out.append(targetPrefix.data(), targetPrefix.size());
    out.append(targetName.data(), targetName.size());
    out.append(".serializeBinary(outBuffer + offset, bufferCapacity - offset) != ");
    out.append(targetPrefix.data(), targetPrefix.size());
    out.append(targetName.data(), targetName.size());
    out.append(singleLineReturn
        ? ".serializedBinarySize()) return 0u;\r\n"
        : ".serializedBinarySize())\r\n");
    if(!singleLineReturn){
        out.append(indent.data(), indent.size());
        out.append("    return 0u;\r\n");
    }
    out.append(indent.data(), indent.size());
    out.append("offset += ");
    out.append(targetPrefix.data(), targetPrefix.size());
    out.append(targetName.data(), targetName.size());
    out.append(".serializedBinarySize();\r\n");
}

void EmitDeserializeNameOrAssetRefStep(MString& out, const MStringView indent, const MStringView targetPrefix, const MStringView targetName, const b8 isAssetRef, const b8 singleLineReturn){
    out.append(indent.data(), indent.size());
    out.append(singleLineReturn
        ? "if(offset + sizeof(::NameHash) > bufferSize) return false;\r\n"
        : "if(offset + sizeof(::NameHash) > bufferSize)\r\n");
    if(!singleLineReturn){
        out.append(indent.data(), indent.size());
        out.append("    return false;\r\n");
    }
    out.append(indent.data(), indent.size());
    out.append("{ ::NameHash __hash{}; NWB_MEMCPY(&__hash, sizeof(::NameHash), inBuffer + offset, sizeof(::NameHash)); ");
    out.append(targetPrefix.data(), targetPrefix.size());
    out.append(targetName.data(), targetName.size());
    if(isAssetRef)
        out.append(".virtualPath");
    out.append(" = ::Name(__hash); }\r\n");
    out.append(indent.data(), indent.size());
    out.append("offset += sizeof(::NameHash);\r\n");
}

void EmitDeserializeSubObjectStep(MString& out, const MStringView indent, const MStringView targetPrefix, const MStringView targetName, const b8 singleLineReturn){
    out.append(indent.data(), indent.size());
    out.append("if(offset + ");
    out.append(targetPrefix.data(), targetPrefix.size());
    out.append(targetName.data(), targetName.size());
    out.append(singleLineReturn
        ? ".serializedBinarySize() > bufferSize) return false;\r\n"
        : ".serializedBinarySize() > bufferSize)\r\n");
    if(!singleLineReturn){
        out.append(indent.data(), indent.size());
        out.append("    return false;\r\n");
    }
    out.append(indent.data(), indent.size());
    out.append("if(!");
    out.append(targetPrefix.data(), targetPrefix.size());
    out.append(targetName.data(), targetName.size());
    out.append(singleLineReturn
        ? ".deserializeBinary(inBuffer + offset, bufferSize - offset)) return false;\r\n"
        : ".deserializeBinary(inBuffer + offset, bufferSize - offset))\r\n");
    if(!singleLineReturn){
        out.append(indent.data(), indent.size());
        out.append("    return false;\r\n");
    }
    out.append(indent.data(), indent.size());
    out.append("offset += ");
    out.append(targetPrefix.data(), targetPrefix.size());
    out.append(targetName.data(), targetName.size());
    out.append(".serializedBinarySize();\r\n");
}

void EmitSerializeBinaryBody(MString& out, const Span<const FieldDecl* const> fields, const SymbolTable& symbols, const MStringView indent){
    MString innerIndent(out.get_allocator());
    innerIndent.append(indent.data(), indent.size());
    innerIndent.append("    ");
    const MStringView innerIndentView(innerIndent.data(), innerIndent.size());

    out.append(indent.data(), indent.size());
    out.append("usize offset = 0u;\r\n");
    for(const FieldDecl* field : fields){
        if(!field || field->isTransient || !field->type)
            continue;
        const BinaryFieldClassInfo info = ClassifyBinarySerializedField(*field, symbols);
        if(!info.isArray && info.isNameOrAssetRef){
            EmitSerializeNameOrAssetRefStep(out, indent, "this->", field->name, info.isAssetRef, false);
            continue;
        }
        if(!info.isArray && info.isSerializeSubObject){
            EmitSerializeSubObjectStep(out, indent, "this->", field->name, false);
            continue;
        }
        if(info.isArray && (info.isNameOrAssetRef || info.isSerializeSubObject)){
            out.append(indent.data(), indent.size());
            out.append("for(const auto& __elem : this->");
            out.append(field->name.data(), field->name.size());
            out.append("){\r\n");
            if(info.isNameOrAssetRef)
                EmitSerializeNameOrAssetRefStep(out, innerIndentView, "", "__elem", info.isAssetRef, true);
            else
                EmitSerializeSubObjectStep(out, innerIndentView, "", "__elem", true);
            out.append(indent.data(), indent.size());
            out.append("}\r\n");
            continue;
        }
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

void EmitDeserializeBinaryBody(MString& out, const Span<const FieldDecl* const> fields, const SymbolTable& symbols, const MStringView indent){
    MString innerIndent(out.get_allocator());
    innerIndent.append(indent.data(), indent.size());
    innerIndent.append("    ");
    const MStringView innerIndentView(innerIndent.data(), innerIndent.size());

    out.append(indent.data(), indent.size());
    out.append("[[maybe_unused]] usize offset = 0u;\r\n");
    for(const FieldDecl* field : fields){
        if(!field || field->isTransient || !field->type)
            continue;
        const BinaryFieldClassInfo info = ClassifyBinarySerializedField(*field, symbols);
        if(!info.isArray && info.isNameOrAssetRef){
            EmitDeserializeNameOrAssetRefStep(out, indent, "this->", field->name, info.isAssetRef, false);
            continue;
        }
        if(!info.isArray && info.isSerializeSubObject){
            EmitDeserializeSubObjectStep(out, indent, "this->", field->name, false);
            continue;
        }
        if(info.isArray && (info.isNameOrAssetRef || info.isSerializeSubObject)){
            out.append(indent.data(), indent.size());
            out.append("for(auto& __elem : this->");
            out.append(field->name.data(), field->name.size());
            out.append("){\r\n");
            if(info.isNameOrAssetRef)
                EmitDeserializeNameOrAssetRefStep(out, innerIndentView, "", "__elem", info.isAssetRef, true);
            else
                EmitDeserializeSubObjectStep(out, innerIndentView, "", "__elem", true);
            out.append(indent.data(), indent.size());
            out.append("}\r\n");
            continue;
        }
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

void EmitFieldNameLookupBody(MString& out, const Span<const FieldDecl* const> fields, const SymbolTable& symbols, const b8 assetDepOnly, const MStringView indent){
    usize idx = 0u;
    for(const FieldDecl* field : fields){
        if(!field || !IsFieldReflected(*field))
            continue;
        if(assetDepOnly && !IsAssetDependencyType(field->type, symbols))
            continue;
        out.append(indent.data(), indent.size());
        out.append("if(index == ");
        AppendAdhdUnsigned(out, idx++);
        out.append("u) return \"");
        out.append(field->name.data(), field->name.size());
        out.append("\";\r\n");
    }
    out.append(indent.data(), indent.size());
    out.append("return {};\r\n");
}

void EmitMethodNameLookupBody(MString& out, const Span<const MethodDecl* const> methods, const MStringView indent){
    usize idx = 0u;
    for(const MethodDecl* method : methods){
        if(!method || !IsMethodReflected(*method))
            continue;
        out.append(indent.data(), indent.size());
        out.append("if(index == ");
        AppendAdhdUnsigned(out, idx++);
        out.append("u) return \"");
        out.append(method->name.data(), method->name.size());
        out.append("\";\r\n");
    }
    out.append(indent.data(), indent.size());
    out.append("return {};\r\n");
}


void EmitContainerAssetDependencyLoop(MString& out, const MStringView fieldName, const MStringView elemName, const MStringView containerDerefPrefix, const b8 isOptional, const b8 isMapContainer){
    if(isOptional){
        out.append("        if(this->");
        out.append(fieldName.data(), fieldName.size());
        out.append(".has_value()){ ");
    }
    else{
        out.append("        ");
    }
    if(elemName == "AssetRef"){
        out.append(isMapContainer ? "for(const auto& [__k, __dep] : " : "for(const auto& __dep : ");
        out.append(containerDerefPrefix.data(), containerDerefPrefix.size());
        out.append(fieldName.data(), fieldName.size());
        out.append(isMapContainer ? "){ (void)__k; if(__dep.valid()) callback(__dep.virtualPath); }" : "){ if(__dep.valid()) callback(__dep.virtualPath); }");
    }
    else{
        out.append(isMapContainer ? "for(const auto& [__k, __item] : " : "for(const auto& __item : ");
        out.append(containerDerefPrefix.data(), containerDerefPrefix.size());
        out.append(fieldName.data(), fieldName.size());
        out.append(isMapContainer ? "){ (void)__k; __item.forEachAssetDependency(callback); }" : ") __item.forEachAssetDependency(callback);");
    }
    out.append(isOptional ? " }\r\n" : "\r\n");
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void ReflectionEmitter::emitInterfaceReflectionDeclarations(MString& out, const InterfaceDecl& decl, MetaArena& arena)const{
    using namespace __hidden_adhd_reflection_emitter;

    EmitAssetTypeNameDecl(out, decl.assetTypeName, "\r\n    ");
    MVector<MethodDecl*> allMethods(arena);
    TypeRef selfIfaceRef(arena);
    selfIfaceRef.name = decl.name;
    m_symbols.collectAllInterfaceMethods(selfIfaceRef, allMethods);
    EmitReflectedCountMethod(out, "reflectedMethodCount", allMethods.size());
    out.append("    [[nodiscard]] static constexpr AStringView reflectedMethodName([[maybe_unused]] const usize index)noexcept{\r\n");
    EmitMethodNameLookupBody(out, allMethods, "        ");
    out.append("    }\r\n");
}

void ReflectionEmitter::emitClassReflectionDeclarations(MString& out, const ClassDecl& decl)const{
    using namespace __hidden_adhd_reflection_emitter;

    const MVector<FieldDecl*>& activeFields = decl.orderedFields;

    out.append("public:\r\n");
    EmitAssetTypeNameDecl(out, decl.assetTypeName);

    if(decl.isSerialize){
        EmitSerializedBinarySizeMethod(out, activeFields, m_symbols);
        out.append("    [[nodiscard]] usize serializeBinary(u8* outBuffer, usize bufferCapacity)const noexcept;\r\n");
        out.append("    [[nodiscard]] b8 deserializeBinary(const u8* inBuffer, usize bufferSize)noexcept;\r\n");
    }

    if(decl.isSerialize || decl.isReflect || !decl.assetTypeName.empty()){
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
        emitExtractNwbAssetDependenciesMethod(out, activeFields);
    }
}

void ReflectionEmitter::emitInlineClassReflection(MString& out, const ClassDecl& decl, const Span<const FieldDecl* const> fields)const{
    using namespace __hidden_adhd_reflection_emitter;

    out.append("public:\r\n");
    EmitAssetTypeNameDecl(out, decl.assetTypeName);

    if(decl.isSerialize){
        EmitSerializedBinarySizeMethod(out, fields, m_symbols);
        out.append("    [[nodiscard]] usize serializeBinary([[maybe_unused]] u8* outBuffer, [[maybe_unused]] const usize bufferCapacity)const noexcept{\r\n");
        EmitSerializeBinaryBody(out, fields, m_symbols, "        ");
        out.append("    }\r\n");

        out.append("    [[nodiscard]] b8 deserializeBinary([[maybe_unused]] const u8* inBuffer, [[maybe_unused]] const usize bufferSize)noexcept{\r\n");
        EmitDeserializeBinaryBody(out, fields, m_symbols, "        ");
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
    emitExtractNwbAssetDependenciesMethod(out, fields);
}

void ReflectionEmitter::emitClassReflectionDefinitions(MString& out, const ClassDecl& decl)const{
    using namespace __hidden_adhd_reflection_emitter;

    const MVector<FieldDecl*>& activeFields = decl.orderedFields;

    if(decl.isSerialize){
        out.append("usize ");
        out.append(decl.name.data(), decl.name.size());
        out.append("::serializeBinary([[maybe_unused]] u8* outBuffer, [[maybe_unused]] const usize bufferCapacity)const noexcept{\r\n");
        EmitSerializeBinaryBody(out, activeFields, m_symbols, "    ");
        out.append("}\r\n\r\n");

        out.append("b8 ");
        out.append(decl.name.data(), decl.name.size());
        out.append("::deserializeBinary([[maybe_unused]] const u8* inBuffer, [[maybe_unused]] const usize bufferSize)noexcept{\r\n");
        EmitDeserializeBinaryBody(out, activeFields, m_symbols, "    ");
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

void ReflectionEmitter::emitAssetDependencyMethods(MString& out, const Span<const FieldDecl* const> fields)const{
    out.append("    template<typename DependencyCallback>\r\n");
    out.append("    void forEachAssetDependency([[maybe_unused]] DependencyCallback&& callback)const noexcept{\r\n");
    for(const FieldDecl* field : fields){
        if(!field || !IsFieldReflected(*field) || !field->type || !IsAssetDependencyType(field->type, m_symbols))
            continue;
        b8 isOptional = false;
        MStringView tname;
        [[maybe_unused]] const TypeRef* effectiveTypeRef = UnwrapOptionalFieldType(field->type, m_symbols, isOptional, tname);
        const MStringView memberDotOrArrow = isOptional ? "->" : ".";
        const MStringView containerDerefPrefix = isOptional ? "*this->" : "this->";

        if(tname == "AssetRef"){
            out.append("        if(this->");
            out.append(field->name.data(), field->name.size());
            if(isOptional){
                out.append(".has_value() && this->");
                out.append(field->name.data(), field->name.size());
            }
            out.append(memberDotOrArrow.data(), memberDotOrArrow.size());
            out.append("valid()) callback(this->");
            out.append(field->name.data(), field->name.size());
            out.append(memberDotOrArrow.data(), memberDotOrArrow.size());
            out.append("virtualPath);\r\n");
        }
        else if(m_symbols.findClass(tname)){
            if(isOptional){
                out.append("        if(this->");
                out.append(field->name.data(), field->name.size());
                out.append(".has_value()) this->");
            }
            else{
                out.append("        this->");
            }
            out.append(field->name.data(), field->name.size());
            out.append(memberDotOrArrow.data(), memberDotOrArrow.size());
            out.append("forEachAssetDependency(callback);\r\n");
        }
        else if(IsReflectedLinearContainerType(tname) && effectiveTypeRef && !effectiveTypeRef->templateArgs.empty() && effectiveTypeRef->templateArgs[0].type){
            const MStringView elemName = m_symbols.resolveBaseTypeName(effectiveTypeRef->templateArgs[0].type->name);
            if(elemName == "AssetRef" || m_symbols.findClass(elemName))
                __hidden_adhd_reflection_emitter::EmitContainerAssetDependencyLoop(out, field->name, elemName, containerDerefPrefix, isOptional, false);
        }
        else if(IsReflectedMapContainerType(tname) && effectiveTypeRef && effectiveTypeRef->templateArgs.size() >= 2u && effectiveTypeRef->templateArgs[1].type){
            const MStringView valName = m_symbols.resolveBaseTypeName(effectiveTypeRef->templateArgs[1].type->name);
            if(valName == "AssetRef" || m_symbols.findClass(valName))
                __hidden_adhd_reflection_emitter::EmitContainerAssetDependencyLoop(out, field->name, valName, containerDerefPrefix, isOptional, true);
        }
    }
    out.append("    }\r\n");
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

