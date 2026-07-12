
#pragma once


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "asset.h"
#include "cook_types.h"

#include <global/core/alloc/scratch.h>
#include <impl/assets/graphics/mesh/material_typed_constants.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace MaterialBindNames{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline AStringView SourceExtensionText(){
    static constexpr AStringView s_ExtensionText = ".bind";
    return s_ExtensionText;
}

inline AStringView GeneratedIncludeCacheDirectoryText(){
    static constexpr AStringView s_DirectoryText = "material_bind_includes";
    return s_DirectoryText;
}

inline AStringView TypedBindingImplicitDefineText(){
    static constexpr AStringView s_DefineText = "NWB_MATERIAL_TYPED_BINDING";
    return s_DefineText;
}

#define NWB_MATERIAL_TYPED_STRINGIFY_IMPL(Value) #Value
#define NWB_MATERIAL_TYPED_STRINGIFY(Value) NWB_MATERIAL_TYPED_STRINGIFY_IMPL(Value)

inline AStringView TypedBindingImplicitDefineValueText(){
    static constexpr AStringView s_DefineValueText = NWB_MATERIAL_TYPED_STRINGIFY(NWB_MATERIAL_TYPED_BINDING_REQUIRED_VALUE);
    return s_DefineValueText;
}

#undef NWB_MATERIAL_TYPED_STRINGIFY
#undef NWB_MATERIAL_TYPED_STRINGIFY_IMPL


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct MaterialBindAttribute{
    MaterialCookString name;
    MaterialCookVector<MaterialCookString> arguments;

    explicit MaterialBindAttribute(MaterialCookArena& memoryArena)
        : name(memoryArena)
        , arguments(memoryArena)
    {}
};

struct MaterialBindField{
    MaterialCookString type;
    MaterialCookString name;
    MaterialCookVector<MaterialBindAttribute> attributes;

    explicit MaterialBindField(MaterialCookArena& memoryArena)
        : type(memoryArena)
        , name(memoryArena)
        , attributes(memoryArena)
    {}

    [[nodiscard]] const MaterialBindAttribute* findAttribute(AStringView attributeName)const;
    [[nodiscard]] AStringView defaultArgument()const;
};

struct MaterialBindStruct{
    MaterialCookString name;
    MaterialCookVector<MaterialBindAttribute> attributes;
    MaterialCookVector<MaterialBindField> fields;

    explicit MaterialBindStruct(MaterialCookArena& memoryArena)
        : name(memoryArena)
        , attributes(memoryArena)
        , fields(memoryArena)
    {}

    [[nodiscard]] const MaterialBindField* findField(AStringView fieldName)const;
    [[nodiscard]] const MaterialBindAttribute* findAttribute(AStringView attributeName)const;
};

struct MaterialBindInstance{
    MaterialCookString type;
    MaterialCookString name;

    explicit MaterialBindInstance(MaterialCookArena& memoryArena)
        : type(memoryArena)
        , name(memoryArena)
    {}
};

struct MaterialBindEntry{
    MaterialCookString source;
    MaterialCookString virtualPath;
    MaterialCookVector<MaterialBindStruct> structs;
    MaterialCookVector<MaterialBindInstance> instances;

    explicit MaterialBindEntry(MaterialCookArena& memoryArena)
        : source(memoryArena)
        , virtualPath(memoryArena)
        , structs(memoryArena)
        , instances(memoryArena)
    {}

    void reset(){
        source.clear();
        virtualPath.clear();
        structs.clear();
        instances.clear();
    }

    [[nodiscard]] const MaterialBindStruct* findStruct(AStringView typeName)const;
    [[nodiscard]] const MaterialBindInstance* findInstance(AStringView instanceName)const;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using MaterialBindParameterMap = MaterialCookMap<ACompactString, ACompactString>;

struct MaterialBindTypedLayoutBlockLookupEntry{
    u32 blockIndex = 0u;
    u32 byteBegin = 0u;
};

struct MaterialBindTypedLayoutParameterLookupEntry{
    u32 fieldIndex = 0u;
    u32 byteOffset = 0u;
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

    void reset();
};

struct MaterialBindTypedLayoutCache{
    MaterialCookVector<MaterialBindTypedLayout> entries;
    MaterialCookMap<Name, usize> lookup;

    explicit MaterialBindTypedLayoutCache(MaterialCookArena& memoryArena)
        : entries(memoryArena)
        , lookup(0, Hasher<Name>(), EqualTo<Name>(), memoryArena)
    {}

    void reserve(usize count);
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool ParseMaterialBindSource(
    const Path& bindFilePath,
    MaterialBindEntry& outEntry,
    Core::Alloc::ScratchArena& scratchArena
);
[[nodiscard]] bool BuildMaterialBindParameterKey(
    AStringView instanceName,
    AStringView fieldName,
    ACompactString& outKey
);
[[nodiscard]] u64 ComputeMaterialBindParameterKeyHash(AStringView parameterKey);
[[nodiscard]] bool BuildMaterialBindTypedLayout(
    const MaterialBindEntry& bindEntry,
    const Name& contextName,
    MaterialBindTypedLayout& outLayout,
    Core::Alloc::ScratchArena& scratchArena
);
[[nodiscard]] bool FindOrBuildMaterialBindTypedLayout(
    const Name& materialInterface,
    const MaterialBindEntry& bindEntry,
    MaterialBindTypedLayoutCache& inOutCache,
    const MaterialBindTypedLayout*& outLayout,
    Core::Alloc::ScratchArena& scratchArena
);
void CopyMaterialBindTypedLayoutDefaults(
    const MaterialBindTypedLayout& layout,
    u64& outLayoutHash,
    Material::TypedLayoutBlockVector& outBlocks,
    Material::TypedLayoutFieldVector& outFields,
    Material::TypedBlockByteVector& outBlockBytes
);
[[nodiscard]] bool ApplyMaterialBindTypedLayoutParameters(
    const MaterialBindTypedLayout& layout,
    const Name& materialName,
    const MaterialBindParameterMap& parameters,
    Material::TypedBlockByteVector& inOutBlockBytes
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

