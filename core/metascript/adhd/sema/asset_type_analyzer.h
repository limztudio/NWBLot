// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "type_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AssetDependencyFieldKind{
    enum Enum : u8{
        None,
        AssetHandle,
        Class,
        LinearContainer,
        MapContainer,
    };
};

struct ReflectedAssetDependencyField{
    AssetDependencyFieldKind::Enum kind = AssetDependencyFieldKind::None;
    b8 isOptional = false;
    const TypeRef* effectiveType = nullptr;
    MStringView effectiveTypeName;
    const TypeRef* elementType = nullptr;
    MStringView elementTypeName;
};


class AssetTypeAnalyzer final{
public:
    explicit AssetTypeAnalyzer(const SymbolTable& symbols)noexcept
        : m_symbols(symbols)
    {}

    [[nodiscard]] const MethodDecl* findCookEntryReader(MStringView name, b8 acceptsValue)const noexcept;
    [[nodiscard]] const TypeRef* findImplementedAssetInterface(const ClassDecl& cls)const noexcept;
    [[nodiscard]] const MethodDecl* findAssetLoadBinaryMethod(const ClassDecl& cls, b8 requireLocalDefinition = false)const noexcept;
    [[nodiscard]] b8 isAssetDependencyType(const TypeRef* type, u32 depth = 0u)const noexcept;
    [[nodiscard]] b8 classifyReflectedAssetDependencyField(const FieldDecl& field, ReflectedAssetDependencyField& outField)const noexcept;
    [[nodiscard]] b8 tryResolveSubsystemFieldAssetType(const FieldDecl& field, MStringView& outAssetTypeName, b8& outIsVector)const noexcept;


private:
    const SymbolTable& m_symbols;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

