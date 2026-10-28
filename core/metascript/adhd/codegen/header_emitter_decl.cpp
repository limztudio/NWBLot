// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "header_emitter.h"

#include "reflection_emitter.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void HeaderEmitter::emitEnumDecl(MString& out, const EnumDecl& decl)const{

    out.append("namespace ");
    out.append(decl.name.data(), decl.name.size());
    out.append("{\r\n");
    out.append("    enum Enum : ");
    out.append(decl.sizeTypeName.data(), decl.sizeTypeName.size());
    out.append("{\r\n");

    usize flagBitIndex = 0u;
    bool hasFlags = false;
    for(const EnumEntry& entry : decl.entries){
        out.append("        ");
        out.append(entry.name.data(), entry.name.size());
        if(entry.valueExpr){
            out.append(" = ");
            m_sourceEmitter.emitExpr(out, *entry.valueExpr);
        }
        else if(entry.isFlag){
            hasFlags = true;
            out.append(" = (static_cast<");
            out.append(decl.sizeTypeName.data(), decl.sizeTypeName.size());
            out.append(">(1) << ");
            AppendAdhdUnsigned(out, flagBitIndex++);
            out.push_back(')');
        }
        out.append(",\r\n");
        if(entry.isFlag)
            hasFlags = true;
    }

    out.append("    };\r\n");
    out.append("    using Flag = Enum;\r\n");
    if(hasFlags){
        out.append("    [[nodiscard]] constexpr Enum operator|(const Enum a, const Enum b)noexcept{ return static_cast<Enum>(static_cast<");
        out.append(decl.sizeTypeName.data(), decl.sizeTypeName.size());
        out.append(">(a) | static_cast<");
        out.append(decl.sizeTypeName.data(), decl.sizeTypeName.size());
        out.append(">(b)); }\r\n");
        out.append("    [[nodiscard]] constexpr Enum operator&(const Enum a, const Enum b)noexcept{ return static_cast<Enum>(static_cast<");
        out.append(decl.sizeTypeName.data(), decl.sizeTypeName.size());
        out.append(">(a) & static_cast<");
        out.append(decl.sizeTypeName.data(), decl.sizeTypeName.size());
        out.append(">(b)); }\r\n");
        out.append("    [[nodiscard]] constexpr Enum operator^(const Enum a, const Enum b)noexcept{ return static_cast<Enum>(static_cast<");
        out.append(decl.sizeTypeName.data(), decl.sizeTypeName.size());
        out.append(">(a) ^ static_cast<");
        out.append(decl.sizeTypeName.data(), decl.sizeTypeName.size());
        out.append(">(b)); }\r\n");
        out.append("    [[nodiscard]] constexpr Enum operator~(const Enum a)noexcept{ return static_cast<Enum>(~static_cast<");
        out.append(decl.sizeTypeName.data(), decl.sizeTypeName.size());
        out.append(">(a)); }\r\n");
        out.append("    constexpr Enum& operator|=(Enum& a, const Enum b)noexcept{ a = (a | b); return a; }\r\n");
        out.append("    constexpr Enum& operator&=(Enum& a, const Enum b)noexcept{ a = (a & b); return a; }\r\n");
        out.append("    constexpr Enum& operator^=(Enum& a, const Enum b)noexcept{ a = (a ^ b); return a; }\r\n");
    }
    out.append("    [[nodiscard]] constexpr usize reflectedEnumeratorCount()noexcept{ return ");
    AppendAdhdUnsigned(out, decl.entries.size());
    out.append("u; }\r\n");
    out.append("    [[nodiscard]] constexpr AStringView reflectedEnumeratorName([[maybe_unused]] const usize index)noexcept{\r\n");
    for(usize i = 0u; i < decl.entries.size(); ++i){
        out.append("        if(index == ");
        AppendAdhdUnsigned(out, i);
        out.append("u) return \"");
        out.append(decl.entries[i].name.data(), decl.entries[i].name.size());
        out.append("\";\r\n");
    }
    out.append("        return {};\r\n    }\r\n");
    if(!decl.entries.empty()){
        out.append("    inline constexpr ::NamedEnumCase<Enum> s_NamedEnumCases[] = {\r\n");
        for(const EnumEntry& entry : decl.entries){
            out.append("        { \"");
            out.append(entry.name.data(), entry.name.size());
            out.append("\", ");
            out.append(entry.name.data(), entry.name.size());
            out.append(" },\r\n");
        }
        out.append("    };\r\n");
        out.append("    [[nodiscard]] constexpr Span<const ::NamedEnumCase<Enum>> namedEnumCases()noexcept{ return s_NamedEnumCases; }\r\n");
    }
    else{
        out.append("    [[nodiscard]] constexpr Span<const ::NamedEnumCase<Enum>> namedEnumCases()noexcept{ return {}; }\r\n");
    }
    out.append("    [[nodiscard]] inline Expected<Enum> parseNamedEnum(const AStringView text)noexcept{ return ::ParseNamedEnumText<Enum>(text, namedEnumCases()); }\r\n");
    out.append("    [[nodiscard]] constexpr AStringView toNamedEnumText([[maybe_unused]] const Enum value)noexcept{\r\n");
    out.append("        for(const ::NamedEnumCase<Enum>& item : namedEnumCases()){\r\n");
    out.append("            if(item.value == value)\r\n");
    out.append("                return item.text;\r\n");
    out.append("        }\r\n");
    out.append("        return {};\r\n    }\r\n");
    out.append("};\r\n\r\n");
}

