// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "cpp_type_emitter.h"
#include "simd_codegen_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class SourceEmitter final{
public:
    SourceEmitter(MetaArena& arena, const SymbolTable& symbols)noexcept
        : m_arena(arena)
        , m_symbols(symbols)
        , m_typeEmitter(symbols)
    {}


public:
    [[nodiscard]] MString emitSource(const TranslationUnit& unit, MStringView headerIncludePath, MStringView namespacePath)const;

    void emitOutOfClassCallable(MString& out, const ClassDecl* ownerClass, const MethodDecl& callable, b8 emitBody, b8 emitInlineKeyword)const;
    void emitMethodBodyBlock(MString& out, const MethodDecl& method, u32 indent)const;
    void emitConstructorNotNullInitializers(MString& out, Span<const FieldDecl* const> orderedFields, const MethodDecl& ctor)const;
    void emitCtorOrCallArgs(MString& out, MStringView baseTypeName, Span<const Expr* const> arguments, MStringView injectedArenaName = {}, b8 useBracesForList = false, b8 emptyUsesBraces = true)const;
    void emitSimdVectorCtorReg(MString& out, MStringView simdType, Span<const Expr* const> arguments)const;
    void emitExpr(MString& out, const Expr& expr, b8 omitOuterBinaryParens = false)const;


private:
    void emitCallTargetExpr(MString& out, const Expr* lhs, b8 isBuiltinCall)const;
    void emitIndent(MString& out, u32 indent)const;
    void emitScratchArenaPrologue(MString& out, u32 indent, MStringView arenaName)const;
    void emitStatementList(MString& out, Span<const Stmt* const> statements, u32 indent)const;
    void emitSwitchIfChainStatements(MString& out, Span<const Stmt* const> statements, u32 indent)const;
    void emitLoopBodyStatements(MString& out, Span<const Stmt* const> statements, u32 indent)const;
    void emitStmt(MString& out, const Stmt& stmt, u32 indent)const;
    void emitTaskScopePrologue(MString& out, u32 indent)const;
    [[nodiscard]] b8 tryEmitParallelWaitStmt(MString& out, const Stmt& stmt, u32 indent)const;
    void emitParallelStmt(MString& out, const Stmt& stmt, u32 indent)const;
    [[nodiscard]] b8 tryEmitStorageStoreFromSimdArgs(MString& out, MStringView storageTypeName, Span<const Expr* const> arguments, MStringView dstExprText)const;
    [[nodiscard]] b8 isDirectSimdRegExpr(const Expr& expr)const noexcept;
    [[nodiscard]] b8 tryEmitSimdVarDeclStmt(MString& out, const Stmt& stmt, u32 indent)const;
    [[nodiscard]] b8 tryEmitSimdAssignStmt(MString& out, const Stmt& stmt, u32 indent)const;
    [[nodiscard]] b8 tryEmitSimdReturnStmt(MString& out, const Stmt& stmt, u32 indent)const;
    void emitSimdLaneGetExpr(MString& out, const Expr& vecExpr, u32 lane, const Expr* dynamicIndexExpr)const;
    [[nodiscard]] b8 tryEmitSimdBinaryOpReg(MString& out, TokenType::Enum binaryOp, const Expr& lhs, const Expr& rhs, MStringView simdType)const;
    void emitSimdRegExpr(MString& out, const Expr& expr, MStringView expectedSimdType)const;
    [[nodiscard]] b8 tryEmitFusedSimdLogicChain(MString& out, const Expr& expr, b8 omitOuterBinaryParens)const;


private:
    MetaArena& m_arena;
    const SymbolTable& m_symbols;
    CppTypeEmitter m_typeEmitter;
    mutable u32 m_activeSwitchBreakLabelLine = 0u;
    mutable const MVector<const Expr*>* m_skippedCtorAssignExprs = nullptr;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
