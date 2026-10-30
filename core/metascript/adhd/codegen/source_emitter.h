// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "cpp_type_emitter.h"
#include "simd_binding_analyzer.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class SourceEmitter{
public:
    SourceEmitter(MetaArena& arena, const SymbolTable& symbols)noexcept
        : m_arena(arena)
        , m_symbols(symbols)
        , m_typeEmitter(symbols)
    {}


public:
    [[nodiscard]] MString emitSource(
        const TranslationUnit& unit,
        MStringView headerIncludePath,
        MStringView namespacePath
    )const;

    void emitOutOfClassCallable(
        MString& out,
        const ClassDecl* ownerClass,
        const MethodDecl& callable,
        b8 emitBody,
        b8 emitInlineKeyword
    )const;
    void emitMethodBodyBlock(MString& out, const MethodDecl& method, u32 indent)const;
    void emitConstructorNotNullInitializers(MString& out, Span<const FieldDecl* const> orderedFields, const MethodDecl& ctor)const;
    void emitCtorOrCallArgs(
        MString& out,
        MStringView baseTypeName,
        Span<const Expr* const> arguments,
        MStringView injectedArenaName = {},
        b8 useBracesForList = false,
        b8 emptyUsesBraces = true
    )const;
    void emitExpr(MString& out, const Expr& expr)const;


private:
    void emitIndent(MString& out, u32 indent)const;
    void emitStatementList(MString& out, Span<const Stmt* const> statements, u32 indent)const;
    void emitSwitchIfChainStatements(MString& out, Span<const Stmt* const> statements, u32 indent)const;
    void emitLoopBodyStatements(MString& out, Span<const Stmt* const> statements, u32 indent)const;
    void emitStmt(MString& out, const Stmt& stmt, u32 indent)const;
    void emitTaskScopePrologue(MString& out, u32 indent)const;
    [[nodiscard]] b8 tryEmitParallelWaitStmt(MString& out, const Stmt& stmt, u32 indent)const;
    void emitParallelStmt(MString& out, const Stmt& stmt, u32 indent)const;
    void emitSimdVectorCtorReg(MString& out, MStringView simdType, Span<const Expr* const> arguments)const;
    [[nodiscard]] const SimdVarBinding* findSimdBinding(b8 isThisMember, MStringView name)const noexcept;
    [[nodiscard]] const SimdVarBinding* findSimdBindingForExpr(const Expr* expr)const noexcept;
    [[nodiscard]] const SimdVarBinding* findSimdRootBindingForExpr(const Expr* expr)const noexcept;
    void emitSimdBindingInitialDecl(MString& out, const SimdVarBinding& binding, b8 loadFromStorage, b8 qualifyThisMember, u32 indent)const;
    void emitSimdStoreForBinding(MString& out, const SimdVarBinding& binding, u32 indent)const;
    void emitSimdLoadReloadForBinding(MString& out, const SimdVarBinding& binding, u32 indent)const;
    void emitSimdLoopElementLoad(MString& out, const SimdVarBinding& loopBinding, u32 indent)const;
    void emitSimdMutCallReloads(MString& out, const Expr& expr, u32 indent)const;
    void emitSimdBlockStores(MString& out, u32 indent)const;
    void emitScopedSimdBody(MString& out, Span<const Stmt* const> statements, u32 indent)const;
    [[nodiscard]] b8 tryEmitSimdVarDeclStmt(MString& out, const Stmt& stmt, u32 indent)const;
    [[nodiscard]] b8 tryEmitSimdAssignStmt(MString& out, const Stmt& stmt, u32 indent)const;
    [[nodiscard]] b8 tryEmitSimdReturnStmt(MString& out, const Stmt& stmt, u32 indent)const;
    void emitSimdLaneGetExpr(MString& out, const Expr& vecExpr, u32 lane, const Expr* dynamicIndexExpr)const;
    [[nodiscard]] b8 tryEmitSimdBinaryOpReg(MString& out, TokenType::Enum binaryOp, const Expr& lhs, const Expr& rhs, MStringView simdType)const;
    void emitSimdRegExpr(MString& out, const Expr& expr, MStringView expectedSimdType)const;


private:
    MetaArena& m_arena;
    const SymbolTable& m_symbols;
    CppTypeEmitter m_typeEmitter;
    mutable const MVector<SimdVarBinding>* m_activeSimdBindings = nullptr;
    mutable const SimdVarBinding* m_activeSimdLoopBinding = nullptr;
    mutable u32 m_activeSimdLoopInnerBreakDepth = 0u;
    mutable u32 m_activeSimdLoopInnerContinueDepth = 0u;
    mutable u32 m_activeSwitchBreakLabelLine = 0u;
    mutable const MVector<const Expr*>* m_skippedCtorAssignExprs = nullptr;
    mutable b8 m_activeScopeUsesTaskScope = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

