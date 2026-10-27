// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "reflection_emitter.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_reflection_emitter{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CountReflectedFields(
    const MVector<FieldDecl*>& fields,
    usize& outNonTransientCount,
    usize& outAssetRefCount
)noexcept{
    outNonTransientCount = 0u;
    outAssetRefCount = 0u;
    for(const FieldDecl* field : fields){
        if(field && !field->isTransient){
            ++outNonTransientCount;
            if(field->type && field->type->name == "AssetRef")
                ++outAssetRefCount;
        }
    }
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
    const bool assetRefOnly,
    const MStringView indent
){
    usize currentIdx = 0u;
    for(const FieldDecl* field : fields){
        if(!field || field->isTransient)
            continue;
        if(assetRefOnly && (!field->type || field->type->name != "AssetRef"))
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void ReflectionEmitter::emitClassReflectionDeclarations(MString& out, const ClassDecl& decl)const{
    out.append("public:\r\n");

    if(!decl.assetTypeName.empty()){
        out.append("    inline static constexpr AStringView s_AssetTypeName = \"");
        out.append(decl.assetTypeName.data(), decl.assetTypeName.size());
        out.append("\";\r\n");
    }

    if(decl.isSerialize){
        out.append("    [[nodiscard]] usize serializeBinary(u8* outBuffer, usize bufferCapacity)const noexcept;\r\n");
        out.append("    [[nodiscard]] bool deserializeBinary(const u8* inBuffer, usize bufferSize)noexcept;\r\n");
    }

    if(decl.isSerialize || decl.isReflect || !decl.assetTypeName.empty()){
        out.append("    [[nodiscard]] static constexpr usize reflectedFieldCount()noexcept;\r\n");
        out.append("    [[nodiscard]] static AStringView reflectedFieldName(usize index)noexcept;\r\n");
        out.append("    [[nodiscard]] static constexpr usize assetDependencyFieldCount()noexcept;\r\n");
        out.append("    [[nodiscard]] static AStringView assetDependencyFieldName(usize index)noexcept;\r\n");
        emitDeserializeNwbMethod(out, decl.fields);
    }
}

void ReflectionEmitter::emitInlineClassReflection(MString& out, const ClassDecl& decl, const MVector<FieldDecl*>& fields)const{
    using namespace __hidden_adhd_reflection_emitter;

    out.append("public:\r\n");

    if(!decl.assetTypeName.empty()){
        out.append("    inline static constexpr AStringView s_AssetTypeName = \"");
        out.append(decl.assetTypeName.data(), decl.assetTypeName.size());
        out.append("\";\r\n");
    }

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
    CountReflectedFields(fields, nonTransientCount, assetRefCount);

    out.append("    [[nodiscard]] static constexpr usize reflectedFieldCount()noexcept{ return ");
    AppendAdhdUnsigned(out, nonTransientCount);
    out.append("u; }\r\n");

    out.append("    [[nodiscard]] static AStringView reflectedFieldName([[maybe_unused]] const usize index)noexcept{\r\n");
    EmitFieldNameLookupBody(out, fields, false, "        ");
    out.append("    }\r\n");

    out.append("    [[nodiscard]] static constexpr usize assetDependencyFieldCount()noexcept{ return ");
    AppendAdhdUnsigned(out, assetRefCount);
    out.append("u; }\r\n");

    out.append("    [[nodiscard]] static AStringView assetDependencyFieldName([[maybe_unused]] const usize index)noexcept{\r\n");
    EmitFieldNameLookupBody(out, fields, true, "        ");
    out.append("    }\r\n");

    emitDeserializeNwbMethod(out, fields);
}

void ReflectionEmitter::emitDeserializeNwbMethod(MString& out, const MVector<FieldDecl*>& fields)const{
    out.append("    template<typename NwbValue>\r\n");
    out.append("    [[nodiscard]] bool deserializeNwb(const NwbValue& inMap)noexcept{\r\n");
    out.append("        if(!inMap.isMap())\r\n");
    out.append("            return false;\r\n");
    for(const FieldDecl* field : fields){
        if(!field || field->isTransient || field->access != AccessSpecifier::Public || !field->type)
            continue;
        const MStringView tname = field->type->name;
        if(tname == "bool"){
            out.append("        if(const auto* __f = inMap.findField(\"");
            out.append(field->name.data(), field->name.size());
            out.append("\"); __f && __f->isNumeric()) this->");
            out.append(field->name.data(), field->name.size());
            out.append(" = (__f->asInteger() != 0);\r\n");
        }
        else if(tname == "i8" || tname == "u8" || tname == "i16" || tname == "u16"
            || tname == "i32" || tname == "u32" || tname == "i64" || tname == "u64"
            || tname == "isize" || tname == "usize")
        {
            out.append("        if(const auto* __f = inMap.findField(\"");
            out.append(field->name.data(), field->name.size());
            out.append("\"); __f && __f->isNumeric()) this->");
            out.append(field->name.data(), field->name.size());
            out.append(" = static_cast<");
            out.append(tname.data(), tname.size());
            out.append(">(__f->asInteger());\r\n");
        }
        else if(tname == "f32" || tname == "f64"){
            out.append("        if(const auto* __f = inMap.findField(\"");
            out.append(field->name.data(), field->name.size());
            out.append("\"); __f && __f->isNumeric()) this->");
            out.append(field->name.data(), field->name.size());
            out.append(" = static_cast<");
            out.append(tname.data(), tname.size());
            out.append(">(__f->toDouble());\r\n");
        }
        else if(tname == "String" || tname == "CompactString"){
            out.append("        if(const auto* __f = inMap.findField(\"");
            out.append(field->name.data(), field->name.size());
            out.append("\"); __f && __f->isString()) this->");
            out.append(field->name.data(), field->name.size());
            out.append(".assign(__f->asString());\r\n");
        }
        else if(tname == "Name"){
            out.append("        if(const auto* __f = inMap.findField(\"");
            out.append(field->name.data(), field->name.size());
            out.append("\"); __f && __f->isString()) this->");
            out.append(field->name.data(), field->name.size());
            out.append(" = ::NWB::Core::Common::Name(__f->asString());\r\n");
        }
        else if(tname == "AssetRef"){
            out.append("        if(const auto* __f = inMap.findField(\"");
            out.append(field->name.data(), field->name.size());
            out.append("\"); __f && __f->isString()) this->");
            out.append(field->name.data(), field->name.size());
            out.append(".virtualPath = ::NWB::Core::Common::Name(__f->asString());\r\n");
        }
        else if(tname == "f32x4"){
            out.append("        if(const auto* __f = inMap.findField(\"");
            out.append(field->name.data(), field->name.size());
            out.append("\"); __f && __f->isList() && __f->asList().size() == 4u) this->");
            out.append(field->name.data(), field->name.size());
            out.append(" = Float4(static_cast<f32>(__f->asList()[0].toDouble()), static_cast<f32>(__f->asList()[1].toDouble()), static_cast<f32>(__f->asList()[2].toDouble()), static_cast<f32>(__f->asList()[3].toDouble()));\r\n");
        }
        else if(m_symbols.findEnum(tname)){
            out.append("        if(const auto* __f = inMap.findField(\"");
            out.append(field->name.data(), field->name.size());
            out.append("\"); __f && __f->isNumeric()) this->");
            out.append(field->name.data(), field->name.size());
            out.append(" = static_cast<");
            out.append(tname.data(), tname.size());
            out.append("::Enum>(__f->asInteger());\r\n");
        }
        else if(const ClassDecl* nestedCls = m_symbols.findClass(tname); nestedCls && (nestedCls->isSerialize || nestedCls->isReflect || !nestedCls->assetTypeName.empty() || !nestedCls->templateParams.empty())){
            out.append("        if(const auto* __f = inMap.findField(\"");
            out.append(field->name.data(), field->name.size());
            out.append("\"); __f && __f->isMap() && !this->");
            out.append(field->name.data(), field->name.size());
            out.append(".deserializeNwb(*__f)) return false;\r\n");
        }
    }
    out.append("        return true;\r\n");
    out.append("    }\r\n");
}

void ReflectionEmitter::emitClassReflectionDefinitions(MString& out, const ClassDecl& decl)const{
    using namespace __hidden_adhd_reflection_emitter;

    if(decl.isSerialize){
        out.append("usize ");
        out.append(decl.name.data(), decl.name.size());
        out.append("::serializeBinary(u8* outBuffer, const usize bufferCapacity)const noexcept{\r\n");
        EmitSerializeBinaryBody(out, decl.fields, "    ");
        out.append("}\r\n\r\n");

        out.append("bool ");
        out.append(decl.name.data(), decl.name.size());
        out.append("::deserializeBinary(const u8* inBuffer, const usize bufferSize)noexcept{\r\n");
        EmitDeserializeBinaryBody(out, decl.fields, "    ");
        out.append("}\r\n\r\n");
    }

    if(decl.isSerialize || decl.isReflect || !decl.assetTypeName.empty()){
        usize nonTransientCount = 0u;
        usize assetRefCount = 0u;
        CountReflectedFields(decl.fields, nonTransientCount, assetRefCount);

        out.append("constexpr usize ");
        out.append(decl.name.data(), decl.name.size());
        out.append("::reflectedFieldCount()noexcept{\r\n    return ");
        AppendAdhdUnsigned(out, nonTransientCount);
        out.append("u;\r\n}\r\n\r\n");

        out.append("AStringView ");
        out.append(decl.name.data(), decl.name.size());
        out.append("::reflectedFieldName([[maybe_unused]] const usize index)noexcept{\r\n");
        EmitFieldNameLookupBody(out, decl.fields, false, "    ");
        out.append("}\r\n\r\n");

        out.append("constexpr usize ");
        out.append(decl.name.data(), decl.name.size());
        out.append("::assetDependencyFieldCount()noexcept{\r\n    return ");
        AppendAdhdUnsigned(out, assetRefCount);
        out.append("u;\r\n}\r\n\r\n");

        out.append("AStringView ");
        out.append(decl.name.data(), decl.name.size());
        out.append("::assetDependencyFieldName([[maybe_unused]] const usize index)noexcept{\r\n");
        EmitFieldNameLookupBody(out, decl.fields, true, "    ");
        out.append("}\r\n\r\n");
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

