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

    void emitMethodBodyBlock(MString& out, const MethodDecl& method, u32 indent)const;
    void emitConstructorNotNullInitializers(MString& out, const MVector<FieldDecl*>& orderedFields, const MethodDecl& ctor)const;
    void emitExpr(MString& out, const Expr& expr)const;


private:
    void emitScopedSimdBody(MString& out, const MVector<Stmt*>& statements, u32 indent)const;
    void emitStatementList(MString& out, const MVector<Stmt*>& statements, u32 indent)const;
    void emitSwitchIfChainStatements(MString& out, const MVector<Stmt*>& statements, u32 indent)const;
    void emitStmt(MString& out, const Stmt& stmt, u32 indent)const;
    void emitTaskScopePrologue(MString& out, u32 indent)const;
    [[nodiscard]] b8 tryEmitParallelWaitStmt(MString& out, const Stmt& stmt, u32 indent)const;
    void emitParallelStmt(MString& out, const Stmt& stmt, u32 indent)const;
    [[nodiscard]] b8 tryEmitSimdVarDeclStmt(MString& out, const Stmt& stmt, u32 indent)const;
    [[nodiscard]] b8 tryEmitSimdAssignStmt(MString& out, const Stmt& stmt, u32 indent)const;
    [[nodiscard]] b8 tryEmitSimdReturnStmt(MString& out, const Stmt& stmt, u32 indent)const;
    [[nodiscard]] b8 tryEmitSimdBinaryOpReg(MString& out, TokenType::Enum binaryOp, const Expr& lhs, const Expr& rhs, MStringView simdType)const;
    void emitSimdRegExpr(MString& out, const Expr& expr, MStringView expectedSimdType)const;
    void emitSimdVectorCtorReg(MString& out, MStringView simdType, const MVector<Expr*>& arguments)const;
    void emitSimdStoreForBinding(MString& out, const SimdVarBinding& binding, u32 indent)const;
    void emitSimdLoadReloadForBinding(MString& out, const SimdVarBinding& binding, u32 indent)const;
    void emitSimdLoopElementLoad(MString& out, const SimdVarBinding& loopBinding, u32 indent)const;
    void emitSimdMutCallReloads(MString& out, const Expr& expr, u32 indent)const;
    void emitSimdBlockStores(MString& out, u32 indent, b8 loopElementsOnly)const;
    [[nodiscard]] const SimdVarBinding* findSimdBinding(b8 isThisMember, MStringView name)const noexcept;
    [[nodiscard]] const SimdVarBinding* findSimdBindingForExpr(const Expr* expr)const noexcept;
    void emitIndent(MString& out, u32 indent)const;


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

