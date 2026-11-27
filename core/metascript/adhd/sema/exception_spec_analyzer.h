// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "symbol_table.h"
#include "type_layout_analyzer.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class TypeChecker;
struct ScopedVarInfo;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ExceptionSpecAnalyzer final{
public:
    ExceptionSpecAnalyzer(MetaArena& arena, const TypeChecker& checker, const SymbolTable& symbols)noexcept
        : m_arena(arena)
        , m_checker(checker)
        , m_symbols(symbols)
        , m_layoutAnalyzer(symbols)
    {}


public:
    void analyze(TranslationUnit& unit)const;


private:
    [[nodiscard]] b8 runMethodPass(const ClassDecl* ownerClass, MethodDecl& method, b8& outLambdaChanged)const;
    void runFieldDefaultsPass(const ClassDecl& cls, b8& outLambdaChanged)const;
    [[nodiscard]] b8 exprCopiesArenaValue(const Expr& valueExpr, const TypeRef* targetType, MVector<ScopedVarInfo>& scope)const;
    [[nodiscard]] b8 classDefaultCtorUnwinds(const ClassDecl& cls, b8 requirePublic)const noexcept;
    [[nodiscard]] b8 classCallCtorUnwinds(const ClassDecl& cls, Span<const Expr* const> args, const ClassDecl* ownerClass, const MethodDecl* method, Span<const ScopedVarInfo> scope)const;
    [[nodiscard]] b8 constructorImplicitlyUnwinds(const ClassDecl& ownerClass, const MethodDecl& ctor, MVector<ScopedVarInfo>& scope, b8& outLambdaChanged)const;
    [[nodiscard]] b8 destructorImplicitlyUnwinds(const ClassDecl& ownerClass)const noexcept;
    void pushVarDeclToScope(const ClassDecl* ownerClass, const MethodDecl* method, const Stmt& stmt, MVector<ScopedVarInfo>& scope)const;
    [[nodiscard]] b8 analyzeStatementList(const ClassDecl* ownerClass, const MethodDecl* method, Span<Stmt* const> statements, MVector<ScopedVarInfo>& scope, b8& outLambdaChanged)const;
    [[nodiscard]] b8 analyzeStatement(const ClassDecl* ownerClass, const MethodDecl* method, Stmt& stmt, MVector<ScopedVarInfo>& scope, b8& outLambdaChanged)const;
    [[nodiscard]] b8 analyzeExpr(const ClassDecl* ownerClass, const MethodDecl* method, Expr& expr, MVector<ScopedVarInfo>& scope, b8& outLambdaChanged)const;
    [[nodiscard]] b8 analyzeCallExpr(const ClassDecl* ownerClass, const MethodDecl* method, Expr& expr, MVector<ScopedVarInfo>& scope, b8& outLambdaChanged)const;


private:
    MetaArena& m_arena;
    const TypeChecker& m_checker;
    const SymbolTable& m_symbols;
    TypeLayoutAnalyzer m_layoutAnalyzer;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