void HeaderEmitter::emitTypeAliasDecl(MString& out, const TypeAliasDecl& decl)const{
    m_typeEmitter.emitTemplateHeaderCpp(out, decl.templateParams);
    out.append("using ");
    out.append(decl.name.data(), decl.name.size());
    out.append(" = ");
    if(decl.aliasedType)
        m_typeEmitter.emitTypeCpp(out, *decl.aliasedType);
    out.append(";\r\n\r\n");
}

void HeaderEmitter::emitInterfaceDecl(MString& out, const InterfaceDecl& decl)const{
    m_typeEmitter.emitTemplateHeaderCpp(out, decl.templateParams);
    out.append("class ");
    out.append(decl.name.data(), decl.name.size());
    if(!decl.baseInterfaces.empty()){
        out.append(" : ");
        for(usize i = 0u; i < decl.baseInterfaces.size(); ++i){
            if(i > 0u)
                out.append(", ");
            out.append("public ");
            if(decl.baseInterfaces[i])
                m_typeEmitter.emitTypeCpp(out, *decl.baseInterfaces[i]);
        }
    }
    out.append("{\r\npublic:\r\n");
    out.append("    virtual ~");
    out.append(decl.name.data(), decl.name.size());
    out.append("() = default;\r\n\r\n");

    for(const MethodDecl* method : decl.methods){
        if(!method)
            continue;
        out.append("    virtual ");
        m_typeEmitter.emitMethodSignature(out, nullptr, *method, true);
        out.append(" = 0;\r\n");
    }

    if(!decl.assetTypeName.empty()){
        out.append("\r\n    inline static constexpr AStringView s_AssetTypeName = \"");
        out.append(decl.assetTypeName.data(), decl.assetTypeName.size());
        out.append("\";\r\n");
    }
    MVector<MethodDecl*> allMethods(m_arena);
    TypeRef selfIfaceRef(m_arena);
    selfIfaceRef.name = decl.name;
    m_symbols.collectAllInterfaceMethods(selfIfaceRef, allMethods);
    out.append("    [[nodiscard]] static constexpr usize reflectedMethodCount()noexcept{ return ");
    AppendAdhdUnsigned(out, allMethods.size());
    out.append("u; }\r\n");
    out.append("    [[nodiscard]] static constexpr AStringView reflectedMethodName([[maybe_unused]] const usize index)noexcept{\r\n");
    for(usize i = 0u; i < allMethods.size(); ++i){
        if(!allMethods[i])
            continue;
        out.append("        if(index == ");
        AppendAdhdUnsigned(out, i);
        out.append("u) return \"");
        out.append(allMethods[i]->name.data(), allMethods[i]->name.size());
        out.append("\";\r\n");
    }
    out.append("        return {};\r\n    }\r\n");

    out.append("};\r\n\r\n");
}

