// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "material_asset.h"

#include <impl/assets_shader/shader_cook.h>


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

inline AStringView TypedBindingImplicitDefineValueText(){
    static constexpr AStringView s_DefineValueText = "1";
    return s_DefineValueText;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct MaterialBindAttribute{
    ShaderCook::CookString name;
    ShaderCook::CookVector<ShaderCook::CookString> arguments;

    explicit MaterialBindAttribute(ShaderCook::CookArena& memoryArena)
        : name(memoryArena)
        , arguments(memoryArena)
    {}
};

struct MaterialBindField{
    ShaderCook::CookString type;
    ShaderCook::CookString name;
    ShaderCook::CookVector<MaterialBindAttribute> attributes;

    explicit MaterialBindField(ShaderCook::CookArena& memoryArena)
        : type(memoryArena)
        , name(memoryArena)
        , attributes(memoryArena)
    {}

    [[nodiscard]] const MaterialBindAttribute* findAttribute(AStringView attributeName)const;
    [[nodiscard]] AStringView defaultArgument()const;
};

struct MaterialBindStruct{
    ShaderCook::CookString name;
    ShaderCook::CookVector<MaterialBindAttribute> attributes;
    ShaderCook::CookVector<MaterialBindField> fields;

    explicit MaterialBindStruct(ShaderCook::CookArena& memoryArena)
        : name(memoryArena)
        , attributes(memoryArena)
        , fields(memoryArena)
    {}

    [[nodiscard]] const MaterialBindField* findField(AStringView fieldName)const;
    [[nodiscard]] const MaterialBindAttribute* findAttribute(AStringView attributeName)const;
};

struct MaterialBindInstance{
    ShaderCook::CookString type;
    ShaderCook::CookString name;

    explicit MaterialBindInstance(ShaderCook::CookArena& memoryArena)
        : type(memoryArena)
        , name(memoryArena)
    {}
};

struct MaterialBindEntry{
    ShaderCook::CookString source;
    ShaderCook::CookString virtualPath;
    ShaderCook::CookVector<MaterialBindStruct> structs;
    ShaderCook::CookVector<MaterialBindInstance> instances;

    explicit MaterialBindEntry(ShaderCook::CookArena& memoryArena)
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


using MaterialBindParameterMap = ShaderCook::CookMap<CompactString, CompactString>;

struct MaterialBindTypedLayoutBlockLookupEntry{
    u32 blockIndex = 0u;
    u32 byteBegin = 0u;
};

struct MaterialBindTypedLayoutParameterLookupEntry{
    u32 fieldIndex = 0u;
    u32 byteOffset = 0u;
};

using MaterialBindTypedLayoutBlockLookup = ShaderCook::CookMap<Name, MaterialBindTypedLayoutBlockLookupEntry>;
using MaterialBindTypedLayoutParameterLookup = ShaderCook::CookMap<CompactString, MaterialBindTypedLayoutParameterLookupEntry>;

struct MaterialBindTypedLayout{
    const MaterialBindEntry* bindEntry = nullptr;
    u64 layoutHash = 0u;
    Material::TypedLayoutBlockVector typedLayoutBlocks;
    Material::TypedLayoutFieldVector typedLayoutFields;
    Material::TypedBlockByteVector typedBlockBytes;
    MaterialBindTypedLayoutBlockLookup blockLookup;
    MaterialBindTypedLayoutParameterLookup parameterLookup;

    explicit MaterialBindTypedLayout(ShaderCook::CookArena& memoryArena)
        : typedLayoutBlocks(memoryArena)
        , typedLayoutFields(memoryArena)
        , typedBlockBytes(memoryArena)
        , blockLookup(0, Hasher<Name>(), EqualTo<Name>(), memoryArena)
        , parameterLookup(0, Hasher<CompactString>(), EqualTo<CompactString>(), memoryArena)
    {}

    void reset();
};

struct MaterialBindTypedLayoutCache{
    ShaderCook::CookVector<MaterialBindTypedLayout> entries;
    ShaderCook::CookMap<Name, usize> lookup;

    explicit MaterialBindTypedLayoutCache(ShaderCook::CookArena& memoryArena)
        : entries(memoryArena)
        , lookup(0, Hasher<Name>(), EqualTo<Name>(), memoryArena)
    {}

    void reserve(usize count);
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool ParseMaterialBindSource(const Path& bindFilePath, MaterialBindEntry& outEntry);
[[nodiscard]] bool BuildMaterialBindParameterKey(
    AStringView instanceName,
    AStringView fieldName,
    CompactString& outKey
);
[[nodiscard]] u64 ComputeMaterialBindParameterKeyHash(AStringView parameterKey);
[[nodiscard]] bool BuildMaterialBindTypedLayout(
    const MaterialBindEntry& bindEntry,
    const Name& contextName,
    MaterialBindTypedLayout& outLayout
);
[[nodiscard]] bool FindOrBuildMaterialBindTypedLayout(
    const Name& materialInterface,
    const MaterialBindEntry& bindEntry,
    MaterialBindTypedLayoutCache& inOutCache,
    const MaterialBindTypedLayout*& outLayout
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

