// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "layout_optimizer.h"
#include "parallel_graph.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ScopedVarInfo{
    MStringView name;
    TypeRef* type = nullptr;
    ParamPassingMode::Enum mode = ParamPassingMode::None;
    bool isConst = false;
    bool isParam = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class TypeChecker{
public:
    TypeChecker(MetaArena& arena, SymbolTable& symbols)noexcept
        : m_arena(arena)
        , m_symbols(symbols)
        , m_layoutOptimizer(arena, symbols)
        , m_parallelValidator(arena)
    {}


public:
    [[nodiscard]] bool checkTranslationUnit(TranslationUnit& unit, MVector<ParseError>& errors);


private:
    [[nodiscard]] bool linkOutOfLineMethods(TranslationUnit& unit, MVector<ParseError>& errors);
    [[nodiscard]] bool checkInterfaceDecl(InterfaceDecl& iface, MVector<ParseError>& errors);
    [[nodiscard]] bool checkClassDecl(ClassDecl& classDecl, MVector<ParseError>& errors);
    [[nodiscard]] bool checkConstructorInitialization(ClassDecl& classDecl, MethodDecl& ctor, MVector<ParseError>& errors);
    [[nodiscard]] bool checkMethodBody(ClassDecl* ownerClass, MethodDecl& method, MVector<ParseError>& errors);
    [[nodiscard]] bool checkStatementList(
        ClassDecl* ownerClass,
        MethodDecl& method,
        MVector<Stmt*>& statements,
        MVector<ScopedVarInfo>& scope,
        MVector<ParseError>& errors
    );
    [[nodiscard]] bool checkStatement(
        ClassDecl* ownerClass,
        MethodDecl& method,
        Stmt& stmt,
        MVector<ScopedVarInfo>& scope,
        MVector<ParseError>& errors
    );
    [[nodiscard]] bool checkExpr(
        ClassDecl* ownerClass,
        MethodDecl& method,
        Expr& expr,
        const MVector<ScopedVarInfo>& scope,
        bool isMutationTarget,
        MVector<ParseError>& errors
    );
    [[nodiscard]] MStringView inferExprTypeName(
        ClassDecl* ownerClass,
        const Expr& expr,
        const MVector<ScopedVarInfo>& scope
    )const noexcept;

    [[nodiscard]] static const ScopedVarInfo* findScopedVar(const MVector<ScopedVarInfo>& scope, MStringView name)noexcept;
    [[nodiscard]] static const FieldDecl* findClassField(const ClassDecl& classDecl, MStringView name)noexcept;
    [[nodiscard]] static const MethodDecl* findClassMethod(const ClassDecl& classDecl, MStringView name)noexcept;
    void reportError(MVector<ParseError>& errors, SourceSpan span, MStringView message)const;


private:
    MetaArena& m_arena;
    SymbolTable& m_symbols;
    LayoutOptimizer m_layoutOptimizer;
    ParallelGraphValidator m_parallelValidator;
    u32 m_parallelDepth = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

