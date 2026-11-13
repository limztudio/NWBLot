// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "header_emitter.h"
#include "constructor_argument_emitter.h"

#include "reflection_emitter.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void HeaderEmitter::emitEnumDecl(MString& out, const EnumDecl& decl)const{
    const MStringView innerEnumName = decl.isFlag ? MStringView("Flag") : MStringView("Enum");

    out.append("namespace ");
    out.append(decl.name.data(), decl.name.size());
    out.append("{\r\n");
    out.append("    enum ");
    out.append(innerEnumName.data(), innerEnumName.size());
    out.append(" : ::");
    out.append(decl.sizeTypeName.data(), decl.sizeTypeName.size());
    out.append("{\r\n");

    usize flagBitIndex = 0u;
    for(const EnumEntry& entry : decl.entries){
        out.append("        ");
        out.append(entry.name.data(), entry.name.size());
        if(entry.valueExpr){
            out.append(" = ");
            const b8 wrapSizeCast = entry.valueExpr->kind != ExprKind::IntegerLiteral;
            if(wrapSizeCast){
                out.append("static_cast<::");
                out.append(decl.sizeTypeName.data(), decl.sizeTypeName.size());
                out.append(">(");
            }
            m_sourceEmitter.emitExpr(out, *entry.valueExpr);
            if(wrapSizeCast)
                out.push_back(')');
        }
        else if(decl.isFlag){
            out.append(" = (static_cast<::");
            out.append(decl.sizeTypeName.data(), decl.sizeTypeName.size());
            out.append(">(1) << ");
            AppendUnsignedDecimal(out, flagBitIndex++);
            out.append("u)");
        }
        out.append(",\r\n");
    }

    out.append("    };\r\n");
    if(decl.isFlag){
#define NWB_ADHD_EXPORT_OPERATOR_TOKEN(tokenType, symbol, isOverloadable, isAssignment, returnsBool, isBitwiseFlagOp, isPrefixUnary, isIncDec, binaryPrecedence, minMemberParams, maxMemberParams, prohibitsUnaryOverload) \
        if constexpr(isBitwiseFlagOp && !isAssignment){ \
            out.append("    [[nodiscard]] constexpr Flag operator" symbol "(const Flag a, const Flag b)noexcept{ return static_cast<Flag>(static_cast<::"); \
            out.append(decl.sizeTypeName.data(), decl.sizeTypeName.size()); \
            out.append(">(a) " symbol " static_cast<::"); \
            out.append(decl.sizeTypeName.data(), decl.sizeTypeName.size()); \
            out.append(">(b)); }\r\n"); \
        }
#include "../export_operators.h"
#undef NWB_ADHD_EXPORT_OPERATOR_TOKEN
        out.append("    [[nodiscard]] constexpr Flag operator~(const Flag a)noexcept{ return static_cast<Flag>(~static_cast<::");
        out.append(decl.sizeTypeName.data(), decl.sizeTypeName.size());
        out.append(">(a)); }\r\n");
#define NWB_ADHD_EXPORT_OPERATOR_TOKEN(tokenType, symbol, isOverloadable, isAssignment, returnsBool, isBitwiseFlagOp, isPrefixUnary, isIncDec, binaryPrecedence, minMemberParams, maxMemberParams, prohibitsUnaryOverload) \
        if constexpr(isBitwiseFlagOp && !isAssignment){ \
            out.append("    constexpr Flag& operator" symbol "=(Flag& a, const Flag b)noexcept{ a = (a " symbol " b); return a; }\r\n"); \
        }
#include "../export_operators.h"
#undef NWB_ADHD_EXPORT_OPERATOR_TOKEN
    }
    usize reflectedEntryCount = 0u;
    for(const EnumEntry& entry : decl.entries){
        if(IsEnumEntryReflected(entry))
            ++reflectedEntryCount;
    }
    out.append("    [[nodiscard]] constexpr usize reflectedEnumeratorCount()noexcept{ return ");
    AppendUnsignedDecimal(out, reflectedEntryCount);
    out.append("u; }\r\n");
    out.append("    [[nodiscard]] constexpr AStringView reflectedEnumeratorName([[maybe_unused]] const usize index)noexcept{\r\n");
    usize reflectedIndex = 0u;
    for(const EnumEntry& entry : decl.entries){
        if(!IsEnumEntryReflected(entry))
            continue;
        out.append("        if(index == ");
        AppendUnsignedDecimal(out, reflectedIndex++);
        out.append("u) return \"");
        out.append(entry.name.data(), entry.name.size());
        out.append("\";\r\n");
    }
    out.append("        return {};\r\n    }\r\n");
    if(reflectedEntryCount > 0u){
        usize namedEnumCaseCount = reflectedEntryCount;
        for(const EnumEntry& entry : decl.entries){
            if(!IsEnumEntryReflected(entry))
                continue;
            if(FindIf(entry.name.begin(), entry.name.end(), [](const MChar ch)noexcept{ return IsAsciiUpper(ch); }) != entry.name.end())
                ++namedEnumCaseCount;
        }
        out.append("    inline constexpr Array<::NamedEnumCase<");
        out.append(innerEnumName.data(), innerEnumName.size());
        out.append(">, ");
        AppendUnsignedDecimal(out, namedEnumCaseCount);
        out.append("u> s_NamedEnumCases = {{\r\n");
        for(const EnumEntry& entry : decl.entries){
            if(!IsEnumEntryReflected(entry))
                continue;
            out.append("        { \"");
            out.append(entry.name.data(), entry.name.size());
            out.append("\", ");
            out.append(entry.name.data(), entry.name.size());
            out.append(" },\r\n");
        }
        for(const EnumEntry& entry : decl.entries){
            if(!IsEnumEntryReflected(entry))
                continue;
            if(FindIf(entry.name.begin(), entry.name.end(), [](const MChar ch)noexcept{ return IsAsciiUpper(ch); }) == entry.name.end())
                continue;
            out.append("        { \"");
            AppendCamelToSnakeCase(out, entry.name);
            out.append("\", ");
            out.append(entry.name.data(), entry.name.size());
            out.append(" },\r\n");
        }
        out.append("    }};\r\n");
        out.append("    [[nodiscard]] constexpr Span<const ::NamedEnumCase<");
        out.append(innerEnumName.data(), innerEnumName.size());
        out.append(">> namedEnumCases()noexcept{ return s_NamedEnumCases; }\r\n");
    }
    else{
        out.append("    [[nodiscard]] constexpr Span<const ::NamedEnumCase<");
        out.append(innerEnumName.data(), innerEnumName.size());
        out.append(">> namedEnumCases()noexcept{ return {}; }\r\n");
    }
    out.append("    [[nodiscard]] inline Expected<");
    out.append(innerEnumName.data(), innerEnumName.size());
    out.append("> parseNamedEnum(const AStringView text)noexcept{ return ::ParseNamedEnumText<");
    out.append(innerEnumName.data(), innerEnumName.size());
    out.append(">(text, namedEnumCases()); }\r\n");
    out.append("    [[nodiscard]] constexpr AStringView toNamedEnumText([[maybe_unused]] const ");
    out.append(innerEnumName.data(), innerEnumName.size());
    out.append(" value)noexcept{\r\n");
    out.append("        for(const ::NamedEnumCase<");
    out.append(innerEnumName.data(), innerEnumName.size());
    out.append(">& item : namedEnumCases()){\r\n");
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
    const b8 isAssetInterface = !decl.assetTypeName.empty();
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
    else if(isAssetInterface){
        out.append(" : public ::NWB::Core::Assets::IAsset");
    }
    out.append("{\r\npublic:\r\n");
    if(isAssetInterface){
        out.append("    NWB_DEFINE_ASSET_TYPE(\"");
        out.append(decl.assetTypeName.data(), decl.assetTypeName.size());
        out.append("\")\r\n\r\n");
        out.append("protected:\r\n");
        out.append("    explicit ");
        out.append(decl.name.data(), decl.name.size());
        out.append("(const ::Name& assetType, const ::Name& virtualPath = ::s_NameNone)noexcept\r\n");
        out.append("        : ::NWB::Core::Assets::IAsset(assetType, virtualPath)\r\n");
        out.append("    {}\r\n");
        out.append("    ");
        out.append(decl.name.data(), decl.name.size());
        out.append("(const ");
        out.append(decl.name.data(), decl.name.size());
        out.append("&) = default;\r\n");
        out.append("    ");
        out.append(decl.name.data(), decl.name.size());
        out.append("(");
        out.append(decl.name.data(), decl.name.size());
        out.append("&&)noexcept = default;\r\n");
        out.append("    ");
        out.append(decl.name.data(), decl.name.size());
        out.append("& operator=(const ");
        out.append(decl.name.data(), decl.name.size());
        out.append("&) = default;\r\n");
        out.append("    ");
        out.append(decl.name.data(), decl.name.size());
        out.append("& operator=(");
        out.append(decl.name.data(), decl.name.size());
        out.append("&&)noexcept = default;\r\n\r\n");
        out.append("public:\r\n");
        out.append("    virtual ~");
        out.append(decl.name.data(), decl.name.size());
        out.append("()noexcept override = default;\r\n\r\n");
    }
    else{
        out.append("    virtual ~");
        out.append(decl.name.data(), decl.name.size());
        out.append("() = default;\r\n\r\n");
    }

    for(const MethodDecl* method : decl.methods){
        if(!method)
            continue;
        out.append("    virtual ");
        m_typeEmitter.emitMethodSignature(out, nullptr, *method, true);
        out.append(" = 0;\r\n");
    }

    const ReflectionEmitter reflectionEmitter(m_symbols, m_typeEmitter);
    reflectionEmitter.emitInterfaceReflectionDeclarations(decl, m_arena, out);

    out.append("};\r\n\r\n");
}

void HeaderEmitter::emitGlobalConstDecl(MString& out, const Stmt& c)const{
    out.append("inline constexpr ");
    m_typeEmitter.emitTypeCpp(out, *c.varType);
    out.push_back(' ');
    out.append(c.name.data(), c.name.size());
    if(c.isCtorInit){
        const MStringView baseConstType = m_symbols.resolveBaseTypeName(c.varType->name);
        ConstructorArgumentEmitter(m_sourceEmitter).emit(out, baseConstType, c.arguments, {}, false, true);
        out.append(";\r\n");
    }
    else if(c.expr && c.expr->kind == ExprKind::StringLiteral && c.varType && c.varType->name != "StringView" && c.varType->name != "Name"){
        out.push_back('(');
        m_sourceEmitter.emitExpr(out, *c.expr);
        out.append(");\r\n");
    }
    else{
        out.append(" = ");
        if(c.expr)
            m_sourceEmitter.emitExpr(out, *c.expr);
        out.append(";\r\n");
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

