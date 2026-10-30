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


class TypeChecker final{
private:
    [[nodiscard]] static const ScopedVarInfo* FindScopedVar(Span<const ScopedVarInfo> scope, MStringView name)noexcept;
    [[nodiscard]] static const TemplateParamDecl* FindTemplateParam(const ClassDecl* ownerClass, const MethodDecl* method, MStringView name)noexcept;
    [[nodiscard]] static b8 StatementsContainReturnOrNative(Span<const Stmt* const> stmts)noexcept;
    static void PropagateFuncCaptureBytes(TypeRef* targetType, const Expr* rhs, Span<const ScopedVarInfo> scope, const SymbolTable& symbols)noexcept;
    [[nodiscard]] static b8 IsEscapingMutCaptureLambda(const Expr* rhs, Span<const ScopedVarInfo> scope)noexcept;


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
    void buildCallArgHints(ClassDecl* ownerClass, const MethodDecl* method, Span<const Expr* const> args, Span<const ScopedVarInfo> scope, MVector<MStringView>& outArgTypes, MVector<u8>& outArgIsMut)const;
    [[nodiscard]] MStringView resolveScopedVarBaseType(const ScopedVarInfo& var)const noexcept;
    [[nodiscard]] b8 isThisConstInScope(const MethodDecl& method, Span<const ScopedVarInfo> scope)const noexcept;
    [[nodiscard]] b8 validateThisAccess(const ClassDecl* ownerClass, SourceSpan span, Span<const ScopedVarInfo> scope, MVector<ParseError>& errors)const;
    [[nodiscard]] b8 isConstructibleTypeName(MStringView calleeName, MStringView baseCallee)const noexcept;
    [[nodiscard]] const MethodDecl* resolveCustomOperator(
        ClassDecl* ownerClass,
        const MethodDecl* method,
        const TypeRef* lhsTypeRef,
        MStringView lhsBaseType,
        MStringView opSym,
        usize arity,
        b8 recvIsConst,
        const ClassDecl** outDeclaringClass = nullptr
    )const noexcept;
    [[nodiscard]] MStringView resolveScopeAccessEnumTypeName(
        ClassDecl* ownerClass,
        const MethodDecl* method,
        const Expr& expr,
        Span<const ScopedVarInfo> scope,
        const ClassDecl** outAccessedClass = nullptr,
        const FieldDecl** outAccessedField = nullptr
    )const noexcept;
    void reportError(MVector<ParseError>& errors, const SourceSpan span, const MStringView message)const{ ReportAdhdDiagnostic(m_arena, errors, span, message); }

    [[nodiscard]] b8 checkTemplateParamBounds(Span<const TemplateParamDecl> params, const ClassDecl* enclosingClass, const MethodDecl* enclosingMethod, MVector<ParseError>& errors);
    [[nodiscard]] b8 interfaceInheritsFrom(const TypeRef& ifaceRef, const TypeRef& requiredIface, u32 depth = 0u)const noexcept;
    [[nodiscard]] b8 classImplementsInterface(const ClassDecl& cls, const TypeRef* implClassRef, const TypeRef& requiredIface)const noexcept;
    [[nodiscard]] b8 checkTemplateArgBounds(Span<const TemplateParamDecl> params, Span<const TypeArg> args, const ClassDecl* enclosingClass, const MethodDecl* enclosingMethod, SourceSpan span, MVector<ParseError>& errors);
    [[nodiscard]] b8 checkTemplateArgTypes(Span<const TypeArg> templateArgs, const ClassDecl* enclosingClass, const MethodDecl* enclosingMethod, SourceSpan fallbackSpan, MVector<ParseError>& errors);
    [[nodiscard]] b8 checkTypeRef(const TypeRef& typeRef, const ClassDecl* enclosingClass, const MethodDecl* enclosingMethod, b8 allowVoid, MVector<ParseError>& errors);
    [[nodiscard]] b8 checkTypeRef(const TypeRef& typeRef, const ClassDecl* enclosingClass, const MethodDecl* enclosingMethod, MVector<ParseError>& errors){ return checkTypeRef(typeRef, enclosingClass, enclosingMethod, false, errors); }
    [[nodiscard]] b8 checkReturnTypeRef(const TypeRef& returnType, const ClassDecl* enclosingClass, const MethodDecl* enclosingMethod, SourceSpan errorSpan, MVector<ParseError>& errors);
    void markStringToPathAdaptation(ClassDecl* ownerClass, const MethodDecl* method, Expr& expr, Span<const ScopedVarInfo> scope)const noexcept;
    [[nodiscard]] b8 verifyConcreteClassImplementsInterface(
        ClassDecl* ownerClass,
        const MethodDecl& method,
        const Expr& rhsExpr,
        const TypeRef* expectedInterfaceType,
        Span<const ScopedVarInfo> scope,
        SourceSpan errorSpan,
        MStringView errorMessage,
        MVector<ParseError>& errors
    );

