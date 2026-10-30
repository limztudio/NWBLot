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
    out.append("    enum Enum : ::");
    out.append(decl.sizeTypeName.data(), decl.sizeTypeName.size());
    out.append("{\r\n");

    usize flagBitIndex = 0u;
    b8 hasFlags = false;
    for(const EnumEntry& entry : decl.entries){
        out.append("        ");
        out.append(entry.name.data(), entry.name.size());
        if(entry.valueExpr){
            out.append(" = ");
            m_sourceEmitter.emitExpr(out, *entry.valueExpr);
        }
        else if(entry.isFlag){
            out.append(" = (static_cast<::");
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
    if(hasFlags){
        out.append("    using Flag = Enum;\r\n");
        out.append("    [[nodiscard]] constexpr Enum operator|(const Enum a, const Enum b)noexcept{ return static_cast<Enum>(static_cast<::");
        out.append(decl.sizeTypeName.data(), decl.sizeTypeName.size());
        out.append(">(a) | static_cast<::");
        out.append(decl.sizeTypeName.data(), decl.sizeTypeName.size());
        out.append(">(b)); }\r\n");
        out.append("    [[nodiscard]] constexpr Enum operator&(const Enum a, const Enum b)noexcept{ return static_cast<Enum>(static_cast<::");
        out.append(decl.sizeTypeName.data(), decl.sizeTypeName.size());
        out.append(">(a) & static_cast<::");
        out.append(decl.sizeTypeName.data(), decl.sizeTypeName.size());
        out.append(">(b)); }\r\n");
        out.append("    [[nodiscard]] constexpr Enum operator^(const Enum a, const Enum b)noexcept{ return static_cast<Enum>(static_cast<::");
        out.append(decl.sizeTypeName.data(), decl.sizeTypeName.size());
        out.append(">(a) ^ static_cast<::");
        out.append(decl.sizeTypeName.data(), decl.sizeTypeName.size());
        out.append(">(b)); }\r\n");
        out.append("    [[nodiscard]] constexpr Enum operator~(const Enum a)noexcept{ return static_cast<Enum>(~static_cast<::");
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

    const ReflectionEmitter reflectionEmitter(m_symbols);
    reflectionEmitter.emitInterfaceReflectionDeclarations(out, decl, m_arena);

    out.append("};\r\n\r\n");
}

void HeaderEmitter::emitGlobalConstDecl(MString& out, const Stmt& c)const{
    out.append("inline constexpr ");
    m_typeEmitter.emitTypeCpp(out, *c.varType);
    out.push_back(' ');
    out.append(c.name.data(), c.name.size());
    if(c.isCtorInit){
        const MStringView baseConstType = m_symbols.resolveBaseTypeName(c.varType->name);
        m_sourceEmitter.emitCtorOrCallArgs(out, baseConstType, c.arguments, {}, false, true);
        out.append(";\r\n");
    }
    else{
        out.append(" = ");
        if(c.expr)
            m_sourceEmitter.emitExpr(out, *c.expr);
        out.append(";\r\n");
    }
}

void HeaderEmitter::emitFreeFunctionDecl(MString& out, const MethodDecl& fn)const{
    const b8 emitInlineBody = (fn.isInline || !fn.templateParams.empty()) && fn.hasBody;
    m_sourceEmitter.emitOutOfClassCallable(out, nullptr, fn, emitInlineBody, true);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

