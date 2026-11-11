// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "ast_types.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct Stmt;
struct Expr;


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
        return "b8";
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
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct Expr{
    ExprKind::Enum kind = ExprKind::Identifier;
    SourceSpan span;
    MStringView text;
    MStringView resolvedSimdType;
    MStringView resolvedUnalignedType;
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
    b8 injectScratchArena = false;
    b8 injectClassArena = false;
    b8 lambdaUsesScratchArena = false;
    b8 lambdaUsesTaskScope = false;
    b8 hasMutLocalCapture = false;
    b8 hasMutContainerCapture = false;
    b8 isVectorRawIndex = false;
    b8 isMatrixRowIndex = false;
    b8 isBuiltinCall = false;
    b8 isBuiltinConstant = false;
    b8 isStringSubstr = false;
    b8 isStringByteRead = false;
    b8 isStringByteConstRef = false;
    b8 isStringByteMutRef = false;
    b8 wrapInSimdResult = false;
    b8 unwrapSimdResultValue = false;
    u8 formatArgWrapKind = 0u;
    const TypeRef* wrapInFuncType = nullptr;


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
    return expr && expr->kind == ExprKind::Call && expr->lhs && expr->lhs->kind == ExprKind::Identifier && expr->lhs->text == "wait";
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

