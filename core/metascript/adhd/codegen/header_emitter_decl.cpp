// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "header_emitter.h"


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
    usize reflectedEntryCount = 0u;
    for(const EnumEntry& entry : decl.entries){
        if(entry.name != "kCount")
            ++reflectedEntryCount;
    }
    out.append("    [[nodiscard]] constexpr usize reflectedEnumeratorCount()noexcept{ return ");
    AppendAdhdUnsigned(out, reflectedEntryCount);
    out.append("u; }\r\n");
    out.append("    [[nodiscard]] constexpr AStringView reflectedEnumeratorName([[maybe_unused]] const usize index)noexcept{\r\n");
    usize reflectedIndex = 0u;
    for(const EnumEntry& entry : decl.entries){
        if(entry.name == "kCount")
            continue;
        out.append("        if(index == ");
        AppendAdhdUnsigned(out, reflectedIndex++);
        out.append("u) return \"");
        out.append(entry.name.data(), entry.name.size());
        out.append("\";\r\n");
    }
    out.append("        return {};\r\n    }\r\n");
    if(reflectedEntryCount > 0u){
        out.append("    inline constexpr ::NamedEnumCase<Enum> s_NamedEnumCases[] = {\r\n");
        for(const EnumEntry& entry : decl.entries){
            if(entry.name == "kCount")
                continue;
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

void HeaderEmitter::emitGlobalConstDecl(MString& out, const Stmt& c)const{
    out.append("inline constexpr ");
    m_typeEmitter.emitTypeCpp(out, *c.varType);
    out.push_back(' ');
    out.append(c.name.data(), c.name.size());
    if(c.isCtorInit){
        const MStringView baseConstType = m_symbols.resolveBaseTypeName(c.varType->name);
        if(!baseConstType.empty() && m_symbols.isSimdMatrixType(baseConstType)){
            if(c.arguments.empty()){
                out.append("{};\r\n");
            }
            else{
                out.append("{ .raw = { ");
                for(usize i = 0u; i < c.arguments.size(); ++i){
                    if(i > 0u)
                        out.append(", ");
                    if(c.arguments[i])
                        m_sourceEmitter.emitExpr(out, *c.arguments[i]);
                }
                out.append(" } };\r\n");
            }
        }
        else if(c.arguments.empty()){
            out.append("{};\r\n");
        }
        else{
            out.push_back('(');
            u32 ctorVecLanes = 0u;
            const bool isScalarSplatCtor = !baseConstType.empty()
                && (m_symbols.isSimdVectorType(baseConstType, ctorVecLanes) || m_symbols.isUnalignedVectorType(baseConstType, ctorVecLanes))
                && c.arguments.size() == 1u && c.arguments[0]
                && c.arguments[0]->resolvedSimdType.empty();
            if(isScalarSplatCtor){
                for(u32 lane = 0u; lane < ctorVecLanes; ++lane){
                    if(lane > 0u)
                        out.append(", ");
                    m_sourceEmitter.emitExpr(out, *c.arguments[0]);
                }
            }
            else{
                for(usize i = 0u; i < c.arguments.size(); ++i){
                    if(i > 0u)
                        out.append(", ");
                    if(c.arguments[i])
                        m_sourceEmitter.emitExpr(out, *c.arguments[i]);
                }
            }
            out.append(");\r\n");
        }
    }
    else{
        out.append(" = ");
        if(c.expr)
            m_sourceEmitter.emitExpr(out, *c.expr);
        out.append(";\r\n");
    }
}

void HeaderEmitter::emitFreeFunctionDecl(MString& out, const MethodDecl& fn)const{
    const bool emitInlineBody = fn.isInline || !fn.templateParams.empty();
    m_typeEmitter.emitTemplateHeaderCpp(out, fn.templateParams);
    if(emitInlineBody)
        out.append("inline ");

    m_typeEmitter.emitMethodSignature(out, nullptr, fn, false);
    if(emitInlineBody && fn.hasBody){
        out.append("{\r\n");
        m_sourceEmitter.emitMethodBodyBlock(out, fn, 1u);
        out.append("}\r\n\r\n");
    }
    else{
        out.append(";\r\n\r\n");
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
