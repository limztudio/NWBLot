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
    bool isPostfix = false;
    bool isSwizzle = false;
    bool moveAtLastUse = false;
    bool adaptToTChar = false;
    bool isPointerMemberAccess = false;
    bool isNotNullRebind = false;
    bool derefNotNull = false;
    bool isMutArg = false;
    bool injectScratchArena = false;
    bool injectClassArena = false;


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

