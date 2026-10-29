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
    b8 isFlag = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct EnumDecl{
    MStringView name;
    MStringView sizeTypeName;
    MVector<EnumEntry> entries;
    SourceSpan span;
    usize underlyingSize = 1u;
    usize underlyingAlign = 1u;
    b8 isReflect = false;
    b8 isImported = false;


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
    b8 isImported = false;


public:
    explicit TypeAliasDecl(MetaArena& arena)noexcept
        : templateParams(arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct FieldDecl{
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
    b8 isTransient = false;
    b8 isReflect = false;


public:
    explicit FieldDecl(MetaArena& arena)noexcept
        : ctorArgs(arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct MethodDecl{
    AccessSpecifier::Enum access = AccessSpecifier::None;
    MStringView ownerClassName;
    MVector<MStringView> ownerTemplateParams;
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
    b8 isForceInline = false;
    b8 isNoInline = false;
    b8 isOutOfLine = false;
    b8 isReflect = false;
    b8 usesScratchArena = false;
    b8 usesTaskScope = false;
    b8 isImported = false;


public:
    explicit MethodDecl(MetaArena& arena)noexcept
        : ownerTemplateParams(arena)
        , templateParams(arena)
        , params(arena)
        , body(arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct InterfaceDecl{
    MStringView name;
    MVector<TemplateParamDecl> templateParams;
    MVector<TypeRef*> baseInterfaces;
    MVector<MethodDecl*> methods;
    MStringView assetTypeName;
    SourceSpan span;
    b8 isReflect = false;
    b8 isImported = false;


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
    MVector<FieldDecl*> fields;
    MVector<FieldDecl*> orderedFields;
    MVector<MethodDecl*> methods;
    MStringView assetTypeName;
    MStringView subsystemTypeName;
    MStringView componentAssetTypeName;
    SourceSpan span;
    usize totalSize = 1u;
    usize totalAlign = 1u;
    b8 isSerialize = false;
    b8 isReflect = false;
    b8 needsClassArena = false;
    b8 isImported = false;


public:
    explicit ClassDecl(MetaArena& arena)noexcept
        : templateParams(arena)
        , implementedInterfaces(arena)
        , fields(arena)
        , orderedFields(arena)
        , methods(arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ClassTemplateInstantiation{
    const ClassDecl* templateDecl = nullptr;
    MVector<TypeArg> templateArgs;
    MVector<FieldDecl*> orderedFields;
    usize totalSize = 1u;
    usize totalAlign = 1u;
    b8 isImported = false;


public:
    explicit ClassTemplateInstantiation(MetaArena& arena)noexcept
        : templateArgs(arena)
        , orderedFields(arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct TranslationUnit{
    MVector<MStringView> topLevelNativeBlocks;
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


public:
    explicit TranslationUnit(MetaArena& arena)noexcept
        : topLevelNativeBlocks(arena)
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
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

