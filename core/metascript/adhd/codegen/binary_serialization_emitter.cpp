// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "binary_serialization_emitter.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_binary_serialization_emitter{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct BinaryFieldClassInfo{
    MStringView elemName;
    MStringView hashMemberSuffix;
    const TypeRef* elemType = nullptr;
    b8 isNameOrAssetRef = false;
    b8 isSerializeSubObject = false;
    b8 isArray = false;
};

[[nodiscard]] BinaryFieldClassInfo ClassifyBinarySerializedField(const FieldDecl& field, const SymbolTable& symbols)noexcept{
    BinaryFieldClassInfo info;
    const TypeRef* resolved = symbols.unwrapTypeAlias(field.type);
    const MStringView tname = resolved ? resolved->name : field.type->name;
    if(FindBuiltinBinaryHashMemberSuffix(tname, info.hashMemberSuffix)){
        info.isNameOrAssetRef = true;
        return info;
    }
    if(const ClassDecl* subCls = symbols.findClass(tname); subCls && subCls->isSerialize){
        info.isSerializeSubObject = true;
        return info;
    }
    if(IsFixedArrayContainerType(tname) && resolved && !resolved->templateArgs.empty() && resolved->templateArgs[0].type){
        info.elemType = symbols.unwrapTypeAlias(resolved->templateArgs[0].type);
        info.elemName = info.elemType ? info.elemType->name : resolved->templateArgs[0].type->name;
        if(FindBuiltinBinaryHashMemberSuffix(info.elemName, info.hashMemberSuffix)){
            info.isArray = true;
            info.isNameOrAssetRef = true;
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

void EmitNameOrAssetRefStep(MString& out, const MStringView indent, const MStringView targetPrefix, const MStringView targetName, const MStringView hashMemberSuffix, const b8 singleLineReturn, const b8 isSerialize){
    const MStringView limitName = isSerialize ? MStringView("bufferCapacity") : MStringView("bufferSize");
    const MStringView failReturn = isSerialize ? MStringView("return 0u;\r\n") : MStringView("return false;\r\n");
    out.append(indent.data(), indent.size());
    out.append("if(offset + sizeof(::NameHash) > ");
    out.append(limitName.data(), limitName.size());
    out.append(singleLineReturn ? ") " : ")\r\n");
    if(!singleLineReturn){
        out.append(indent.data(), indent.size());
        out.append("    ");
    }
    out.append(failReturn.data(), failReturn.size());
    out.append(indent.data(), indent.size());
    if(isSerialize){
        out.append("FOUNDATION_MEMCPY(outBuffer + offset, bufferCapacity - offset, &");
        out.append(targetPrefix.data(), targetPrefix.size());
        out.append(targetName.data(), targetName.size());
        out.append(hashMemberSuffix.data(), hashMemberSuffix.size());
        out.append(".identityHash(), sizeof(::NameHash));\r\n");
    }
    else{
        out.append("{ ::NameHash __hash{}; FOUNDATION_MEMCPY(&__hash, sizeof(::NameHash), inBuffer + offset, sizeof(::NameHash)); ");
        out.append(targetPrefix.data(), targetPrefix.size());
        out.append(targetName.data(), targetName.size());
        out.append(hashMemberSuffix.data(), hashMemberSuffix.size());
        out.append(" = ::Name(__hash); }\r\n");
    }
    out.append(indent.data(), indent.size());
    out.append("offset += sizeof(::NameHash);\r\n");
}

void EmitSubObjectStep(MString& out, const MStringView indent, const MStringView targetPrefix, const MStringView targetName, const b8 singleLineReturn, const b8 isSerialize){
    const MStringView limitName = isSerialize ? MStringView("bufferCapacity") : MStringView("bufferSize");
    const MStringView failReturn = isSerialize ? MStringView("return 0u;\r\n") : MStringView("return false;\r\n");
    out.append(indent.data(), indent.size());
    out.append("if(offset + ");
    out.append(targetPrefix.data(), targetPrefix.size());
    out.append(targetName.data(), targetName.size());
    out.append(".serializedBinarySize() > ");
    out.append(limitName.data(), limitName.size());
    out.append(singleLineReturn ? ") " : ")\r\n");
    if(!singleLineReturn){
        out.append(indent.data(), indent.size());
        out.append("    ");
    }
    out.append(failReturn.data(), failReturn.size());
    out.append(indent.data(), indent.size());
    out.append(isSerialize ? "if(" : "if(!");
    out.append(targetPrefix.data(), targetPrefix.size());
    out.append(targetName.data(), targetName.size());
    out.append(isSerialize ? ".serializeBinary(outBuffer + offset, bufferCapacity - offset) != " : ".deserializeBinary(inBuffer + offset, bufferSize - offset)");
    if(isSerialize){
        out.append(targetPrefix.data(), targetPrefix.size());
        out.append(targetName.data(), targetName.size());
        out.append(".serializedBinarySize()");
    }
    out.append(singleLineReturn ? ") " : ")\r\n");
    if(!singleLineReturn){
        out.append(indent.data(), indent.size());
        out.append("    ");
    }
    out.append(failReturn.data(), failReturn.size());
    out.append(indent.data(), indent.size());
    out.append("offset += ");
    out.append(targetPrefix.data(), targetPrefix.size());
    out.append(targetName.data(), targetName.size());
    out.append(".serializedBinarySize();\r\n");
}

void EmitBinaryTransferBody(MString& out, const SymbolTable& symbols, const Span<const FieldDecl* const> fields, const MStringView indent, const b8 isSerialize){
    MString innerIndent(out.get_allocator());
    innerIndent.append(indent.data(), indent.size());
    innerIndent.append("    ");
    const MStringView innerIndentView(innerIndent.data(), innerIndent.size());
    const MStringView limitName = isSerialize ? MStringView("bufferCapacity") : MStringView("bufferSize");
    const MStringView failReturn = isSerialize ? MStringView("return 0u;\r\n") : MStringView("return false;\r\n");

    out.append(indent.data(), indent.size());
    out.append(isSerialize ? "usize offset = 0u;\r\n" : "[[maybe_unused]] usize offset = 0u;\r\n");
    for(const FieldDecl* field : fields){
        if(!field || field->isTransient || !field->type)
            continue;
        const BinaryFieldClassInfo info = ClassifyBinarySerializedField(*field, symbols);
        if(!info.isArray && info.isNameOrAssetRef){
            EmitNameOrAssetRefStep(out, indent, "this->", field->name, info.hashMemberSuffix, false, isSerialize);
            continue;
        }
        if(!info.isArray && info.isSerializeSubObject){
            EmitSubObjectStep(out, indent, "this->", field->name, false, isSerialize);
            continue;
        }
        if(info.isArray && (info.isNameOrAssetRef || info.isSerializeSubObject)){
            out.append(indent.data(), indent.size());
            out.append(isSerialize ? "for(const auto& __elem : this->" : "for(auto& __elem : this->");
            out.append(field->name.data(), field->name.size());
            out.append("){\r\n");
            if(info.isNameOrAssetRef)
                EmitNameOrAssetRefStep(out, innerIndentView, "", "__elem", info.hashMemberSuffix, true, isSerialize);
            else
                EmitSubObjectStep(out, innerIndentView, "", "__elem", true, isSerialize);
            out.append(indent.data(), indent.size());
            out.append("}\r\n");
            continue;
        }
        out.append(indent.data(), indent.size());
        out.append("if(offset + sizeof(this->");
        out.append(field->name.data(), field->name.size());
        out.append(") > ");
        out.append(limitName.data(), limitName.size());
        out.append(")\r\n");
        out.append(indent.data(), indent.size());
        out.append("    ");
        out.append(failReturn.data(), failReturn.size());
        out.append(indent.data(), indent.size());
        if(isSerialize){
            out.append("FOUNDATION_MEMCPY(outBuffer + offset, bufferCapacity - offset, &this->");
            out.append(field->name.data(), field->name.size());
            out.append(", sizeof(this->");
            out.append(field->name.data(), field->name.size());
            out.append("));\r\n");
        }
        else{
            out.append("FOUNDATION_MEMCPY(&this->");
            out.append(field->name.data(), field->name.size());
            out.append(", sizeof(this->");
            out.append(field->name.data(), field->name.size());
            out.append("), inBuffer + offset, sizeof(this->");
            out.append(field->name.data(), field->name.size());
            out.append("));\r\n");
        }
        out.append(indent.data(), indent.size());
        out.append("offset += sizeof(this->");
        out.append(field->name.data(), field->name.size());
        out.append(");\r\n");
    }
    out.append(indent.data(), indent.size());
    out.append(isSerialize ? "return offset;\r\n" : "return true;\r\n");
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void BinarySerializationEmitter::emitSizeMethod(MString& out, const Span<const FieldDecl* const> fields)const{
    using namespace __hidden_adhd_binary_serialization_emitter;

    out.append("    [[nodiscard]] static constexpr usize serializedBinarySize()noexcept{ return 0u");
    for(const FieldDecl* field : fields){
        if(!field || field->isTransient || !field->type)
            continue;
        const BinaryFieldClassInfo info = ClassifyBinarySerializedField(*field, m_symbols);
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

void BinarySerializationEmitter::emitSerializeBody(MString& out, const Span<const FieldDecl* const> fields, const MStringView indent)const{
    __hidden_adhd_binary_serialization_emitter::EmitBinaryTransferBody(out, m_symbols, fields, indent, true);
}

void BinarySerializationEmitter::emitDeserializeBody(MString& out, const Span<const FieldDecl* const> fields, const MStringView indent)const{
    __hidden_adhd_binary_serialization_emitter::EmitBinaryTransferBody(out, m_symbols, fields, indent, false);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

