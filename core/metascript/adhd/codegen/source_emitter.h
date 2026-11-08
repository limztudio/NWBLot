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
        , m_activeLocalVarDecls(arena)
    {}


public:
    [[nodiscard]] MString emitSource(const TranslationUnit& unit, MStringView headerIncludePath, MStringView namespacePath)const;

    void emitOutOfClassCallable(MString& out, const ClassDecl* ownerClass, const MethodDecl& callable, b8 emitBody, b8 emitInlineKeyword)const;
    void emitMethodBodyBlock(MString& out, const MethodDecl& method, u32 indent)const;
    void emitConstructorNotNullInitializers(MString& out, Span<const FieldDecl* const> orderedFields, const MethodDecl& ctor)const;
    void emitExpr(MString& out, const Expr& expr, b8 omitOuterBinaryParens = false)const;


private:
    friend class SimdConstructorEmitter;
    friend class SimdCastConstructorEmitter;
    friend class SimdLaneConstructorEmitter;
    friend class SimdScalarLaneEmitter;
    friend class SimdLaneOperationEmitter;
    friend class SimdBinaryEmitter;
    friend class SimdCallEmitter;
    friend class SimdStorageEmitter;
    friend class SimdLogicEmitter;
    friend class BuiltinCallEmitter;
    friend class ParallelStatementEmitter;
    friend class SwitchStatementEmitter;
    friend class ConstructorArgumentEmitter;


private:
    void emitCallTargetExpr(MString& out, const Expr* lhs, b8 isBuiltinCall)const;
    void emitIndent(MString& out, u32 indent)const;
    void emitScratchArenaPrologue(MString& out, u32 indent, MStringView arenaName)const;
    void emitStatementList(MString& out, Span<const Stmt* const> statements, u32 indent)const;
    void emitLoopBodyStatements(MString& out, Span<const Stmt* const> statements, u32 indent)const;
    void emitStmt(MString& out, const Stmt& stmt, u32 indent)const;
    [[nodiscard]] b8 isDirectSimdRegExpr(const Expr& expr)const noexcept;
    [[nodiscard]] b8 tryEmitSimdVarDeclStmt(MString& out, const Stmt& stmt, u32 indent)const;
    [[nodiscard]] b8 tryEmitSimdAssignStmt(MString& out, const Stmt& stmt, u32 indent)const;
    [[nodiscard]] b8 tryEmitSimdReturnStmt(MString& out, const Stmt& stmt, u32 indent)const;
    void emitSimdLaneGetExpr(MString& out, const Expr& vecExpr, u32 lane, const Expr* dynamicIndexExpr)const;
    void emitSimdRegExpr(MString& out, const Expr& expr, MStringView expectedSimdType)const;
    [[nodiscard]] b8 tryEmitFusedSimdLogicChain(MString& out, const Expr& expr, b8 omitOuterBinaryParens)const;


private:
    MetaArena& m_arena;
    const SymbolTable& m_symbols;
    CppTypeEmitter m_typeEmitter;
    [[nodiscard]] Span<const Stmt* const> activeLocalVarDecls()const noexcept{
        return Span<const Stmt* const>(m_activeLocalVarDecls.data(), m_activeLocalVarDecls.size());
    }


private:
    mutable u32 m_activeSwitchBreakLabelLine = 0u;
    mutable TokenType::Enum m_unfusedLogicOp = TokenType::EndOfFile;
    mutable const MVector<const Expr*>* m_skippedCtorAssignExprs = nullptr;
    mutable MVector<const Stmt*> m_activeLocalVarDecls;
    mutable Span<const Stmt* const> m_remainingBlockStatements;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
