// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../ast/ast_decl.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct BuiltinReceiverMethodInfo;


class SymbolTable final{
public:
    [[nodiscard]] static b8 IsArenaBackedContainer(MStringView typeName)noexcept;
    [[nodiscard]] static b8 IsSimdRegisterVectorType(MStringView typeName)noexcept;
    [[nodiscard]] static b8 IsSimdRegisterMatrixType(MStringView typeName)noexcept;
    [[nodiscard]] static b8 IsSimdRegisterType(MStringView typeName)noexcept;
    [[nodiscard]] static b8 IsSimdStorageVectorType(MStringView typeName, u32& outLanes)noexcept;
    [[nodiscard]] static b8 IsSimdStorageMatrixType(MStringView typeName)noexcept;
    [[nodiscard]] static b8 IsSimdStorageType(MStringView typeName)noexcept;
    [[nodiscard]] static b8 IsSimdVectorType(MStringView typeName, u32& outLanes)noexcept;
    [[nodiscard]] static b8 IsUnalignedVectorType(MStringView typeName, u32& outLanes)noexcept;
    [[nodiscard]] static b8 IsUnalignedVectorType(MStringView typeName)noexcept;
    [[nodiscard]] static b8 IsSimdMatrixType(MStringView typeName)noexcept;
    [[nodiscard]] static b8 IsSimdType(MStringView typeName)noexcept;
    [[nodiscard]] static b8 IsSimdOrUnalignedStorageType(MStringView typeName)noexcept;
    [[nodiscard]] static b8 IsSimdStorageOrUnalignedVectorType(MStringView typeName, u32& outLanes)noexcept;
    [[nodiscard]] static b8 IsStringSwitchType(MStringView typeName)noexcept;
    [[nodiscard]] static const FieldDecl* FindClassField(const ClassDecl& classDecl, MStringView name)noexcept;
    [[nodiscard]] b8 canAccessMember(const ClassDecl& receiver, const ClassDecl* caller, const ClassDecl* declaringClass, AccessSpecifier::Enum access)const noexcept;
    [[nodiscard]] b8 canAccessPrivateMembers(const ClassDecl& target, const ClassDecl* caller)const noexcept;
    [[nodiscard]] static b8 HasCustomAssignOperator(const ClassDecl& classDecl)noexcept;


public:
    explicit SymbolTable(MetaArena& arena, TranslationUnit& unit)noexcept
        : m_arena(arena)
        , m_unit(unit)
        , m_typeMap(arena)
        , m_globalConstantMap(arena)
        , m_freeFunctions(arena)
        , m_freeFunctionHead(arena)
        , m_freeFunctionNext(arena)
    {}


public:
    [[nodiscard]] b8 build(TranslationUnit& unit, MVector<ParseError>& errors);

    [[nodiscard]] EnumDecl* findEnum(MStringView name)const noexcept;
    [[nodiscard]] InterfaceDecl* findInterface(MStringView name)const noexcept;
    [[nodiscard]] ClassDecl* findClass(MStringView name)const noexcept;
    [[nodiscard]] TypeAliasDecl* findTypeAlias(MStringView name)const noexcept;
    [[nodiscard]] MethodDecl* findFreeFunction(MStringView name, const usize* preferredArgCount = nullptr, Span<const MStringView> argTypeNames = {}, Span<const u8> argIsMut = {})const noexcept;
    [[nodiscard]] const MethodDecl* findUniqueFreeFunction(MStringView name, b8& outAmbiguous)const noexcept;
    [[nodiscard]] Stmt* findGlobalConstant(MStringView name)const noexcept;
    [[nodiscard]] b8 hasMultipleFreeFunctions(MStringView name)const noexcept;
    [[nodiscard]] b8 hasMultipleClassMethods(const ClassDecl& classDecl, MStringView name, b8 staticOnly = false)const noexcept;
    [[nodiscard]] b8 hasMultipleInterfaceMethods(const TypeRef& ifaceType, MStringView methodName, u32 depth = 0u)const noexcept;

