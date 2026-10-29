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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