    [[nodiscard]] b8 checkInterfaceDecl(InterfaceDecl& iface, MVector<ParseError>& errors);
    [[nodiscard]] b8 checkCallableSignature(const ClassDecl* enclosingClass, MethodDecl& method, MVector<ParseError>& errors);
    [[nodiscard]] b8 hasFieldInlineOrAutoDefault(const ClassDecl& classDecl, const FieldDecl& field)const noexcept;
    [[nodiscard]] b8 checkClassDecl(ClassDecl& classDecl, MVector<ParseError>& errors);
    [[nodiscard]] b8 checkConstructorInitialization(ClassDecl& classDecl, MethodDecl& ctor, MVector<ParseError>& errors);

    [[nodiscard]] b8 checkMethodBody(ClassDecl* ownerClass, MethodDecl& method, MVector<ParseError>& errors);
    [[nodiscard]] b8 checkStatementList(ClassDecl* ownerClass, MethodDecl& method, MVector<Stmt*>& statements, MVector<ScopedVarInfo>& scope, MVector<ParseError>& errors);
    [[nodiscard]] b8 checkConstructorInitArguments(ClassDecl* ownerClass, MethodDecl& method, const TypeRef* targetType, MVector<Expr*>& arguments, Span<const ScopedVarInfo> scope, SourceSpan errorSpan, MVector<ParseError>& errors);
    [[nodiscard]] b8 checkVarDeclStatement(ClassDecl* ownerClass, MethodDecl& method, Stmt& stmt, MVector<ScopedVarInfo>& scope, MVector<ParseError>& errors);
    [[nodiscard]] b8 checkReturnStatement(ClassDecl* ownerClass, MethodDecl& method, Stmt& stmt, Span<const ScopedVarInfo> scope, MVector<ParseError>& errors);
    [[nodiscard]] b8 checkStatement(ClassDecl* ownerClass, MethodDecl& method, Stmt& stmt, MVector<ScopedVarInfo>& scope, MVector<ParseError>& errors);

    [[nodiscard]] b8 checkExpr(ClassDecl* ownerClass, MethodDecl& method, Expr& expr, Span<const ScopedVarInfo> scope, b8 isMutationTarget, MVector<ParseError>& errors);
    [[nodiscard]] b8 applyInterfaceRefRebind(
        ClassDecl* ownerClass,
        MethodDecl& method,
        Expr& assignExpr,
        const TypeRef* targetType,
        ParamPassingMode::Enum targetRefMode,
        Span<const ScopedVarInfo> scope,
        MStringView errorMessage,
        b8& outRhsNeedsMut,
        MVector<ParseError>& errors
    );
    [[nodiscard]] b8 checkBinaryExpr(ClassDecl* ownerClass, MethodDecl& method, Expr& expr, Span<const ScopedVarInfo> scope, MVector<ParseError>& errors);
    [[nodiscard]] b8 isReceiverConst(ClassDecl* ownerClass, const MethodDecl& method, const Expr& recv, Span<const ScopedVarInfo> scope)const noexcept;

    [[nodiscard]] b8 checkCallableArguments(
        ClassDecl* ownerClass,
        MethodDecl& method,
        MVector<Expr*>& arguments,
        Span<const ScopedVarInfo> scope,
        const MethodDecl* targetCallable,
        const ClassDecl* targetOwnerCls,
        const TypeRef* recvTypeRef,
        Span<const TypeArg> callTemplateArgs,
        const TypeRef* invokedFuncType,
        const ScopedVarInfo* localCallable,
        MStringView calledMemberName,
        MVector<ParseError>& errors
    );
    [[nodiscard]] b8 checkCallExpr(ClassDecl* ownerClass, MethodDecl& method, Expr& expr, Span<const ScopedVarInfo> scope, b8 isMutationTarget, MVector<ParseError>& errors);
    [[nodiscard]] b8 checkLambdaExpr(ClassDecl* ownerClass, MethodDecl& method, Expr& expr, Span<const ScopedVarInfo> scope, MVector<ParseError>& errors);

    [[nodiscard]] MStringView inferExprTypeName(ClassDecl* ownerClass, const MethodDecl* method, const Expr& expr, Span<const ScopedVarInfo> scope)const noexcept;
    [[nodiscard]] const TypeRef* inferExprTypeRef(ClassDecl* ownerClass, const MethodDecl* method, const Expr& expr, Span<const ScopedVarInfo> scope)const noexcept;
    [[nodiscard]] MStringView inferExprBaseType(ClassDecl* ownerClass, const MethodDecl* method, const Expr& expr, Span<const ScopedVarInfo> scope, const TypeRef** outTypeRef = nullptr)const noexcept;
    void analyzeBlockMoveAtLastUse(Span<Stmt* const> statements, Span<const ScopedVarInfo> scope, usize savedScopeSize)const noexcept;


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
    b8 m_activeParallelBlockUsesScratch = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

