// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "ast_stmt.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct EnumEntry{
    MStringView name;
    Expr* valueExpr = nullptr;
    SourceSpan span;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct EnumDecl{
    MStringView name;
    MStringView sizeTypeName;
    MVector<EnumEntry> entries;
    SourceSpan span;
    usize underlyingSize = 1u;
    usize underlyingAlign = 1u;
    MStringView cppNamespace;
    MStringView cppExportName;
    MStringView requiredHeader;
    b8 isFlag = false;
    b8 isImported = false;
    b8 isExtern = false;
    b8 isDirectCppEnum = false;


public:
    explicit EnumDecl(MetaArena& arena)noexcept
        : entries(arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct TypeAliasDecl{
    MStringView name;
    MVector<TemplateParamDecl> templateParams;
    TypeRef* aliasedType = nullptr;
    SourceSpan span;
    MStringView cppNamespace;
    MStringView cppExportName;
    MStringView requiredHeader;
    b8 isImported = false;
    b8 isExtern = false;


public:
    explicit TypeAliasDecl(MetaArena& arena)noexcept
        : templateParams(arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ClassDecl;

struct FieldDecl{
    const ClassDecl* declaringClass = nullptr;
    AccessSpecifier::Enum access = AccessSpecifier::None;
    TypeRef* type = nullptr;
    MStringView name;
    Expr* defaultExpr = nullptr;
    MVector<Expr*> ctorArgs;
    SourceSpan span;
    usize resolvedSize = 0u;
    usize resolvedAlign = 1u;
    usize resolvedOffset = 0u;
    usize originalIndex = 0u;
    b8 hasCtorInit = false;
    b8 usesClassArena = false;
    b8 arenaFirstDefaultInit = false;
    b8 isTransient = false;
    b8 isReflect = false;
    b8 isStatic = false;


public:
    explicit FieldDecl(MetaArena& arena)noexcept
        : ctorArgs(arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct MethodDecl{
    const ClassDecl* declaringClass = nullptr;
    AccessSpecifier::Enum access = AccessSpecifier::None;
    MStringView ownerClassName;
    MVector<MStringView> ownerTemplateParams;
    MVector<TemplateParamDecl> ownerTemplateParamDecls;
    MVector<TemplateParamDecl> templateParams;
    TypeRef* returnType = nullptr;
    MStringView name;
    MVector<ParamDecl> params;
    MVector<Stmt*> body;
    SourceSpan span;
    b8 hasBody = false;
    b8 isConst = false;
    b8 isOverride = false;
    b8 isConstructor = false;
    b8 isDestructor = false;
    b8 isOperator = false;
    b8 isInline = false;
    b8 isOutOfLine = false;
    b8 allowsCppUnwind = false;
    b8 declaredBodylessInImport = false;
    b8 forceNothrowContract = false;
    b8 isOpenVirtualContract = false;
    b8 isNothrowExtern = false;
    b8 exportsExplicitBool = false;
    b8 exportsTakingAssignment = false;
    b8 exportsRvalueOverload = false;
    MStringView initializedCppBase;
    MVector<MStringView> cppBaseArguments;
    MVector<Expr*> exceptionProbes;
    b8 probesReturnValue = false;
    b8 isReflect = false;
    b8 usesScratchArena = false;
    b8 usesTaskScope = false;
    b8 injectsCallerArena = false;
    b8 returnsFromOwnerArena = false;
    b8 hasSynthesizedArenaParams = false;
    MStringView cppNamespace;
    MStringView cppExportName;
    MStringView requiredHeader;
    usize minArgCount = 0u;
    b8 hasDefaultArgs = false;
    b8 isStatic = false;
    b8 returnsReference = false;
    b8 isImported = false;
    b8 isExtern = false;
    b8 isVariadic = false;
    b8 returnsBorrowedView = false;
    u8 callLoweringKind = 0u;


public:
    explicit MethodDecl(MetaArena& arena)noexcept
        : ownerTemplateParams(arena)
        , ownerTemplateParamDecls(arena)
        , templateParams(arena)
        , params(arena)
        , body(arena)
        , cppBaseArguments(arena)
        , exceptionProbes(arena)
    {}

    [[nodiscard]] constexpr b8 acceptsArgCount(const usize count)const noexcept{
        if(isVariadic)
            return count >= minArgCount;
        return params.size() == count || (hasDefaultArgs && count >= minArgCount && count <= params.size());
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct InterfaceDecl{
    MStringView name;
    MVector<TemplateParamDecl> templateParams;
    MVector<TypeRef*> baseInterfaces;
    MVector<MethodDecl*> methods;
    MStringView assetTypeName;
    SourceSpan span;
    MStringView cppNamespace;
    MStringView cppExportName;
    MStringView requiredHeader;
    b8 isImported = false;
    b8 isExtern = false;


public:
    explicit InterfaceDecl(MetaArena& arena)noexcept
        : templateParams(arena)
        , baseInterfaces(arena)
        , methods(arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ClassDecl{
    MStringView name;
    MVector<TemplateParamDecl> templateParams;
    MVector<TypeRef*> implementedInterfaces;
    MVector<MStringView> friendClasses;
    MVector<TypeAliasDecl*> cppTypeAliases;
    MVector<FieldDecl*> fields;
    MVector<FieldDecl*> orderedFields;
    MVector<MethodDecl*> methods;
    MStringView assetTypeName;
    MStringView systemTypeName;
    MStringView subsystemTypeName;
    MStringView componentTypeName;
    SourceSpan span;
    usize totalSize = 1u;
    usize totalAlign = 1u;
    usize recordAlignment = 0u;
    const ClassDecl* concreteBase = nullptr;
    b8 isExtensible = false;
    b8 isSerialize = false;
    b8 isReflect = false;
    b8 isArenaOwner = false;
    b8 needsClassArena = false;
    MStringView cppNamespace;
    MStringView cppExportName;
    MStringView requiredHeader;
    b8 isImported = false;
    b8 isExtern = false;
    b8 isAbstract = false;
    b8 isCopyConstructible = true;
    b8 isMoveConstructible = true;
    b8 isCopyAssignable = true;
    b8 isMoveAssignable = true;
    b8 hasExplicitDestructor = false;
    b8 hasExplicitLayout = false;
    u8 arenaCtorMode = 0u;
    b8 isSelfDefaulted = false;
    b8 isSmallValue = false;
    b8 isBorrowView = false;
    b8 isBinarySerializable = false;
    b8 emitsBraceFieldInit = false;
    b8 isEqualitySwitch = false;
    b8 isNonThreadSafeContainer = false;
    b8 isNwbMapKey = false;
    b8 isPolymorphicBase = false;


public:
    explicit ClassDecl(MetaArena& arena)noexcept
        : templateParams(arena)
        , implementedInterfaces(arena)
        , friendClasses(arena)
        , cppTypeAliases(arena)
        , fields(arena)
        , orderedFields(arena)
        , methods(arena)
    {}

    [[nodiscard]] constexpr b8 usesStaticClassArena()const noexcept{
        return isArenaOwner
            && assetTypeName.empty()
            && systemTypeName.empty()
            && subsystemTypeName.empty();
    }

    [[nodiscard]] constexpr b8 canBorrowMotherArena()const noexcept{
        return !isSerialize && !usesStaticClassArena() && subsystemTypeName.empty();
    }
};

[[nodiscard]] constexpr const TemplateParamDecl* FindEnclosingTemplateParam(const ClassDecl* const ownerClass, const MethodDecl* const method, const MStringView name, const b8 wantConstValue)noexcept{
    if(method){
        if(const TemplateParamDecl* tp = FindTemplateParamByName(Span<const TemplateParamDecl>(method->templateParams.data(), method->templateParams.size()), name)){
            if((tp->constValueType != nullptr) == wantConstValue)
                return tp;
        }
    }
    if(ownerClass){
        if(const TemplateParamDecl* tp = FindTemplateParamByName(Span<const TemplateParamDecl>(ownerClass->templateParams.data(), ownerClass->templateParams.size()), name)){
            if((tp->constValueType != nullptr) == wantConstValue)
                return tp;
        }
    }
    return nullptr;
}

[[nodiscard]] constexpr b8 ShouldEmitCallableInHeader(const ClassDecl* const ownerClass, const MethodDecl& callable)noexcept{
    return callable.hasBody
        && ((ownerClass && !ownerClass->templateParams.empty())
            || !callable.templateParams.empty()
            || callable.isInline
            || callable.hasSynthesizedArenaParams);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ClassTemplateInstantiation{
    const ClassDecl* templateDecl = nullptr;
    MVector<TypeArg> templateArgs;
    MVector<FieldDecl*> orderedFields;
    usize totalSize = 1u;
    usize totalAlign = 1u;
    b8 needsClassArena = false;
    b8 isImported = false;


public:
    explicit ClassTemplateInstantiation(MetaArena& arena)noexcept
        : templateArgs(arena)
        , orderedFields(arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ImportedCppHeader{
    MStringView headerPath;
    SourceSpan span;
    b8 isSystemHeader = true;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct AssetRegistration{
    MStringView className;
    TypeRef* registrar;
    SourceSpan span;
};

struct RegistrationBinding{
    MStringView name;
    MStringView cppName;
    MStringView header;
    usize parameterCount = 0u;
};

struct RegistrationCall{
    MStringView cppName;
    MVector<MStringView> arguments;
};

struct StaticAssertion{
    Expr* condition;
    Expr* message;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct TranslationUnit{
    AstNodePool astNodePool;
    List<MString, MetaArena> substitutedConstantTexts;
    MStringView namespaceWrapper;
    MStringView cppNamespace;
    MVector<StaticAssertion> staticAssertions;
    MVector<AssetRegistration> assetRegistrations;
    MVector<RegistrationBinding> registrationBindings;
    MVector<RegistrationCall> registrationCalls;
    MVector<MStringView> registrationHeaders;
    MVector<MStringView> requiredGlobalHeaders;
    MVector<MStringView> extraPreambleHeaders;
    MVector<MString> includedAdhdFiles;
    MVector<MString> resolvedIncludedFiles;
    MVector<Stmt*> globalConstants;
    MVector<TypeAliasDecl*> typeAliases;
    MVector<EnumDecl*> enums;
    MVector<InterfaceDecl*> interfaces;
    MVector<ClassDecl*> classes;
    MVector<ClassTemplateInstantiation*> templateInstantiations;
    MVector<MethodDecl*> freeFunctions;
    MVector<MethodDecl*> outOfLineMethods;
    MVector<ImportedCppHeader> importedCppHeaders;


public:
    explicit TranslationUnit(MetaArena& arena)noexcept
        : astNodePool(arena)
        , substitutedConstantTexts(arena)
        , staticAssertions(arena)
        , assetRegistrations(arena)
        , registrationBindings(arena)
        , registrationCalls(arena)
        , registrationHeaders(arena)
        , requiredGlobalHeaders(arena)
        , extraPreambleHeaders(arena)
        , includedAdhdFiles(arena)
        , resolvedIncludedFiles(arena)
        , globalConstants(arena)
        , typeAliases(arena)
        , enums(arena)
        , interfaces(arena)
        , classes(arena)
        , templateInstantiations(arena)
        , freeFunctions(arena)
        , outOfLineMethods(arena)
        , importedCppHeaders(arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

