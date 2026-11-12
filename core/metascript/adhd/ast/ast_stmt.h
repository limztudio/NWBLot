// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "ast_expr.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace StmtKind{
    enum Enum : u8{
        Block,
        VarDecl,
        ExprStmt,
        Return,
        If,
        Switch,
        ForRange,
        ForIndex,
        While,
        DoWhile,
        Break,
        Continue,
        ParallelForRange,
        ParallelForIndex,
        ParallelBlock,
        ParallelCall,
        NativeBlock,
        Diagnostic,
    };
};

[[nodiscard]] constexpr b8 IsTerminalJumpStmtKind(const StmtKind::Enum kind)noexcept{
    return kind == StmtKind::Return || kind == StmtKind::Break || kind == StmtKind::Continue;
}

[[nodiscard]] constexpr b8 IsSequentialLoopStmtKind(const StmtKind::Enum kind)noexcept{
    return kind == StmtKind::While || kind == StmtKind::DoWhile || kind == StmtKind::ForRange || kind == StmtKind::ForIndex;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct SwitchCase{
    Expr* matchExpr = nullptr;
    MVector<Stmt*> statements;
    SourceSpan span;
    b8 isDefault = false;


public:
    explicit SwitchCase(MetaArena& arena)noexcept
        : statements(arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct NativeBinding{
    ParamPassingMode::Enum mode = ParamPassingMode::None;
    MStringView aliasName;
    Expr* sourceExpr = nullptr;
    SourceSpan span;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct Stmt{
    StmtKind::Enum kind = StmtKind::Block;
    SourceSpan span;

    MVector<Stmt*> statements;
    MVector<Stmt*> elseStatements;

    TypeRef* varType = nullptr;
    TypeRef* inferredAutoType = nullptr;
    MStringView name;
    Expr* expr = nullptr;
    Expr* condition = nullptr;
    Expr* stepExpr = nullptr;
    Stmt* initStmt = nullptr;
    MVector<Expr*> arguments;

    ParamPassingMode::Enum rangeMode = ParamPassingMode::None;
    MVector<SwitchCase> switchCases;
    MVector<MStringView> parallelDependencies;
    MVector<NativeBinding> nativeBindings;
    MStringView nativeCode;

    ParallelCoreKind::Enum parallelCore = ParallelCoreKind::Any;
    ParallelPriorityKind::Enum parallelPriority = ParallelPriorityKind::Normal;
    DiagnosticKind::Enum diagnosticKind = DiagnosticKind::Assert;

    b8 isConst = false;
    b8 isCtorInit = false;
    b8 isEssential = false;
    b8 lowerSwitchToIfChain = false;
    b8 usesScratchArena = false;
    b8 arenaFirstInit = false;
    b8 wrapReturnNotNull = false;
    b8 isImported = false;


public:
    explicit Stmt(MetaArena& arena)noexcept
        : statements(arena)
        , elseStatements(arena)
        , arguments(arena)
        , switchCases(arena)
        , parallelDependencies(arena)
        , nativeBindings(arena)
    {}
};

[[nodiscard]] constexpr b8 IsThisMemberSimpleAssignStmt(const Stmt& stmt, const MStringView expectedFieldName = {})noexcept{
    if(stmt.kind != StmtKind::ExprStmt || !stmt.expr || stmt.expr->kind != ExprKind::Binary || stmt.expr->op != TokenType::Equal)
        return false;
    if(!stmt.expr->lhs || stmt.expr->lhs->kind != ExprKind::ThisMember || stmt.expr->lhs->text.empty())
        return false;
    return expectedFieldName.empty() || stmt.expr->lhs->text == expectedFieldName;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

