// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "ast_types.h"
#include "builtin_type_semantics.h"
#include "keyword_semantics.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct Stmt;
struct Expr;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct LayoutQuery{
    MStringView keyword;
    MStringView cppPrefix;
    MStringView cppSuffix;
    b8 booleanResult;
};

namespace LayoutQueryKind{
    enum Enum : u8{
#define NWB_ADHD_EXPORT_LAYOUT_QUERY(adhdName, enumName, cppPrefix, cppSuffix, booleanResult) enumName,
#include "../export_keywords.h"
#undef NWB_ADHD_EXPORT_LAYOUT_QUERY
        kCount,
    };
}

inline constexpr Array<LayoutQuery, LayoutQueryKind::kCount> s_LayoutQueries = {{
#define NWB_ADHD_EXPORT_LAYOUT_QUERY(adhdName, enumName, cppPrefix, cppSuffix, booleanResult) LayoutQuery{ adhdName, cppPrefix, cppSuffix, booleanResult },
#include "../export_keywords.h"
#undef NWB_ADHD_EXPORT_LAYOUT_QUERY
}};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ExprKind{
    enum Enum : u8{
        IntegerLiteral,
        FloatLiteral,
        DoubleLiteral,
        BoolLiteral,
        StringLiteral,
        Identifier,
        ThisMember,
        MemberAccess,
        ScopeAccess,
        Call,
        Index,
        Unary,
        Binary,
        Cast,
        BitCast,
        LayoutQuery,
        Lambda,
    };
};

[[nodiscard]] constexpr b8 IsLiteralExprKind(const ExprKind::Enum kind)noexcept{
    if(kind == ExprKind::BoolLiteral)
        return true;
#define NWB_ADHD_EXPORT_LITERAL_TOKEN(litTokenType, exprKind, adhdTypeName) \
    if(kind == exprKind) return true;
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_LITERAL_TOKEN
    return false;
}

[[nodiscard]] constexpr b8 IsAlwaysRvalueExprKind(const ExprKind::Enum kind)noexcept{
    if(IsLiteralExprKind(kind))
        return true;
    switch(kind){
    case ExprKind::ScopeAccess:
    case ExprKind::Binary:
    case ExprKind::Cast:
    case ExprKind::BitCast:
    case ExprKind::LayoutQuery:
    case ExprKind::Lambda:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] constexpr b8 TryParseCastKeyword(const MStringView text, ExprKind::Enum& outKind)noexcept{
#define NWB_ADHD_EXPORT_CAST_KEYWORD(adhdKeyword, exprKind) \
    if(text == adhdKeyword){ outKind = exprKind; return true; }
#include "../export_keywords.h"
#undef NWB_ADHD_EXPORT_CAST_KEYWORD
    return false;
}

[[nodiscard]] constexpr b8 TryMapLiteralTokenToExprKind(const TokenType::Enum tokenType, ExprKind::Enum& outKind)noexcept{
#define NWB_ADHD_EXPORT_LITERAL_TOKEN(litTokenType, exprKind, adhdTypeName) \
    if(tokenType == litTokenType){ outKind = exprKind; return true; }
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_LITERAL_TOKEN
    return false;
}

[[nodiscard]] constexpr MStringView LookupFixedLiteralExprTypeName(const ExprKind::Enum kind)noexcept{
    if(kind == ExprKind::BoolLiteral)
        return BuiltinTypeSemantics::BoolScalarTypeName();
#define NWB_ADHD_EXPORT_LITERAL_TOKEN(litTokenType, exprKind, adhdTypeName) \
    if(kind == exprKind) return adhdTypeName;
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_LITERAL_TOKEN
    return {};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct LambdaCapture{
    ParamPassingMode::Enum mode = ParamPassingMode::None;
    MStringView name;
    SourceSpan span;
    b8 isGlobalConstant = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ParamDecl{
    ParamPassingMode::Enum mode = ParamPassingMode::None;
    TypeRef* type = nullptr;
    MStringView name;
    SourceSpan span;
    b8 retainsBorrow = false;
    b8 cppConstReference = false;
    b8 forwardsArgument = false;
    b8 copiesArgument = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct Expr{
    ExprKind::Enum kind = ExprKind::Identifier;
    LayoutQueryKind::Enum layoutQuery = LayoutQueryKind::kCount;
    SourceSpan span;
    MStringView text;
    MStringView resolvedSimdType;
    MStringView resolvedUnalignedType;
    MStringView resolvedScalarType;
    MStringView resolvedCppNamespace;
    MStringView resolvedCppExportName;
    Expr* lhs = nullptr;
    Expr* rhs = nullptr;
    TypeRef* targetType = nullptr;
    MVector<TypeArg> callTemplateArgs;
    MVector<TypeArg> deducedTemplateArgs;
    MVector<Expr*> arguments;
    MVector<MStringView> scopeChain;
    MVector<LambdaCapture> lambdaCaptures;
    MVector<ParamDecl> lambdaParams;
    TypeRef* lambdaReturnType = nullptr;
    MVector<Stmt*> lambdaBody;
    TokenType::Enum op = TokenType::EndOfFile;
    b8 isPostfix = false;
    b8 isSwizzle = false;
    b8 moveAtLastUse = false;
    b8 adaptToTChar = false;
    b8 isPointerMemberAccess = false;
    b8 isNotNullRebind = false;
    b8 derefNotNull = false;
    b8 isMutArg = false;
    b8 isRetainedArg = false;
    b8 injectScratchArena = false;
    b8 injectClassArena = false;
    b8 returnsFromThisArena = false;
    b8 lambdaUsesScratchArena = false;
    b8 lambdaUsesTaskScope = false;
    b8 lambdaInjectsCallerArena = false;
    b8 lambdaAllowsUnwind = false;
    b8 useAdhdAssign = false;
    b8 useAdhdWrappedClassAssign = false;
    b8 useAdhdWrappedScratchAssign = false;
    b8 useAdhdMapIndex = false;
    b8 hasMutLocalCapture = false;
    b8 hasMutContainerCapture = false;
    b8 isVectorRawIndex = false;
    b8 isMatrixRowIndex = false;
    b8 isBuiltinCall = false;
    b8 isUserCallableCall = false;
    b8 isBuiltinConstant = false;
    b8 isStringSubstr = false;
    b8 isStringByteRead = false;
    b8 isStringByteConstRef = false;
    b8 isStringByteMutRef = false;
    b8 wrapInSimdResult = false;
    b8 wrapInNotNull = false;
    b8 unwrapSimdResultValue = false;
    u8 formatArgWrapKind = 0u;
    const TypeRef* wrapInFuncType = nullptr;
    b8 semanticChecked = false;
    mutable b8 hasCachedTypeRef = false;
    mutable const TypeRef* cachedTypeRef = nullptr;


public:
    explicit Expr(MetaArena& arena)noexcept
        : callTemplateArgs(arena)
        , deducedTemplateArgs(arena)
        , arguments(arena)
        , scopeChain(arena)
        , lambdaCaptures(arena)
        , lambdaParams(arena)
        , lambdaBody(arena)
    {}
};

[[nodiscard]] constexpr b8 IsParallelWaitCallExpr(const Expr* expr)noexcept{
    return expr && expr->kind == ExprKind::Call && expr->lhs && expr->lhs->kind == ExprKind::Identifier && KeywordSemantics::IsParallelWaitKeyword(expr->lhs->text);
}

constexpr void PromoteStringByteReadToConstRef(Expr& expr)noexcept{
    if(expr.isStringByteRead){
        expr.isStringByteRead = false;
        expr.isStringByteConstRef = true;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

