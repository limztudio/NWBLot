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


inline constexpr u32 s_MaterialMagic = 0x4D544C38u; // MTL8 (per-material project resource paths)
inline constexpr usize s_ShaderEntryBytes = sizeof(Core::ShaderType::Enum) + sizeof(NameHash);
// Material render-property flags packed into the serialized materialFlags word (decoded in Material::loadBinary),
// mirroring the authored `transparent`/`two_sided`/`refractive` booleans. `Refractive` is the dedicated
// refractive-caster classification (SEPARATE from `Transparent`). The refraction VALUES (refractionIor /
// shadowAbsorptionTint) are shader-side (NwbMeshSurface), not in this payload. `All` is the mask of supported bits;
// loadBinary rejects any bit outside it.
namespace MaterialFlag{
    enum Mask : u32{
        None = 0u,
        Transparent = 1u << 0u,
        TwoSided = 1u << 1u,
        Refractive = 1u << 2u,
        All = Transparent | TwoSided | Refractive,
    };
};

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

// Per-material resource identity. The renderer resolves resourceName according to resourceSource to a
// device-lifetime global-heap handle and writes that handle's slot into constantByteOffset; no device-specific
// descriptor value is serialized here.
struct MaterialResourceReferenceBinary{
    NameHash blockNameHash = {};
    NameHash fieldNameHash = {};
    NameHash resourceNameHash = {};
    u32 resourceKind = MaterialResourceKind::None;
    u32 resourceSource = MaterialResourceSource::None;
    u32 constantByteOffset = 0u;
    u32 reserved = 0u;
};
static_assert(
    sizeof(MaterialResourceReferenceBinary) == sizeof(NameHash) * 3u + sizeof(u32) * 4u,
    "MaterialResourceReferenceBinary layout drifted"
);
static_assert(
    IsStandardLayout_V<MaterialResourceReferenceBinary>,
    "MaterialResourceReferenceBinary must stay binary-serializable"
);
static_assert(
    IsTriviallyCopyable_V<MaterialResourceReferenceBinary>,
    "MaterialResourceReferenceBinary must stay binary-serializable"
);

inline constexpr usize s_TypedLayoutBlockBytes = sizeof(MaterialTypedLayoutBlockBinary);
inline constexpr usize s_TypedLayoutFieldBytes = sizeof(MaterialTypedLayoutFieldBinary);
inline constexpr usize s_ResourceReferenceBytes = sizeof(MaterialResourceReferenceBinary);

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

template<typename BlockVector, typename FieldVector, typename ResourceReferenceVector>
[[nodiscard]] inline bool ValidateMaterialResourceReferences(
    const BlockVector& blocks,
    const FieldVector& fields,
    const ResourceReferenceVector& resourceReferences
){
    usize resourceFieldCount = 0u;
    u32 constantByteBegin = 0u;
    for(const MaterialTypedLayoutBlock& block : blocks){
        if(!IsValidMaterialBlockClass(block.blockClass))
            return false;
        if(block.fieldBegin > fields.size() || block.fieldCount > fields.size() - block.fieldBegin)
            return false;

        for(u32 fieldOffset = 0u; fieldOffset < block.fieldCount; ++fieldOffset){
            const MaterialTypedLayoutField& field = fields[static_cast<usize>(block.fieldBegin) + fieldOffset];
            if(!IsMaterialLayoutResourceFieldType(field.fieldType))
                continue;

            // Opaque handles are intentionally static constants. A resource in mutable storage would make the
            // cooked resource contract ambiguous and permit instance data to become a descriptor slot.
            if(block.blockClass != MaterialBlockClass::MaterialConstant)
                return false;
            if(field.offset > Limit<u32>::s_Max - constantByteBegin)
                return false;

            ++resourceFieldCount;
            const MaterialResourceKind::Enum expectedKind = MaterialLayoutFieldResourceKind(field.fieldType);
            const u32 expectedByteOffset = constantByteBegin + field.offset;
            bool foundReference = false;
            for(const MaterialResourceReference& resourceReference : resourceReferences){
                if(resourceReference.blockName != block.blockName || resourceReference.fieldName != field.fieldName)
                    continue;
                if(foundReference)
                    return false;
                if(
                    !resourceReference.resourceName
                    || resourceReference.resourceKind != expectedKind
                    || !IsValidSerializedMaterialResourceReference(
                        resourceReference.resourceKind,
                        resourceReference.resourceSource,
                        resourceReference.resourceName
                    )
                    || resourceReference.constantByteOffset != expectedByteOffset
                )
                    return false;

                foundReference = true;
            }
            if(!foundReference)
                return false;
        }

        if(block.blockClass == MaterialBlockClass::MaterialConstant){
            if(block.byteSize > Limit<u32>::s_Max - constantByteBegin)
                return false;
            constantByteBegin += block.byteSize;
        }
    }

    // Every cooked record must correspond to exactly one resource field. Since duplicate matching records above
    // are rejected, this also excludes stray references to numeric or mutable fields.
    return resourceFieldCount == resourceReferences.size();
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

