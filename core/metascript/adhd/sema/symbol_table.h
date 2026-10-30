// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../ast/ast_decl.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class SymbolTable final{
public:
    [[nodiscard]] static b8 IsArenaBackedContainer(MStringView typeName)noexcept;
    [[nodiscard]] static b8 IsSimdVectorType(MStringView typeName, u32& outLanes)noexcept;
    [[nodiscard]] static b8 IsUnalignedVectorType(MStringView typeName, u32& outLanes)noexcept;
    [[nodiscard]] static b8 IsUnalignedVectorType(MStringView typeName)noexcept;
    [[nodiscard]] static b8 IsSimdMatrixType(MStringView typeName)noexcept;
    [[nodiscard]] static b8 IsSimdType(MStringView typeName)noexcept;
    [[nodiscard]] static b8 IsStringSwitchType(MStringView typeName)noexcept;
    [[nodiscard]] static const FieldDecl* FindClassField(const ClassDecl& classDecl, MStringView name)noexcept;


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
    [[nodiscard]] MethodDecl* findFreeFunction(MStringView name, const usize* preferredArgCount = nullptr, Span<const MStringView> argTypeNames = {}, Span<const u8> argIsMut = {})const noexcept;
    [[nodiscard]] Stmt* findGlobalConstant(MStringView name)const noexcept;

    [[nodiscard]] const MethodDecl* findClassMethod(const ClassDecl& classDecl, MStringView name, const usize* preferredArgCount = nullptr, b8 preferConst = false, b8 requirePublic = false, Span<const MStringView> argTypeNames = {}, Span<const u8> argIsMut = {})const noexcept;
    [[nodiscard]] const MethodDecl* findClassOperator(const ClassDecl& classDecl, MStringView opName, const usize* preferredArgCount = nullptr, b8 preferConst = false, b8 requirePublic = false, Span<const MStringView> argTypeNames = {}, Span<const u8> argIsMut = {})const noexcept;
    [[nodiscard]] const MethodDecl* findClassConstructor(const ClassDecl& classDecl, usize argCount, b8 requirePublic = false, Span<const MStringView> argTypeNames = {}, Span<const u8> argIsMut = {})const noexcept;
    [[nodiscard]] MethodDecl* findInterfaceMethod(const TypeRef& ifaceType, MStringView methodName, const usize* preferredArgCount = nullptr, u32 depth = 0u, Span<const MStringView> argTypeNames = {}, Span<const u8> argIsMut = {})const noexcept;
    void collectAllInterfaceMethods(const TypeRef& ifaceType, u32 depth, MVector<MethodDecl*>& outMethods)const;
    void collectAllInterfaceMethods(const TypeRef& ifaceType, MVector<MethodDecl*>& outMethods)const{ collectAllInterfaceMethods(ifaceType, 0u, outMethods); }

    [[nodiscard]] TypeRef* substituteTypeParams(const TypeRef& origType, Span<const TemplateParamDecl> templateParams, Span<const TypeArg> templateArgs)const;
    [[nodiscard]] TypeRef* substituteTypeParams(const TypeRef* origType, Span<const TemplateParamDecl> templateParams, const TypeRef* instanceTypeRef)const;
    [[nodiscard]] const TypeRef* unwrapTypeAlias(const TypeRef* type)const noexcept;
    [[nodiscard]] ParamPassingMode::Enum resolveRefMode(const TypeRef* type)const noexcept;
    [[nodiscard]] MStringView resolveBaseTypeName(MStringView typeName)const noexcept;
    [[nodiscard]] b8 isSelfDefaultedType(MStringView typeName)const noexcept;
    [[nodiscard]] b8 isSmallValueType(MStringView typeName)const noexcept;
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