    [[nodiscard]] const MethodDecl* findClassMethod(
        const ClassDecl& classDecl,
        MStringView name,
        const usize* preferredArgCount = nullptr,
        b8 preferConst = false,
        b8 requirePublic = false,
        Span<const MStringView> argTypeNames = {},
        Span<const u8> argIsMut = {},
        b8 staticOnly = false
    )const noexcept;
    [[nodiscard]] const MethodDecl* findClassOperator(
        const ClassDecl& classDecl,
        MStringView opName,
        const usize* preferredArgCount = nullptr,
        b8 preferConst = false,
        b8 requirePublic = false,
        Span<const MStringView> argTypeNames = {},
        Span<const u8> argIsMut = {}
    )const noexcept;
    [[nodiscard]] const MethodDecl* findClassConstructor(const ClassDecl& classDecl, usize argCount, b8 requirePublic = false, Span<const MStringView> argTypeNames = {}, Span<const u8> argIsMut = {})const noexcept;
    [[nodiscard]] MethodDecl* findInterfaceMethod(const TypeRef& ifaceType, MStringView methodName, const usize* preferredArgCount = nullptr, u32 depth = 0u, Span<const MStringView> argTypeNames = {}, Span<const u8> argIsMut = {})const noexcept;
    void collectAllInterfaceMethods(const TypeRef& ifaceType, u32 depth, MVector<MethodDecl*>& outMethods)const;
    void collectAllInterfaceMethods(const TypeRef& ifaceType, MVector<MethodDecl*>& outMethods)const{ collectAllInterfaceMethods(ifaceType, 0u, outMethods); }

    [[nodiscard]] TypeRef* substituteTypeParams(const TypeRef& origType, Span<const TemplateParamDecl> templateParams, Span<const TypeArg> templateArgs)const;
    [[nodiscard]] TypeRef* substituteTypeParams(const TypeRef* origType, Span<const TemplateParamDecl> templateParams, const TypeRef* instanceTypeRef)const;
    [[nodiscard]] const TypeRef* unwrapTypeAlias(const TypeRef* type)const noexcept;
    [[nodiscard]] ParamPassingMode::Enum resolveRefMode(const TypeRef* type)const noexcept;
    [[nodiscard]] MStringView resolveBaseTypeName(MStringView typeName)const noexcept;
    [[nodiscard]] b8 isSelfDefaultedType(MStringView typeName, const ClassDecl* callerClass = nullptr)const noexcept;
    [[nodiscard]] b8 isSmallValueType(MStringView typeName)const noexcept;
    [[nodiscard]] MStringView findTypeNamespace(MStringView name)const noexcept;
    [[nodiscard]] b8 hasTypeSymbol(MStringView name)const noexcept;
    [[nodiscard]] b8 isEnumType(MStringView name)const noexcept;
    [[nodiscard]] b8 isInterfaceOrAbstractType(MStringView name)const noexcept;
    [[nodiscard]] b8 isAbstractBuiltinType(MStringView name)const noexcept;
    [[nodiscard]] b8 isPolymorphicBaseBuiltinType(MStringView name)const noexcept;
    [[nodiscard]] b8 hasPolymorphicInterface(const ClassDecl& classDecl)const noexcept;
    [[nodiscard]] b8 hasPolymorphicBase(const ClassDecl& classDecl)const noexcept;
    [[nodiscard]] const BuiltinReceiverMethodInfo* findInheritedBuiltinReceiverMethod(const ClassDecl* cls, const InterfaceDecl* iface, MStringView methodName)const noexcept;
    [[nodiscard]] MStringView resolveRoleScopeMemberType(MStringView scopeName, MStringView memberName, b8 allowStaticFields)const noexcept;


private:
    struct TypeSymbolKind{
        enum Enum : u8{
            EnumDeclKind,
            InterfaceDeclKind,
            ClassDeclKind,
            TypeAliasDeclKind,
        };
    };

    struct TypeSymbolEntry{
        TypeSymbolKind::Enum kind = TypeSymbolKind::EnumDeclKind;
        void* decl = nullptr;
        MStringView cppNamespace;
    };

    static constexpr usize s_InvalidFreeFunctionIndex = Limit<usize>::s_Max;


private:
    void countInterfaceMethodsUpToTwo(const TypeRef& ifaceType, MStringView methodName, u32 depth, u32& inOutCount)const noexcept;


private:
    MetaArena& m_arena;
    TranslationUnit& m_unit;
    HashMap<MStringView, TypeSymbolEntry, MetaArena, MStringViewHash> m_typeMap;
    HashMap<MStringView, Stmt*, MetaArena, MStringViewHash> m_globalConstantMap;
    MVector<MethodDecl*> m_freeFunctions;
    HashMap<MStringView, usize, MetaArena, MStringViewHash> m_freeFunctionHead;
    MVector<usize> m_freeFunctionNext;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

