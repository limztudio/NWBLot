// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "asset.h"
#include "cook_types.h"

#include <impl/assets/graphics/mesh/material_typed_constants.h>

#include <core/alloc/scratch.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace MaterialBindNames{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline AStringView SourceExtensionText()noexcept{
    static constexpr AStringView s_ExtensionText = ".bind";
    return s_ExtensionText;
}

inline AStringView GeneratedIncludeCacheDirectoryText()noexcept{
    static constexpr AStringView s_DirectoryText = "material_bind_includes";
    return s_DirectoryText;
}

inline AStringView TypedBindingImplicitDefineText()noexcept{
    static constexpr AStringView s_DefineText = "NWB_MATERIAL_TYPED_BINDING";
    return s_DefineText;
}

#define NWB_MATERIAL_TYPED_STRINGIFY_IMPL(Value) #Value
#define NWB_MATERIAL_TYPED_STRINGIFY(Value) NWB_MATERIAL_TYPED_STRINGIFY_IMPL(Value)

inline AStringView TypedBindingImplicitDefineValueText()noexcept{
    static constexpr AStringView s_DefineValueText = NWB_MATERIAL_TYPED_STRINGIFY(NWB_MATERIAL_TYPED_BINDING_REQUIRED_VALUE);
    return s_DefineValueText;
}

#undef NWB_MATERIAL_TYPED_STRINGIFY
#undef NWB_MATERIAL_TYPED_STRINGIFY_IMPL


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct MaterialBindAttribute{
    MaterialCookString name;
    MaterialCookVector<MaterialCookString> arguments;

    explicit MaterialBindAttribute(MaterialCookArena& memoryArena)noexcept
        : name(memoryArena)
        , arguments(memoryArena)
    {}
};

struct MaterialBindField{
    MaterialCookString type;
    MaterialCookString name;
    MaterialCookVector<MaterialBindAttribute> attributes;

    explicit MaterialBindField(MaterialCookArena& memoryArena)noexcept
        : type(memoryArena)
        , name(memoryArena)
        , attributes(memoryArena)
    {}


public:
    [[nodiscard]] const MaterialBindAttribute* findAttribute(AStringView attributeName)const noexcept;
    [[nodiscard]] AStringView defaultArgument()const noexcept;
};

struct MaterialBindStruct{
    MaterialCookString name;
    MaterialCookVector<MaterialBindAttribute> attributes;
    MaterialCookVector<MaterialBindField> fields;

    explicit MaterialBindStruct(MaterialCookArena& memoryArena)noexcept
        : name(memoryArena)
        , attributes(memoryArena)
        , fields(memoryArena)
    {}


public:
    [[nodiscard]] const MaterialBindField* findField(AStringView fieldName)const noexcept;
    [[nodiscard]] const MaterialBindAttribute* findAttribute(AStringView attributeName)const noexcept;
};

struct MaterialBindInstance{
    MaterialCookString type;
    MaterialCookString name;

    explicit MaterialBindInstance(MaterialCookArena& memoryArena)noexcept
        : type(memoryArena)
        , name(memoryArena)
    {}
};

struct MaterialBindEntry{
    MaterialCookString source;
    MaterialCookString virtualPath;
    MaterialCookVector<MaterialBindStruct> structs;
    MaterialCookVector<MaterialBindInstance> instances;

    explicit MaterialBindEntry(MaterialCookArena& memoryArena)noexcept
        : source(memoryArena)
        , virtualPath(memoryArena)
        , structs(memoryArena)
        , instances(memoryArena)
    {}


public:
    void reset()noexcept{
        source.clear();
        virtualPath.clear();
        structs.clear();
        instances.clear();
    }

    [[nodiscard]] const MaterialBindStruct* findStruct(AStringView typeName)const noexcept;
    [[nodiscard]] const MaterialBindInstance* findInstance(AStringView instanceName)const noexcept;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using MaterialBindParameterMap = MaterialCookMap<ACompactString, ACompactString>;

struct MaterialBindTypedLayoutBlockLookupEntry{
    u32 blockIndex = 0u;
    u32 byteBegin = 0u;
    u32 constantByteBegin = 0u;
};

struct MaterialBindTypedLayoutParameterLookupEntry{
    u32 fieldIndex = 0u;
    u32 byteOffset = 0u;
    u32 constantByteOffset = 0u;
    Name blockName = s_NameNone;
};

using MaterialBindTypedLayoutBlockLookup = MaterialCookMap<Name, MaterialBindTypedLayoutBlockLookupEntry>;
using MaterialBindTypedLayoutParameterLookup = MaterialCookMap<ACompactString, MaterialBindTypedLayoutParameterLookupEntry>;

struct MaterialBindTypedLayout{
    const MaterialBindEntry* bindEntry = nullptr;
    u64 layoutHash = 0u;
    Material::TypedLayoutBlockVector typedLayoutBlocks;
    Material::TypedLayoutFieldVector typedLayoutFields;
    Material::TypedBlockByteVector typedBlockBytes;
    MaterialBindTypedLayoutBlockLookup blockLookup;
    MaterialBindTypedLayoutParameterLookup parameterLookup;

    explicit MaterialBindTypedLayout(MaterialCookArena& memoryArena)
        : typedLayoutBlocks(memoryArena)
        , typedLayoutFields(memoryArena)
        , typedBlockBytes(memoryArena)
        , blockLookup(0, Hasher<Name>(), EqualTo<Name>(), memoryArena)
        , parameterLookup(0, Hasher<ACompactString>(), EqualTo<ACompactString>(), memoryArena)
    {}


public:
    void reset()noexcept;
};

struct MaterialBindTypedLayoutCache{
    MaterialCookVector<MaterialBindTypedLayout> entries;
    MaterialCookMap<Name, usize> lookup;

    explicit MaterialBindTypedLayoutCache(MaterialCookArena& memoryArena)
        : entries(memoryArena)
        , lookup(0, Hasher<Name>(), EqualTo<Name>(), memoryArena)
    {}


public:
    void reserve(usize count);
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Expected<MaterialBindEntry> ParseMaterialBindSource(const Path& bindFilePath, MaterialCookArena& arena, Core::Alloc::ScratchArena& scratchArena);
[[nodiscard]] Expected<ACompactString> BuildMaterialBindParameterKey(AStringView instanceName, AStringView fieldName)noexcept;
[[nodiscard]] u64 ComputeMaterialBindParameterKeyHash(AStringView parameterKey)noexcept;
[[nodiscard]] Expected<MaterialBindTypedLayout> BuildMaterialBindTypedLayout(const MaterialBindEntry& bindEntry, const Name& contextName, MaterialCookArena& arena, Core::Alloc::ScratchArena& scratchArena);
[[nodiscard]] Expected<const MaterialBindTypedLayout*> FindOrBuildMaterialBindTypedLayout(const Name& materialInterface, const MaterialBindEntry& bindEntry, MaterialBindTypedLayoutCache& inOutCache, Core::Alloc::ScratchArena& scratchArena);
void CopyMaterialBindTypedLayoutDefaults(const MaterialBindTypedLayout& layout, u64& outLayoutHash, Material::TypedLayoutBlockVector& outBlocks, Material::TypedLayoutFieldVector& outFields, Material::TypedBlockByteVector& outBlockBytes);
[[nodiscard]] bool ApplyMaterialBindTypedLayoutParameters(
    const MaterialBindTypedLayout& layout,
    const Name& materialName,
    const MaterialBindParameterMap& parameters,
    Material::TypedBlockByteVector& inOutBlockBytes,
    Material::ResourceReferenceVector& outResourceReferences
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