void HeaderEmitter::emitClassDecl(MString& out, const ClassDecl& decl)const{
    ReflectionEmitter reflectionEmitter(m_symbols);

    if(decl.isSerialize)
        out.append("#pragma pack(push, 1)\r\n");

    m_typeEmitter.emitTemplateHeaderCpp(out, decl.templateParams);
    out.append("class ");
    out.append(decl.name.data(), decl.name.size());
    out.append(" final");

    if(!decl.implementedInterfaces.empty()){
        out.append(" : ");
        for(usize i = 0u; i < decl.implementedInterfaces.size(); ++i){
            if(i > 0u)
                out.append(", ");
            out.append("public ");
            if(decl.implementedInterfaces[i])
                m_typeEmitter.emitTypeCpp(out, *decl.implementedInterfaces[i]);
        }
    }
    out.append("{\r\n");

    emitClassMethodsCpp(out, decl, decl.orderedFields, !decl.templateParams.empty());

    if(!decl.orderedFields.empty())
        out.append("\r\n");

    emitClassFieldsCpp(out, decl.orderedFields);

    if(decl.isSerialize || decl.isReflect || !decl.assetTypeName.empty()){
        out.append("\r\n");
        if(decl.templateParams.empty())
            reflectionEmitter.emitClassReflectionDeclarations(out, decl);
        else
            reflectionEmitter.emitInlineClassReflection(out, decl, decl.orderedFields);
    }

    out.append("};\r\n");

    if(decl.isSerialize)
        out.append("#pragma pack(pop)\r\n");

    out.append("\r\n");
}

void HeaderEmitter::emitClassTemplateSpecialization(MString& out, const ClassTemplateInstantiation& inst)const{
    if(!inst.templateDecl)
        return;

    const ClassDecl& decl = *inst.templateDecl;
    ReflectionEmitter reflectionEmitter(m_symbols);

    if(decl.isSerialize)
        out.append("#pragma pack(push, 1)\r\n");

    out.append("template<>\r\nclass ");
    out.append(decl.name.data(), decl.name.size());
    m_typeEmitter.emitTypeArgListCpp(out, inst.templateArgs);
    out.append(" final");

    if(!decl.implementedInterfaces.empty()){
        out.append(" : ");
        for(usize i = 0u; i < decl.implementedInterfaces.size(); ++i){
            if(i > 0u)
                out.append(", ");
            out.append("public ");
            if(decl.implementedInterfaces[i]){
                const TypeRef* resolvedIface = m_symbols.substituteTypeParams(
                    *decl.implementedInterfaces[i],
                    decl.templateParams,
                    inst.templateArgs
                );
                m_typeEmitter.emitTypeCpp(out, *resolvedIface);
            }
        }
    }
    out.append("{\r\n");

    const usize aliasCount = decl.templateParams.size() < inst.templateArgs.size()
        ? decl.templateParams.size()
        : inst.templateArgs.size();
    if(aliasCount > 0u){
        out.append("private:\r\n");
        for(usize i = 0u; i < aliasCount; ++i){
            const TemplateParamDecl& param = decl.templateParams[i];
            const TypeArg& arg = inst.templateArgs[i];
            if(param.constValueType){
                const MStringView valText = arg.type ? arg.type->name : arg.constantText;
                out.append("    static constexpr ");
                m_typeEmitter.emitTypeCpp(out, *param.constValueType);
                out.push_back(' ');
                out.append(param.name.data(), param.name.size());
                out.append(" = ");
                out.append(valText.data(), valText.size());
                out.append(";\r\n");
            }
            else if(arg.type){
                out.append("    using ");
                out.append(param.name.data(), param.name.size());
                out.append(" = ");
                m_typeEmitter.emitTypeCpp(out, *arg.type);
                out.append(";\r\n");
            }
        }
        out.append("\r\n");
    }

    emitClassMethodsCpp(out, decl, inst.orderedFields, true);

    if(!inst.orderedFields.empty() && !decl.methods.empty())
        out.append("\r\n");

    emitClassFieldsCpp(out, inst.orderedFields);

    out.append("\r\n");
    reflectionEmitter.emitInlineClassReflection(out, decl, inst.orderedFields);

    out.append("};\r\n");
    if(decl.isSerialize)
        out.append("#pragma pack(pop)\r\n");
    out.append("\r\n");
}

