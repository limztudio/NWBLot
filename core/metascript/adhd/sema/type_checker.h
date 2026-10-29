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
    u32 declaredParallelDepth = 0u;
    b8 isConst = false;
    b8 isParam = false;
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
    [[nodiscard]] b8 checkTranslationUnit(TranslationUnit& unit, MVector<ParseError>& errors);


private:
    [[nodiscard]] b8 linkOutOfLineMethods(TranslationUnit& unit, MVector<ParseError>& errors);
    [[nodiscard]] b8 checkTemplateParamBounds(
        const MVector<TemplateParamDecl>& params,
        const ClassDecl* enclosingClass,
        const MethodDecl* enclosingMethod,
        MVector<ParseError>& errors
    );
    [[nodiscard]] b8 checkTemplateArgBounds(
        const MVector<TemplateParamDecl>& params,
        const MVector<TypeArg>& args,
        const ClassDecl* enclosingClass,
        const MethodDecl* enclosingMethod,
        SourceSpan span,
        MVector<ParseError>& errors
    );
    [[nodiscard]] b8 checkTemplateArgTypes(
        const MVector<TypeArg>& templateArgs,
        const ClassDecl* enclosingClass,
        const MethodDecl* enclosingMethod,
        SourceSpan fallbackSpan,
        MVector<ParseError>& errors
    );
    [[nodiscard]] b8 checkTypeRef(
        const TypeRef& typeRef,
        const ClassDecl* enclosingClass,
        const MethodDecl* enclosingMethod,
        MVector<ParseError>& errors,
        b8 allowVoid = false
    );
    [[nodiscard]] b8 classImplementsInterface(const ClassDecl& cls, const TypeRef* implClassRef, const TypeRef& requiredIface)const noexcept;
    [[nodiscard]] b8 interfaceInheritsFrom(const TypeRef& ifaceRef, const TypeRef& requiredIface, u32 depth = 0u)const noexcept;
    void markStringToPathAdaptation(
        ClassDecl* ownerClass,
        const MethodDecl& method,
        Expr& expr,
        const MVector<ScopedVarInfo>& scope
    )const noexcept;
    [[nodiscard]] b8 verifyConcreteClassImplementsInterface(
        ClassDecl* ownerClass,
        const MethodDecl& method,
        const Expr& rhsExpr,
        const TypeRef* expectedInterfaceType,
        const MVector<ScopedVarInfo>& scope,
        SourceSpan errorSpan,
        MStringView errorMessage,
        MVector<ParseError>& errors
    );
    [[nodiscard]] b8 checkInterfaceDecl(InterfaceDecl& iface, MVector<ParseError>& errors);
    [[nodiscard]] b8 checkClassDecl(ClassDecl& classDecl, MVector<ParseError>& errors);
    [[nodiscard]] b8 checkCallableSignature(const ClassDecl* enclosingClass, MethodDecl& method, MVector<ParseError>& errors);
    [[nodiscard]] b8 hasFieldInlineOrAutoDefault(const ClassDecl& classDecl, const FieldDecl& field)const noexcept;
    [[nodiscard]] b8 checkConstructorInitialization(ClassDecl& classDecl, MethodDecl& ctor, MVector<ParseError>& errors);
    [[nodiscard]] b8 checkMethodBody(ClassDecl* ownerClass, MethodDecl& method, MVector<ParseError>& errors);
    [[nodiscard]] b8 checkStatementList(
        ClassDecl* ownerClass,
        MethodDecl& method,
        MVector<Stmt*>& statements,
        MVector<ScopedVarInfo>& scope,
        MVector<ParseError>& errors
    );
    void analyzeBlockMoveAtLastUse(
        const MVector<Stmt*>& statements,
        const MVector<ScopedVarInfo>& scope,
        usize savedScopeSize
    )const noexcept;
    [[nodiscard]] b8 checkStatement(
        ClassDecl* ownerClass,
        MethodDecl& method,
        Stmt& stmt,
        MVector<ScopedVarInfo>& scope,
        MVector<ParseError>& errors
    );
    [[nodiscard]] b8 checkVarDeclStatement(
        ClassDecl* ownerClass,
        MethodDecl& method,
        Stmt& stmt,
        MVector<ScopedVarInfo>& scope,
        MVector<ParseError>& errors
    );
    [[nodiscard]] b8 checkReturnStatement(
        ClassDecl* ownerClass,
        MethodDecl& method,
        Stmt& stmt,
        const MVector<ScopedVarInfo>& scope,
        MVector<ParseError>& errors
    );
    [[nodiscard]] b8 checkExpr(
        ClassDecl* ownerClass,
        MethodDecl& method,
        Expr& expr,
        const MVector<ScopedVarInfo>& scope,
        b8 isMutationTarget,
        MVector<ParseError>& errors
    );
    [[nodiscard]] b8 checkCallExpr(
        ClassDecl* ownerClass,
        MethodDecl& method,
        Expr& expr,
        const MVector<ScopedVarInfo>& scope,
        b8 isMutationTarget,
        MVector<ParseError>& errors
    );
    [[nodiscard]] b8 checkCallableArguments(
        ClassDecl* ownerClass,
        MethodDecl& method,
        MVector<Expr*>& arguments,
        const MVector<ScopedVarInfo>& scope,
        const MethodDecl* targetCallable,
        const ClassDecl* targetOwnerCls,
        const TypeRef* recvTypeRef,
        const MVector<TypeArg>* callTemplateArgs,
        const TypeRef* invokedFuncType,
        const ScopedVarInfo* localCallable,
        MStringView calledMemberName,
        MVector<ParseError>& errors
    );
    [[nodiscard]] b8 checkBinaryExpr(
        ClassDecl* ownerClass,
        MethodDecl& method,
        Expr& expr,
        const MVector<ScopedVarInfo>& scope,
        MVector<ParseError>& errors
    );
    [[nodiscard]] b8 checkLambdaExpr(
        ClassDecl* ownerClass,
        MethodDecl& method,
        Expr& expr,
        const MVector<ScopedVarInfo>& scope,
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

    [[nodiscard]] b8 isReceiverConst(
        ClassDecl* ownerClass,
        const MethodDecl& method,
        const Expr& recv,
        const MVector<ScopedVarInfo>& scope
    )const noexcept;

    static void propagateFuncCaptureBytes(
        TypeRef* targetType,
        const Expr* rhs,
        const MVector<ScopedVarInfo>& scope,
        const SymbolTable& symbols
    )noexcept;
    [[nodiscard]] static b8 isEscapingMutCaptureLambda(const Expr* rhs, const MVector<ScopedVarInfo>& scope)noexcept;
    [[nodiscard]] static b8 statementsContainReturnOrNative(const MVector<Stmt*>& stmts)noexcept;

    [[nodiscard]] static const TemplateParamDecl* findTemplateParam(
        const ClassDecl* ownerClass,
        const MethodDecl* method,
        MStringView name
    )noexcept;
    void buildCallArgHints(
        ClassDecl* ownerClass,
        const MethodDecl* method,
        const MVector<Expr*>& args,
        const MVector<ScopedVarInfo>& scope,
        MVector<MStringView>& outArgTypes,
        MVector<b8>& outArgIsMut
    )const;
    [[nodiscard]] static const ScopedVarInfo* findScopedVar(const MVector<ScopedVarInfo>& scope, MStringView name)noexcept;
    [[nodiscard]] static const FieldDecl* findClassField(const ClassDecl& classDecl, MStringView name)noexcept;
    [[nodiscard]] const MethodDecl* findClassMethod(
        const ClassDecl& classDecl,
        MStringView name,
        const usize* preferredArgCount = nullptr,
        b8 preferConst = false,
        b8 requirePublic = false,
        const MVector<MStringView>* argTypeNames = nullptr,
        const MVector<b8>* argIsMut = nullptr
    )const noexcept;
    [[nodiscard]] const MethodDecl* findClassOperator(
        const ClassDecl& classDecl,
        MStringView opName,
        const usize* preferredArgCount = nullptr,
        b8 preferConst = false,
        b8 requirePublic = false,
        const MVector<MStringView>* argTypeNames = nullptr,
        const MVector<b8>* argIsMut = nullptr
    )const noexcept;
    [[nodiscard]] const MethodDecl* findClassConstructor(
        const ClassDecl& classDecl,
        usize argCount,
        b8 requirePublic = false,
        const MVector<MStringView>* argTypeNames = nullptr,
        const MVector<b8>* argIsMut = nullptr
    )const noexcept;
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
    b8 m_inLambda = false;
    b8 m_inClassArenaContext = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
