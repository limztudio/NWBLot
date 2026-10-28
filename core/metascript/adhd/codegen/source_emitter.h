// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "header_emitter.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct SimdVarBinding{
    bool isThisMember = false;
    bool isLocalDecl = false;
    bool isLoopElement = false;
    bool readBeforeWrite = false;
    bool written = false;
    bool unconditionallyWrittenFirst = false;
    bool needsStorageSync = false;
    bool capturedByMutLambda = false;
    MStringView name;
    MStringView simdType;
    MString regName;

    explicit SimdVarBinding(MetaArena& arena)noexcept
        : regName(arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class SourceEmitter{
public:
    SourceEmitter(MetaArena& arena, const SymbolTable& symbols, const HeaderEmitter& headerEmitter)noexcept
        : m_arena(arena)
        , m_symbols(symbols)
        , m_headerEmitter(headerEmitter)
    {}


public:
    [[nodiscard]] MString emitSource(
        const TranslationUnit& unit,
        MStringView headerIncludePath,
        MStringView namespacePath
    )const;

    void emitMethodBodyBlock(MString& out, const MethodDecl& method, u32 indent)const;
    void emitExpr(MString& out, const Expr& expr)const;


private:
    void emitScopedSimdBody(MString& out, const MVector<Stmt*>& statements, u32 indent)const;
    void emitStatementList(MString& out, const MVector<Stmt*>& statements, u32 indent)const;
    void emitSwitchIfChainStatements(MString& out, const MVector<Stmt*>& statements, u32 indent)const;
    void emitStmt(MString& out, const Stmt& stmt, u32 indent)const;
    void emitSimdRegExpr(MString& out, const Expr& expr, MStringView expectedSimdType)const;
    void emitSimdVectorCtorReg(MString& out, MStringView simdType, const MVector<Expr*>& arguments)const;
    void emitSimdStoreForBinding(MString& out, const SimdVarBinding& binding, u32 indent)const;
    void emitSimdLoadReloadForBinding(MString& out, const SimdVarBinding& binding, u32 indent)const;
    void emitSimdMutCallReloads(MString& out, const Expr& expr, u32 indent)const;
    void emitSimdBlockStores(MString& out, u32 indent, bool loopElementsOnly)const;
    [[nodiscard]] const SimdVarBinding* findSimdBinding(bool isThisMember, MStringView name)const noexcept;
    void emitIndent(MString& out, u32 indent)const;


private:
    MetaArena& m_arena;
    const SymbolTable& m_symbols;
    const HeaderEmitter& m_headerEmitter;
    mutable const MVector<SimdVarBinding>* m_activeSimdBindings = nullptr;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