void HeaderEmitter::emitClassMethodsCpp(
    MString& out,
    const ClassDecl& decl,
    const MVector<FieldDecl*>& orderedFields,
    const bool forceInlineBody
)const{
    if(decl.needsClassArena){
        out.append("private:\r\n");
        out.append("    inline static ::NWB::Core::Alloc::GlobalArena s_ClassArena{::Name(\"");
        out.append(decl.name.data(), decl.name.size());
        out.append("\")};\r\n\r\n");
    }

    for(const MethodDecl* method : decl.methods){
        if(!method)
            continue;
        if(method->access == AccessSpecifier::Public)
            out.append("public:\r\n    ");
        else if(method->access == AccessSpecifier::Protected)
            out.append("protected:\r\n    ");
        else
            out.append("private:\r\n    ");

        if(!method->templateParams.empty()){
            m_typeEmitter.emitTemplateHeaderCpp(out, method->templateParams);
            out.append("    ");
        }
        const bool emitInlineBody = forceInlineBody || method->isInline || !method->templateParams.empty();
        if(method->isInline)
            out.append("NWB_FORCE_INLINE ");
        else if(method->isNoInline)
            out.append("NWB_NO_INLINE ");

        m_typeEmitter.emitMethodSignature(out, &decl, *method, true);
        if(emitInlineBody && method->hasBody){
            if(method->isConstructor)
                m_sourceEmitter.emitConstructorNotNullInitializers(out, orderedFields, *method);
            out.append("{\r\n");
            m_sourceEmitter.emitMethodBodyBlock(out, *method, 2u);
            out.append("    }\r\n");
        }
        else{
            out.append(";\r\n");
        }
    }
}

void HeaderEmitter::emitClassFieldsCpp(MString& out, const MVector<FieldDecl*>& orderedFields)const{
    for(const FieldDecl* field : orderedFields){
        if(!field || !field->type)
            continue;
        if(field->access == AccessSpecifier::Public)
            out.append("public:\r\n    ");
        else if(field->access == AccessSpecifier::Protected)
            out.append("protected:\r\n    ");
        else
            out.append("private:\r\n    [[maybe_unused]] ");

        m_typeEmitter.emitTypeCpp(out, *field->type);
        out.push_back(' ');
        out.append(field->name.data(), field->name.size());
        const ParamPassingMode::Enum fieldRefMode = m_symbols.resolveRefMode(field->type);
        if(field->defaultExpr){
            if(fieldRefMode != ParamPassingMode::None){
                out.append(" = ::MakeNotNull(&(");
                m_sourceEmitter.emitExpr(out, *field->defaultExpr);
                out.append("))");
            }
            else if(field->usesClassArena && field->defaultExpr->kind == ExprKind::StringLiteral){
                out.push_back('{');
                m_sourceEmitter.emitExpr(out, *field->defaultExpr);
                out.append(", s_ClassArena}");
            }
            else{
                out.append(" = ");
                m_sourceEmitter.emitExpr(out, *field->defaultExpr);
            }
        }
        else if(field->hasCtorInit){
            out.push_back('{');
            for(usize i = 0u; i < field->ctorArgs.size(); ++i){
                if(i > 0u)
                    out.append(", ");
                if(field->ctorArgs[i])
                    m_sourceEmitter.emitExpr(out, *field->ctorArgs[i]);
            }
            if(field->usesClassArena){
                if(!field->ctorArgs.empty())
                    out.append(", ");
                out.append("s_ClassArena");
            }
            out.push_back('}');
        }
        else if(field->usesClassArena){
            out.append("{s_ClassArena}");
        }
        else if(fieldRefMode == ParamPassingMode::None
            && (m_symbols.resolveBaseTypeName(field->type->name) == "Array"
                || m_symbols.findEnum(m_symbols.resolveBaseTypeName(field->type->name)) != nullptr))
        {
            out.append("{}");
        }
        out.append(";\r\n");
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

