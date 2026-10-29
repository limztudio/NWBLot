// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../ast/ast_decl.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class SymbolTable{
public:
    explicit SymbolTable(MetaArena& arena)noexcept
        : m_arena(arena)
        , m_enums(arena)
        , m_interfaces(arena)
        , m_classes(arena)
        , m_typeAliases(arena)
        , m_freeFunctions(arena)
        , m_globalConstants(arena)
    {}


public:
    [[nodiscard]] b8 build(TranslationUnit& unit, MVector<ParseError>& errors);

    [[nodiscard]] EnumDecl* findEnum(MStringView name)const noexcept;
    [[nodiscard]] InterfaceDecl* findInterface(MStringView name)const noexcept;
    [[nodiscard]] ClassDecl* findClass(MStringView name)const noexcept;
    [[nodiscard]] TypeAliasDecl* findTypeAlias(MStringView name)const noexcept;
    [[nodiscard]] MethodDecl* findFreeFunction(
        MStringView name,
        const usize* preferredArgCount = nullptr,
        const MVector<MStringView>* argTypeNames = nullptr,
        const MVector<b8>* argIsMut = nullptr
    )const noexcept;
    [[nodiscard]] Stmt* findGlobalConstant(MStringView name)const noexcept;

    [[nodiscard]] MethodDecl* findInterfaceMethod(
        const TypeRef& ifaceType,
        MStringView methodName,
        const usize* preferredArgCount = nullptr,
        u32 depth = 0u,
        const MVector<MStringView>* argTypeNames = nullptr,
        const MVector<b8>* argIsMut = nullptr
    )const noexcept;
    void collectAllInterfaceMethods(const TypeRef& ifaceType, MVector<MethodDecl*>& outMethods, u32 depth = 0u)const;

    [[nodiscard]] TypeRef* substituteTypeParams(
        const TypeRef& origType,
        const MVector<TemplateParamDecl>& templateParams,
        const MVector<TypeArg>& templateArgs
    )const;
    [[nodiscard]] const TypeRef* unwrapTypeAlias(const TypeRef* type)const noexcept;
    [[nodiscard]] ParamPassingMode::Enum resolveRefMode(const TypeRef* type)const noexcept;
    [[nodiscard]] MStringView resolveBaseTypeName(MStringView typeName)const noexcept;
    [[nodiscard]] b8 isSelfDefaultedType(MStringView typeName)const noexcept;

    [[nodiscard]] static b8 isArenaBackedContainer(MStringView typeName)noexcept;
    [[nodiscard]] static b8 isSimdVectorType(MStringView typeName, u32& outLanes)noexcept;
    [[nodiscard]] static b8 isUnalignedVectorType(MStringView typeName, u32& outLanes)noexcept;
    [[nodiscard]] static b8 isUnalignedVectorType(MStringView typeName)noexcept;
    [[nodiscard]] static b8 isSimdMatrixType(MStringView typeName)noexcept;
    [[nodiscard]] static b8 isSimdType(MStringView typeName)noexcept;
    [[nodiscard]] static b8 isStringSwitchType(MStringView typeName)noexcept;
    [[nodiscard]] static MStringView mapBuiltinCppType(MStringView typeName)noexcept;
    [[nodiscard]] b8 hasTypeSymbol(MStringView name)const noexcept;


private:
    MetaArena& m_arena;
    MVector<EnumDecl*> m_enums;
    MVector<InterfaceDecl*> m_interfaces;
    MVector<ClassDecl*> m_classes;
    MVector<TypeAliasDecl*> m_typeAliases;
    MVector<MethodDecl*> m_freeFunctions;
    MVector<Stmt*> m_globalConstants;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

