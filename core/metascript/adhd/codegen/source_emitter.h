// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "cpp_type_emitter.h"
#include "simd_codegen_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 IsNameLiteralOnlyUnit(const TranslationUnit& unit)noexcept;
[[nodiscard]] b8 IsScalarLiteralOnlyUnit(const TranslationUnit& unit)noexcept;


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

    void setActiveOwnerClassContext(const ClassDecl* ownerClass, const b8 ownerNeedsArena)const noexcept{
        m_activeOwnerClass = ownerClass;
        m_activeOwnerNeedsArena = ownerNeedsArena;
    }
    void setActiveTemplateContext(
        const Span<const TemplateParamDecl> templateParams,
        const Span<const TypeArg> templateArgs,
        const Span<const FieldDecl* const> orderedFields
    )const noexcept{
        m_activeTemplateParams = templateParams;
        m_activeTemplateArgs = templateArgs;
        m_activeOrderedFields = orderedFields;
    }
    void setInFieldInitializer(const b8 inFieldInit)const noexcept{ m_inFieldInitializer = inFieldInit; }
    [[nodiscard]] b8 inFieldInitializer()const noexcept{ return m_inFieldInitializer; }
    [[nodiscard]] Span<const TemplateParamDecl> activeTemplateParams()const noexcept{ return m_activeTemplateParams; }
    [[nodiscard]] Span<const TypeArg> activeTemplateArgs()const noexcept{ return m_activeTemplateArgs; }
    [[nodiscard]] Span<const FieldDecl* const> activeOrderedFields()const noexcept{ return m_activeOrderedFields; }
    [[nodiscard]] const ClassDecl* activeOwnerClass()const noexcept{ return m_activeOwnerClass; }
    [[nodiscard]] b8 activeOwnerNeedsArena()const noexcept{ return m_activeOwnerNeedsArena; }
    [[nodiscard]] MStringView activeClassArenaExpr()const noexcept{
        return (m_activeOwnerClass && m_activeOwnerClass->isArenaOwner)
            ? MStringView("static_cast<::ArenaBase&>(s_ClassArena)")
            : MStringView("(*this->__adhd_arena)");
    }

    void emitOutOfClassCallable(MString& out, const ClassDecl* ownerClass, const MethodDecl& callable, b8 emitBody, b8 emitInlineKeyword, b8 includeTemplateDefaults = false)const;
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
    void emitCallTargetExpr(MString& out, const Expr* lhs, b8 isBuiltinCall, Span<const TypeArg> callTemplateArgs = {})const;
    void emitIndent(MString& out, u32 indent)const;
    void emitScratchArenaPrologue(MString& out, u32 indent, MStringView arenaName)const;
    [[nodiscard]] b8 statementsNeedSimdFloatingPointEnv(Span<const Stmt* const> statements)const noexcept;
    void emitUnusedParamSuppressions(MString& out, Span<const ParamDecl> params, Span<const Stmt* const> statements, u32 indent)const;
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
    mutable const ClassDecl* m_activeOwnerClass = nullptr;
    mutable b8 m_activeOwnerNeedsArena = false;
    mutable Span<const TemplateParamDecl> m_activeTemplateParams{};
    mutable Span<const TypeArg> m_activeTemplateArgs{};
    mutable Span<const FieldDecl* const> m_activeOrderedFields{};
    mutable b8 m_inFieldInitializer = false;
    [[nodiscard]] b8 instantiatedStatementsNeedScratchArena(Span<const Stmt* const> statements)const;
    [[nodiscard]] b8 instantiatedExprNeedsScratchArena(const Expr* expr)const;
    mutable const TypeRef* m_activeCallerArenaReturnType = nullptr;
    mutable u32 m_parallelWorkerDepth = 0u;
    mutable u32 m_activeSwitchBreakLabelLine = 0u;
    mutable TokenType::Enum m_unfusedLogicOp = TokenType::EndOfFile;
    mutable const MVector<const Expr*>* m_skippedCtorAssignExprs = nullptr;
    mutable MVector<const Stmt*> m_activeLocalVarDecls;
    mutable Span<const Stmt* const> m_remainingBlockStatements;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

