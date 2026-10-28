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
    MStringView inferredTypeName;
    TypeRef* type = nullptr;
    const Expr* lambdaExpr = nullptr;
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
    [[nodiscard]] bool checkTemplateParamBounds(const MVector<TemplateParamDecl>& params, MVector<ParseError>& errors);
    [[nodiscard]] bool checkTemplateArgBounds(
        const MVector<TemplateParamDecl>& params,
        const MVector<TypeArg>& args,
        const ClassDecl* enclosingClass,
        const MethodDecl* enclosingMethod,
        SourceSpan span,
        MVector<ParseError>& errors
    );
    [[nodiscard]] bool checkTypeRef(
        const TypeRef& typeRef,
        const ClassDecl* enclosingClass,
        const MethodDecl* enclosingMethod,
        MVector<ParseError>& errors,
        bool allowVoid = false
    );
    [[nodiscard]] bool classImplementsInterface(const ClassDecl& cls, MStringView requiredIface)const noexcept;
    [[nodiscard]] bool interfaceInheritsFrom(const InterfaceDecl& iface, MStringView requiredIface, u32 depth = 0u)const noexcept;
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
        const MethodDecl* method,
        const Expr& expr,
        const MVector<ScopedVarInfo>& scope
    )const noexcept;
    [[nodiscard]] const TypeRef* inferExprTypeRef(
        ClassDecl* ownerClass,
        const MethodDecl* method,
        const Expr& expr,
        const MVector<ScopedVarInfo>& scope
    )const noexcept;

    [[nodiscard]] static const TemplateParamDecl* findTemplateParam(
        const ClassDecl* ownerClass,
        const MethodDecl* method,
        MStringView name
    )noexcept;
    [[nodiscard]] static const ScopedVarInfo* findScopedVar(const MVector<ScopedVarInfo>& scope, MStringView name)noexcept;
    [[nodiscard]] static const FieldDecl* findClassField(const ClassDecl& classDecl, MStringView name)noexcept;
    [[nodiscard]] static const MethodDecl* findClassMethod(const ClassDecl& classDecl, MStringView name, const usize* preferredArgCount = nullptr)noexcept;
    void reportError(MVector<ParseError>& errors, SourceSpan span, MStringView message)const;


private:
    MetaArena& m_arena;
    SymbolTable& m_symbols;
    LayoutOptimizer m_layoutOptimizer;
    ParallelGraphValidator m_parallelValidator;
    const TypeRef* m_activeLambdaReturnType = nullptr;
    u32 m_parallelDepth = 0u;
    u32 m_loopDepth = 0u;
    u32 m_switchDepth = 0u;
    bool m_inLambda = false;
    bool m_inClassArenaContext = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

