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
    [[nodiscard]] bool build(TranslationUnit& unit, MVector<ParseError>& errors);

    [[nodiscard]] EnumDecl* findEnum(MStringView name)const noexcept;
    [[nodiscard]] InterfaceDecl* findInterface(MStringView name)const noexcept;
    [[nodiscard]] ClassDecl* findClass(MStringView name)const noexcept;
    [[nodiscard]] TypeAliasDecl* findTypeAlias(MStringView name)const noexcept;
    [[nodiscard]] MethodDecl* findFreeFunction(MStringView name)const noexcept;
    [[nodiscard]] Stmt* findGlobalConstant(MStringView name)const noexcept;

    [[nodiscard]] MethodDecl* findInterfaceMethod(MStringView interfaceName, MStringView methodName)const noexcept;
    void collectAllInterfaceMethods(MStringView interfaceName, MVector<MethodDecl*>& outMethods)const;

    [[nodiscard]] static bool isArenaBackedContainer(MStringView typeName)noexcept;
    [[nodiscard]] static bool isSimdVectorType(MStringView typeName, u32& outLanes)noexcept;
    [[nodiscard]] static bool isSimdMatrixType(MStringView typeName)noexcept;
    [[nodiscard]] static bool isSimdType(MStringView typeName)noexcept;
    [[nodiscard]] static bool isStringSwitchType(MStringView typeName)noexcept;
    [[nodiscard]] static MStringView mapBuiltinCppType(MStringView typeName)noexcept;


private:
    [[nodiscard]] bool hasTypeSymbol(MStringView name)const noexcept;


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

