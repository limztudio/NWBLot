// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "reflection_emitter.h"

#include "binary_serialization_emitter.h"
#include "nwb_reflection_emitter.h"

#include <core/metascript/adhd/sema/asset_type_analyzer.h>
#include <core/metascript/adhd/sema/type_resolver.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_reflection_emitter{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CountReflectedFields(const Span<const FieldDecl* const> fields, const SymbolTable& symbols, usize& outNonTransientCount, usize& outAssetRefCount)noexcept{
    outNonTransientCount = 0u;
    outAssetRefCount = 0u;
    const AssetTypeAnalyzer assetTypes(symbols);
    for(const FieldDecl* field : fields){
        if(!field || !IsFieldReflected(*field))
            continue;
        ++outNonTransientCount;
        if(assetTypes.isAssetDependencyType(field->type))
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
    AppendUnsignedDecimal(out, count);
    out.append("u; }\r\n");
}

void EmitFieldNameLookupBody(MString& out, const Span<const FieldDecl* const> fields, const SymbolTable& symbols, const b8 assetDepOnly, const MStringView indent){
    usize idx = 0u;
    for(const FieldDecl* field : fields){
        if(!field || !IsFieldReflected(*field))
            continue;
        if(assetDepOnly && !AssetTypeAnalyzer(symbols).isAssetDependencyType(field->type))
            continue;
        out.append(indent.data(), indent.size());
        out.append("if(index == ");
        AppendUnsignedDecimal(out, idx++);
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
        AppendUnsignedDecimal(out, idx++);
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
    if(IsAssetHandleType(elemName)){
        out.append(isMapContainer ? "for([[maybe_unused]] const auto& [__k, __dep] : " : "for(const auto& __dep : ");
        out.append(containerDerefPrefix.data(), containerDerefPrefix.size());
        out.append(fieldName.data(), fieldName.size());
        out.append("){ if(__dep.valid()) callback(__dep.virtualPath); }");
    }
    else{
        out.append(isMapContainer ? "for([[maybe_unused]] const auto& [__k, __item] : " : "for(const auto& __item : ");
        out.append(containerDerefPrefix.data(), containerDerefPrefix.size());
        out.append(fieldName.data(), fieldName.size());
        out.append(") __item.forEachAssetDependency(callback);");
    }
    out.append(isOptional ? " }\r\n" : "\r\n");
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void ReflectionEmitter::emitInterfaceReflectionDeclarations(const InterfaceDecl& decl, MetaArena& arena, MString& out)const{
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
    emitClassReflectionMembers(out, decl, decl.orderedFields, false);
}

void ReflectionEmitter::emitInlineClassReflection(MString& out, const ClassDecl& decl, const Span<const FieldDecl* const> fields)const{
    emitClassReflectionMembers(out, decl, fields, true);
}

void ReflectionEmitter::emitClassReflectionDefinitions(MString& out, const ClassDecl& decl)const{
    using namespace __hidden_adhd_reflection_emitter;

    const MVector<FieldDecl*>& activeFields = decl.orderedFields;

    if(decl.isSerialize){
        const BinarySerializationEmitter binaryEmitter(m_symbols);
        out.append("usize ");
        out.append(decl.name.data(), decl.name.size());
        out.append("::serializeBinary([[maybe_unused]] u8* outBuffer, [[maybe_unused]] const usize bufferCapacity)const noexcept{\r\n");
        binaryEmitter.emitSerializeBody(out, activeFields, "    ");
        out.append("}\r\n\r\n");

        out.append("b8 ");
        out.append(decl.name.data(), decl.name.size());
        out.append("::deserializeBinary([[maybe_unused]] const u8* inBuffer, [[maybe_unused]] const usize bufferSize)noexcept{\r\n");
        binaryEmitter.emitDeserializeBody(out, activeFields, "    ");
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

void ReflectionEmitter::emitClassReflectionMembers(MString& out, const ClassDecl& decl, const Span<const FieldDecl* const> fields, const b8 inlineBodies)const{
    using namespace __hidden_adhd_reflection_emitter;

    out.append("public:\r\n");
    EmitAssetTypeNameDecl(out, decl.assetTypeName);

    if(decl.isSerialize){
        const BinarySerializationEmitter binaryEmitter(m_symbols);
        binaryEmitter.emitSizeMethod(out, fields);
        if(inlineBodies){
            out.append("    [[nodiscard]] usize serializeBinary([[maybe_unused]] u8* outBuffer, [[maybe_unused]] const usize bufferCapacity)const noexcept{\r\n");
            binaryEmitter.emitSerializeBody(out, fields, "        ");
            out.append("    }\r\n");

            out.append("    [[nodiscard]] b8 deserializeBinary([[maybe_unused]] const u8* inBuffer, [[maybe_unused]] const usize bufferSize)noexcept{\r\n");
            binaryEmitter.emitDeserializeBody(out, fields, "        ");
            out.append("    }\r\n");
        }
        else{
            out.append("    [[nodiscard]] usize serializeBinary(u8* outBuffer, usize bufferCapacity)const noexcept;\r\n");
            out.append("    [[nodiscard]] b8 deserializeBinary(const u8* inBuffer, usize bufferSize)noexcept;\r\n");
        }
    }

    if(!inlineBodies && !decl.isSerialize && !decl.isReflect && decl.assetTypeName.empty())
        return;

    usize nonTransientCount = 0u;
    usize assetRefCount = 0u;
    CountReflectedFields(fields, m_symbols, nonTransientCount, assetRefCount);
    const usize methodCount = CountReflectedMethods(decl.methods);

    EmitReflectedCountMethod(out, "reflectedFieldCount", nonTransientCount);
    if(inlineBodies){
        out.append("    [[nodiscard]] static AStringView reflectedFieldName([[maybe_unused]] const usize index)noexcept{\r\n");
        EmitFieldNameLookupBody(out, fields, m_symbols, false, "        ");
        out.append("    }\r\n");
    }
    else{
        out.append("    [[nodiscard]] static AStringView reflectedFieldName(usize index)noexcept;\r\n");
    }

    EmitReflectedCountMethod(out, "reflectedMethodCount", methodCount);
    if(inlineBodies){
        out.append("    [[nodiscard]] static AStringView reflectedMethodName([[maybe_unused]] const usize index)noexcept{\r\n");
        EmitMethodNameLookupBody(out, decl.methods, "        ");
        out.append("    }\r\n");
    }
    else{
        out.append("    [[nodiscard]] static AStringView reflectedMethodName(usize index)noexcept;\r\n");
    }

    EmitReflectedCountMethod(out, "assetDependencyFieldCount", assetRefCount);
    if(inlineBodies){
        out.append("    [[nodiscard]] static AStringView assetDependencyFieldName([[maybe_unused]] const usize index)noexcept{\r\n");
        EmitFieldNameLookupBody(out, fields, m_symbols, true, "        ");
        out.append("    }\r\n");
    }
    else{
        out.append("    [[nodiscard]] static AStringView assetDependencyFieldName(usize index)noexcept;\r\n");
    }

    const NwbReflectionEmitter nwbEmitter(m_symbols);
    nwbEmitter.emitDeserializeNwbMethod(out, fields);
    emitAssetDependencyMethods(out, fields);
    nwbEmitter.emitExtractNwbAssetDependenciesMethod(out, fields);
}

void ReflectionEmitter::emitAssetDependencyMethods(MString& out, const Span<const FieldDecl* const> fields)const{
    out.append("    template<typename DependencyCallback>\r\n");
    out.append("    void forEachAssetDependency([[maybe_unused]] DependencyCallback&& callback)const noexcept{\r\n");
    for(const FieldDecl* field : fields){
        if(!field || !IsFieldReflected(*field) || !field->type || !AssetTypeAnalyzer(m_symbols).isAssetDependencyType(field->type))
            continue;
        b8 isOptional = false;
        MStringView tname;
        const TypeRef* effectiveTypeRef = TypeResolver(m_symbols).unwrapOptionalFieldType(field->type, isOptional, tname);
        const MStringView memberDotOrArrow = isOptional ? "->" : ".";
        const MStringView containerDerefPrefix = isOptional ? "*this->" : "this->";

        if(IsAssetHandleType(tname)){
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
            if(IsAssetHandleType(elemName) || m_symbols.findClass(elemName))
                __hidden_adhd_reflection_emitter::EmitContainerAssetDependencyLoop(out, field->name, elemName, containerDerefPrefix, isOptional, false);
        }
        else if(IsReflectedMapContainerType(tname) && effectiveTypeRef && effectiveTypeRef->templateArgs.size() >= 2u && effectiveTypeRef->templateArgs[1].type){
            const MStringView valName = m_symbols.resolveBaseTypeName(effectiveTypeRef->templateArgs[1].type->name);
            if(IsAssetHandleType(valName) || m_symbols.findClass(valName))
                __hidden_adhd_reflection_emitter::EmitContainerAssetDependencyLoop(out, field->name, valName, containerDerefPrefix, isOptional, true);
        }
    }
    out.append("    }\r\n");
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

