// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "asset.h"
#include <impl/assets_material/cook_types.adhd.h>

#include <core/alloc/global.h>
#include <core/alloc/scratch.h>

#include <global/algorithm.h>
#include <global/not_null.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using MaterialCookArena = Core::Alloc::GlobalArena;
using MaterialCookString = AString<MaterialCookArena>;

template<typename T>
using MaterialCookVector = Vector<T, MaterialCookArena>;

template<typename T, typename V>
using MaterialCookMap = HashMap<T, V, MaterialCookArena, Hasher<T>, EqualTo<T>>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace MaterialBindDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ItemT>
[[nodiscard]] inline const ItemT* FindNamedBindItem(const Span<const ItemT> items, const AStringView itemName)noexcept{
    return FindIfPointer(items, [itemName](const ItemT& item)noexcept{ return AStringView(item.name) == itemName; });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct MaterialBindAttribute{
    MaterialCookString name;
    MaterialCookVector<MaterialCookString> arguments;


public:
    explicit MaterialBindAttribute(MaterialCookArena& memoryArena)noexcept
        : name(memoryArena)
        , arguments(memoryArena)
    {}
};

struct MaterialBindField{
    MaterialCookString type;
    MaterialCookString name;
    MaterialCookVector<MaterialBindAttribute> attributes;


public:
    explicit MaterialBindField(MaterialCookArena& memoryArena)noexcept
        : type(memoryArena)
        , name(memoryArena)
        , attributes(memoryArena)
    {}


public:
    [[nodiscard]] const MaterialBindAttribute* findAttribute(const AStringView attributeName)const noexcept{
        return MaterialBindDetail::FindNamedBindItem<MaterialBindAttribute>(attributes, attributeName);
    }
    [[nodiscard]] AStringView defaultArgument()const noexcept{
        const MaterialBindAttribute* attribute = findAttribute(s_MaterialDefaultAttribute);
        return (attribute && attribute->arguments.size() == 1u) ? AStringView(attribute->arguments[0u]) : AStringView();
    }
};

struct MaterialBindStruct{
    MaterialCookString name;
    MaterialCookVector<MaterialBindAttribute> attributes;
    MaterialCookVector<MaterialBindField> fields;


public:
    explicit MaterialBindStruct(MaterialCookArena& memoryArena)noexcept
        : name(memoryArena)
        , attributes(memoryArena)
        , fields(memoryArena)
    {}


public:
    [[nodiscard]] const MaterialBindField* findField(const AStringView fieldName)const noexcept{
        return MaterialBindDetail::FindNamedBindItem<MaterialBindField>(fields, fieldName);
    }
    [[nodiscard]] const MaterialBindAttribute* findAttribute(const AStringView attributeName)const noexcept{
        return MaterialBindDetail::FindNamedBindItem<MaterialBindAttribute>(attributes, attributeName);
    }
};

struct MaterialBindInstance{
    MaterialCookString type;
    MaterialCookString name;


public:
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


public:
    explicit MaterialBindEntry(MaterialCookArena& memoryArena)noexcept
        : source(memoryArena)
        , virtualPath(memoryArena)
        , structs(memoryArena)
        , instances(memoryArena)
    {}


public:
    [[nodiscard]] const MaterialBindStruct* findStruct(const AStringView typeName)const noexcept{
        return MaterialBindDetail::FindNamedBindItem<MaterialBindStruct>(structs, typeName);
    }
    [[nodiscard]] const MaterialBindInstance* findInstance(const AStringView instanceName)const noexcept{
        return MaterialBindDetail::FindNamedBindItem<MaterialBindInstance>(instances, instanceName);
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using MaterialBindParameterMap = MaterialCookMap<ACompactString, ACompactString>;
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


public:
    explicit MaterialBindTypedLayout(MaterialCookArena& memoryArena)
        : typedLayoutBlocks(memoryArena)
        , typedLayoutFields(memoryArena)
        , typedBlockBytes(memoryArena)
        , blockLookup(0, Hasher<Name>(), EqualTo<Name>(), memoryArena)
        , parameterLookup(0, Hasher<ACompactString>(), EqualTo<ACompactString>(), memoryArena)
    {}
};

struct MaterialBindTypedLayoutCache{
    MaterialCookVector<MaterialBindTypedLayout> entries;
    MaterialCookMap<Name, usize> lookup;


public:
    explicit MaterialBindTypedLayoutCache(MaterialCookArena& memoryArena)
        : entries(memoryArena)
        , lookup(0, Hasher<Name>(), EqualTo<Name>(), memoryArena)
    {}


public:
    void reserve(const usize count){
        entries.reserve(count);
        lookup.reserve(count);
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Expected<MaterialBindEntry> ParseMaterialBindSource(const Path& bindFilePath, MaterialCookArena& arena, Core::Alloc::ScratchArena& scratchArena);
[[nodiscard]] Expected<MaterialBindTypedLayout> BuildMaterialBindTypedLayout(const MaterialBindEntry& bindEntry, const Name& contextName, MaterialCookArena& arena, Core::Alloc::ScratchArena& scratchArena);
[[nodiscard]] Expected<NotNull<const MaterialBindTypedLayout*>> FindOrBuildMaterialBindTypedLayout(const Name& materialInterface, const MaterialBindEntry& bindEntry, Core::Alloc::ScratchArena& scratchArena, MaterialBindTypedLayoutCache& inOutCache);
inline void CopyMaterialBindTypedLayoutDefaults(const MaterialBindTypedLayout& layout, u64& outLayoutHash, Material::TypedLayoutBlockVector& outBlocks, Material::TypedLayoutFieldVector& outFields, Material::TypedBlockByteVector& outBlockBytes){
    CopyMaterialBindTypedLayoutDefaults(layout.layoutHash, layout.typedLayoutBlocks, layout.typedLayoutFields, layout.typedBlockBytes, outLayoutHash, outBlocks, outFields, outBlockBytes);
}
[[nodiscard]] b8 ApplyMaterialBindTypedLayoutParameters(
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

