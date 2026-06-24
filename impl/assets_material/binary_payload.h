// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "asset.h"

#include <global/hash_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace MaterialBinaryPayload{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_MaterialMagic = 0x4D544C34u; // MTL4
inline constexpr usize s_ShaderEntryBytes = sizeof(Core::ShaderType::Enum) + sizeof(NameHash);
// Material render-property flags packed into the serialized materialFlags word (decoded in Material::loadBinary),
// mirroring the authored `transparent`/`two_sided`/`refractive` booleans. `Refractive` is the dedicated
// refractive-caster classification (SEPARATE from `Transparent`); it rides the existing flags word -- no POD
// layout change, so the MTL4 format stays backward-compatible (older data has bit2 = 0 = not refractive). The
// refraction VALUES (ior/thickness/transmission) are shader-side (NwbMeshSurface), not in this payload. `All` is
// the mask of supported bits; loadBinary rejects any bit outside it.
namespace MaterialFlag{
    enum Mask : u32{
        None = 0u,
        Transparent = 1u << 0u,
        TwoSided = 1u << 1u,
        Refractive = 1u << 2u,
        All = Transparent | TwoSided | Refractive,
    };
}

static_assert(sizeof(Core::ShaderType::Enum) == sizeof(u8), "Material shader stage indices must stay byte-sized");

struct MaterialTypedLayoutBlockBinary{
    NameHash blockNameHash = {};
    u32 blockClass = MaterialBlockClass::None;
    u32 fieldBegin = 0u;
    u32 fieldCount = 0u;
    u32 byteSize = 0u;
};
static_assert(
    sizeof(MaterialTypedLayoutBlockBinary) == sizeof(NameHash) + sizeof(u32) * 4u,
    "MaterialTypedLayoutBlockBinary layout drifted"
);
static_assert(
    IsStandardLayout_V<MaterialTypedLayoutBlockBinary>,
    "MaterialTypedLayoutBlockBinary must stay binary-serializable"
);
static_assert(
    IsTriviallyCopyable_V<MaterialTypedLayoutBlockBinary>,
    "MaterialTypedLayoutBlockBinary must stay binary-serializable"
);

struct MaterialTypedLayoutFieldBinary{
    NameHash fieldNameHash = {};
    u32 fieldType = MaterialLayoutFieldType::None;
    u32 offset = 0u;
    UInt4U defaultValue = {};
};
static_assert(
    sizeof(MaterialTypedLayoutFieldBinary) == sizeof(NameHash) + sizeof(u32) * 6u,
    "MaterialTypedLayoutFieldBinary layout drifted"
);
static_assert(
    IsStandardLayout_V<MaterialTypedLayoutFieldBinary>,
    "MaterialTypedLayoutFieldBinary must stay binary-serializable"
);
static_assert(
    IsTriviallyCopyable_V<MaterialTypedLayoutFieldBinary>,
    "MaterialTypedLayoutFieldBinary must stay binary-serializable"
);

inline constexpr usize s_TypedLayoutBlockBytes = sizeof(MaterialTypedLayoutBlockBinary);
inline constexpr usize s_TypedLayoutFieldBytes = sizeof(MaterialTypedLayoutFieldBinary);

template<typename BlockVector>
[[nodiscard]] inline bool ComputeMaterialTypedBlockByteSize(const BlockVector& blocks, usize& outByteSize){
    outByteSize = 0u;
    for(const MaterialTypedLayoutBlock& block : blocks){
        if(static_cast<usize>(block.byteSize) > Limit<usize>::s_Max - outByteSize)
            return false;

        outByteSize += block.byteSize;
    }
    return true;
}

[[nodiscard]] inline u64 UpdateMaterialTypedLayoutHashName(u64 hash, const Name& name){
    return UpdateFnv64(hash, reinterpret_cast<const u8*>(&name.hash()), sizeof(NameHash));
}

template<typename ValueType>
[[nodiscard]] inline u64 UpdateMaterialTypedLayoutHashValue(u64 hash, const ValueType& value){
    return UpdateFnv64(hash, reinterpret_cast<const u8*>(&value), sizeof(value));
}

template<typename BlockVector, typename FieldVector>
[[nodiscard]] inline u64 ComputeMaterialTypedLayoutHash(const BlockVector& blocks, const FieldVector& fields){
    if(blocks.empty() && fields.empty())
        return 0u;

    u64 hash = UpdateFnv64TextExact(FNV64_OFFSET_BASIS, AStringView("NWB_MATERIAL_TYPED_LAYOUT_V1"));
    const u32 blockCount = static_cast<u32>(blocks.size());
    const u32 fieldCount = static_cast<u32>(fields.size());
    hash = UpdateMaterialTypedLayoutHashValue(hash, blockCount);
    hash = UpdateMaterialTypedLayoutHashValue(hash, fieldCount);
    for(const MaterialTypedLayoutBlock& block : blocks){
        hash = UpdateMaterialTypedLayoutHashName(hash, block.blockName);
        hash = UpdateMaterialTypedLayoutHashValue(hash, static_cast<u32>(block.blockClass));
        hash = UpdateMaterialTypedLayoutHashValue(hash, block.fieldBegin);
        hash = UpdateMaterialTypedLayoutHashValue(hash, block.fieldCount);
        hash = UpdateMaterialTypedLayoutHashValue(hash, block.byteSize);
    }
    for(const MaterialTypedLayoutField& field : fields){
        hash = UpdateMaterialTypedLayoutHashName(hash, field.fieldName);
        hash = UpdateMaterialTypedLayoutHashValue(hash, static_cast<u32>(field.fieldType));
        hash = UpdateMaterialTypedLayoutHashValue(hash, field.offset);
        hash = UpdateMaterialTypedLayoutHashValue(hash, field.defaultValue);
    }
    return hash;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

