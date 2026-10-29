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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct LambdaCapture{
    ParamPassingMode::Enum mode = ParamPassingMode::None;
    MStringView name;
    SourceSpan span;
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
    Expr* lhs = nullptr;
    Expr* rhs = nullptr;
    TypeRef* targetType = nullptr;
    MVector<TypeArg> callTemplateArgs;
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


public:
    explicit Expr(MetaArena& arena)noexcept
        : callTemplateArgs(arena)
        , arguments(arena)
        , scopeChain(arena)
        , lambdaCaptures(arena)
        , lambdaParams(arena)
        , lambdaBody(arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

