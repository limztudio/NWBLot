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
    [[nodiscard]] bool tryEmitParallelWaitStmt(MString& out, const Stmt& stmt, u32 indent)const;
    void emitParallelStmt(MString& out, const Stmt& stmt, u32 indent)const;
    [[nodiscard]] bool tryEmitSimdVarDeclStmt(MString& out, const Stmt& stmt, u32 indent)const;
    [[nodiscard]] bool tryEmitSimdAssignStmt(MString& out, const Stmt& stmt, u32 indent)const;
    [[nodiscard]] bool tryEmitSimdReturnStmt(MString& out, const Stmt& stmt, u32 indent)const;
    void emitSimdRegExpr(MString& out, const Expr& expr, MStringView expectedSimdType)const;
    void emitSimdVectorCtorReg(MString& out, MStringView simdType, const MVector<Expr*>& arguments)const;
    void emitSimdStoreForBinding(MString& out, const SimdVarBinding& binding, u32 indent)const;
    void emitSimdLoadReloadForBinding(MString& out, const SimdVarBinding& binding, u32 indent)const;
    void emitSimdLoopElementLoad(MString& out, const SimdVarBinding& loopBinding, u32 indent)const;
    void emitSimdMutCallReloads(MString& out, const Expr& expr, u32 indent)const;
    void emitSimdBlockStores(MString& out, u32 indent, bool loopElementsOnly)const;
    [[nodiscard]] const SimdVarBinding* findSimdBinding(bool isThisMember, MStringView name)const noexcept;
    [[nodiscard]] const SimdVarBinding* findSimdBindingForExpr(const Expr* expr)const noexcept;
    void emitIndent(MString& out, u32 indent)const;


private:
    MetaArena& m_arena;
    const SymbolTable& m_symbols;
    CppTypeEmitter m_typeEmitter;
    mutable const MVector<SimdVarBinding>* m_activeSimdBindings = nullptr;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

