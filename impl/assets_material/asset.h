// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"

#include <core/assets/module.h>
#include <core/assets/paths.h>
#include <core/assets/ref.h>
#include <core/graphics/api.h>
#include <impl/assets/graphics/mesh/material_typed_constants.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Sampler;
class Shader;
class Texture;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// AVBOIT uses a per-material pixel shader for each transparent pass. These references are a single atomic contract:
// a transparent material needs all three pass shaders, while an opaque material must not carry any of them.
[[nodiscard]] inline bool HasValidMaterialAvboitPixelShaderContract(
    const bool transparent,
    const Core::Assets::AssetRef<Shader>& accumulatePixelShader,
    const Core::Assets::AssetRef<Shader>& occupancyPixelShader,
    const Core::Assets::AssetRef<Shader>& extinctionPixelShader
){
    if(transparent){
        return
            accumulatePixelShader.valid()
            && occupancyPixelShader.valid()
            && extinctionPixelShader.valid()
        ;
    }

    return
        !accumulatePixelShader.valid()
        && !occupancyPixelShader.valid()
        && !extinctionPixelShader.valid()
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace MaterialParameterValueType{
    enum Enum : u32{
        None = 0,
        Bool = 1,
        Char = 2,
        UChar = 3,
        Short = 4,
        UShort = 5,
        Int = 6,
        UInt = 7,
        Half = 8,
        Float = 9,
    };
};

namespace MaterialBlockClass{
    enum Enum : u32{
        None = 0,
        MaterialConstant = 1,
        MaterialMutable = 2,
    };
};

[[nodiscard]] inline bool IsValidMaterialBlockClass(const MaterialBlockClass::Enum blockClass){
    return blockClass == MaterialBlockClass::MaterialConstant || blockClass == MaterialBlockClass::MaterialMutable;
}


namespace MaterialResourceKind{
    enum Enum : u32{
        None = 0,
        SampledImage2D = 1,
        Sampler = 2,
    };
};

[[nodiscard]] inline bool IsValidMaterialResourceKind(const MaterialResourceKind::Enum resourceKind){
    return resourceKind == MaterialResourceKind::SampledImage2D || resourceKind == MaterialResourceKind::Sampler;
}


// Resource fields always name an engine or project asset. The field type determines the expected asset family; the
// renderer resolves that path to a global bindless slot at runtime.
namespace MaterialResourceSource{
    enum Enum : u32{
        None = 0u,
        Asset = 1u,
        // Keep the previous spelling source-compatible. Its serialized ordinal remains the generic asset source.
        ProjectAsset = Asset,
    };
};

[[nodiscard]] inline bool IsValidMaterialResourceSource(const MaterialResourceSource::Enum resourceSource){
    return resourceSource == MaterialResourceSource::Asset;
}

[[nodiscard]] inline bool IsMaterialAssetReference(const AStringView resourceName){
    const usize rootEnd = resourceName.find('/');
    if(
        rootEnd == AStringView::npos
        || rootEnd == 0u
        || rootEnd + 1u >= resourceName.size()
    )
        return false;

    const AStringView virtualRoot = resourceName.substr(0u, rootEnd);
    if(
        virtualRoot != Core::Assets::s_EngineVirtualRoot
        && virtualRoot != Core::Assets::s_ProjectVirtualRoot
    )
        return false;

    usize componentBegin = rootEnd + 1u;
    for(usize index = componentBegin; index <= resourceName.size(); ++index){
        if(index != resourceName.size() && resourceName[index] != '/')
            continue;

        const AStringView component = resourceName.substr(componentBegin, index - componentBegin);
        if(component.empty() || component == "." || component == ".." || component.find('\\') != AStringView::npos)
            return false;
        componentBegin = index + 1u;
    }
    return true;
}

[[nodiscard]] inline bool IsSupportedMaterialResourceReference(
    const MaterialResourceKind::Enum resourceKind,
    const MaterialResourceSource::Enum resourceSource,
    const AStringView resourceName
){
    return IsValidMaterialResourceKind(resourceKind)
        && resourceSource == MaterialResourceSource::Asset
        && IsMaterialAssetReference(resourceName)
    ;
}

// Serialized resource paths arrive as Name hashes, so their original text cannot be revalidated here. The explicit
// asset source and the resource kind keep the renderer's asset-family dispatch unambiguous.
[[nodiscard]] inline bool IsValidSerializedMaterialResourceReference(
    const MaterialResourceKind::Enum resourceKind,
    const MaterialResourceSource::Enum resourceSource,
    const Name& resourceName
){
    if(!resourceName)
        return false;

    return IsValidMaterialResourceKind(resourceKind)
        && resourceSource == MaterialResourceSource::Asset
    ;
}


namespace MaterialLayoutFieldType{
    enum Enum : u32{
        None = 0,
        Bool = 1,
        Bool2 = 2,
        Bool3 = 3,
        Bool4 = 4,
        Char = 5,
        Char2 = 6,
        Char3 = 7,
        Char4 = 8,
        UChar = 9,
        UChar2 = 10,
        UChar3 = 11,
        UChar4 = 12,
        Short = 13,
        Short2 = 14,
        Short3 = 15,
        Short4 = 16,
        UShort = 17,
        UShort2 = 18,
        UShort3 = 19,
        UShort4 = 20,
        Int = 21,
        Int2 = 22,
        Int3 = 23,
        Int4 = 24,
        UInt = 25,
        UInt2 = 26,
        UInt3 = 27,
        UInt4 = 28,
        Half = 29,
        Half2 = 30,
        Half3 = 31,
        Half4 = 32,
        Float = 33,
        Float2 = 34,
        Float3 = 35,
        Float4 = 36,
        // Resource fields occupy one patched uint heap slot in the typed-byte payload. They intentionally live
        // after the contiguous numeric range so a resource can never be mistaken for an authored uint parameter.
        SampledImage2D = 37,
        Sampler = 38,
    };
};

inline constexpr u32 s_MaterialLayoutFieldComponentsPerValueType = NWB_MATERIAL_TYPED_VALUE_COMPONENT_COUNT;
inline constexpr u32 s_MaterialLayoutFieldFirstTypeId = static_cast<u32>(MaterialLayoutFieldType::Bool);
inline constexpr u32 s_MaterialParameterFirstValueTypeId = static_cast<u32>(MaterialParameterValueType::Bool);
static_assert(
    static_cast<u32>(MaterialParameterValueType::Float) - s_MaterialParameterFirstValueTypeId
        == (static_cast<u32>(MaterialLayoutFieldType::Float4) - s_MaterialLayoutFieldFirstTypeId) / s_MaterialLayoutFieldComponentsPerValueType,
    "Material layout field/value type ordering must remain contiguous"
);

[[nodiscard]] inline bool IsValidMaterialLayoutFieldType(const MaterialLayoutFieldType::Enum fieldType){
    return fieldType >= MaterialLayoutFieldType::Bool && fieldType <= MaterialLayoutFieldType::Sampler;
}

[[nodiscard]] inline bool IsMaterialLayoutNumericFieldType(const MaterialLayoutFieldType::Enum fieldType){
    return fieldType >= MaterialLayoutFieldType::Bool && fieldType <= MaterialLayoutFieldType::Float4;
}

[[nodiscard]] inline bool IsMaterialLayoutResourceFieldType(const MaterialLayoutFieldType::Enum fieldType){
    return fieldType == MaterialLayoutFieldType::SampledImage2D || fieldType == MaterialLayoutFieldType::Sampler;
}

[[nodiscard]] inline MaterialResourceKind::Enum MaterialLayoutFieldResourceKind(const MaterialLayoutFieldType::Enum fieldType){
    switch(fieldType){
    case MaterialLayoutFieldType::SampledImage2D: return MaterialResourceKind::SampledImage2D;
    case MaterialLayoutFieldType::Sampler: return MaterialResourceKind::Sampler;
    default: return MaterialResourceKind::None;
    }
}

[[nodiscard]] inline MaterialLayoutFieldType::Enum MaterialLayoutFieldTypeFromResourceKind(
    const MaterialResourceKind::Enum resourceKind
){
    switch(resourceKind){
    case MaterialResourceKind::SampledImage2D: return MaterialLayoutFieldType::SampledImage2D;
    case MaterialResourceKind::Sampler: return MaterialLayoutFieldType::Sampler;
    default: return MaterialLayoutFieldType::None;
    }
}

[[nodiscard]] inline u32 MaterialLayoutFieldComponentCount(const MaterialLayoutFieldType::Enum fieldType){
    if(!IsMaterialLayoutNumericFieldType(fieldType))
        return 0u;

    return ((static_cast<u32>(fieldType) - s_MaterialLayoutFieldFirstTypeId) % s_MaterialLayoutFieldComponentsPerValueType) + 1u;
}

[[nodiscard]] inline MaterialParameterValueType::Enum MaterialLayoutFieldValueType(
    const MaterialLayoutFieldType::Enum fieldType
){
    if(!IsMaterialLayoutNumericFieldType(fieldType))
        return MaterialParameterValueType::None;

    const u32 valueTypeOffset = (static_cast<u32>(fieldType) - s_MaterialLayoutFieldFirstTypeId) / s_MaterialLayoutFieldComponentsPerValueType;
    return static_cast<MaterialParameterValueType::Enum>(s_MaterialParameterFirstValueTypeId + valueTypeOffset);
}

[[nodiscard]] inline MaterialLayoutFieldType::Enum MaterialLayoutFieldTypeFromParameterType(
    const MaterialParameterValueType::Enum valueType,
    const u32 componentCount
){
    if(componentCount == 0u || componentCount > s_MaterialLayoutFieldComponentsPerValueType)
        return MaterialLayoutFieldType::None;

    u32 firstFieldType = 0u;
    switch(valueType){
    case MaterialParameterValueType::Bool: firstFieldType = static_cast<u32>(MaterialLayoutFieldType::Bool); break;
    case MaterialParameterValueType::Char: firstFieldType = static_cast<u32>(MaterialLayoutFieldType::Char); break;
    case MaterialParameterValueType::UChar: firstFieldType = static_cast<u32>(MaterialLayoutFieldType::UChar); break;
    case MaterialParameterValueType::Short: firstFieldType = static_cast<u32>(MaterialLayoutFieldType::Short); break;
    case MaterialParameterValueType::UShort: firstFieldType = static_cast<u32>(MaterialLayoutFieldType::UShort); break;
    case MaterialParameterValueType::Int: firstFieldType = static_cast<u32>(MaterialLayoutFieldType::Int); break;
    case MaterialParameterValueType::UInt: firstFieldType = static_cast<u32>(MaterialLayoutFieldType::UInt); break;
    case MaterialParameterValueType::Half: firstFieldType = static_cast<u32>(MaterialLayoutFieldType::Half); break;
    case MaterialParameterValueType::Float: firstFieldType = static_cast<u32>(MaterialLayoutFieldType::Float); break;
    default: return MaterialLayoutFieldType::None;
    }

    return static_cast<MaterialLayoutFieldType::Enum>(firstFieldType + componentCount - 1u);
}

[[nodiscard]] inline u32 MaterialParameterValueTypeByteSize(const MaterialParameterValueType::Enum valueType){
    switch(valueType){
    case MaterialParameterValueType::Bool:
    case MaterialParameterValueType::Char:
    case MaterialParameterValueType::UChar:
        return sizeof(u8);
    case MaterialParameterValueType::Short:
    case MaterialParameterValueType::UShort:
    case MaterialParameterValueType::Half:
        return sizeof(u16);
    case MaterialParameterValueType::Int:
    case MaterialParameterValueType::UInt:
    case MaterialParameterValueType::Float:
        return sizeof(u32);
    default:
        return 0u;
    }
}

[[nodiscard]] inline u32 MaterialLayoutFieldByteSize(const MaterialLayoutFieldType::Enum fieldType){
    if(IsMaterialLayoutResourceFieldType(fieldType))
        return sizeof(u32);

    return
        MaterialLayoutFieldComponentCount(fieldType)
        * MaterialParameterValueTypeByteSize(MaterialLayoutFieldValueType(fieldType))
    ;
}

[[nodiscard]] inline u32 MaterialLayoutFieldAlignment(const MaterialLayoutFieldType::Enum fieldType){
    if(IsMaterialLayoutResourceFieldType(fieldType))
        return sizeof(u32);

    return MaterialParameterValueTypeByteSize(MaterialLayoutFieldValueType(fieldType));
}

[[nodiscard]] inline bool AlignMaterialLayoutFieldOffset(
    const u32 byteOffset,
    const MaterialLayoutFieldType::Enum fieldType,
    u32& outByteOffset
){
    const u32 alignment = MaterialLayoutFieldAlignment(fieldType);
    if(alignment == 0u)
        return false;

    return AlignUpU32Checked(byteOffset, alignment, outByteOffset);
}

[[nodiscard]] inline bool AlignMaterialLayoutBlockByteSize(const u32 byteSize, u32& outByteSize){
    return AlignUpU32Checked(byteSize, NWB_MATERIAL_TYPED_WORD_BYTES, outByteSize);
}

struct MaterialTypedLayoutBlock{
    Name blockName = NAME_NONE;
    MaterialBlockClass::Enum blockClass = MaterialBlockClass::None;
    u32 fieldBegin = 0u;
    u32 fieldCount = 0u;
    u32 byteSize = 0u;
};

struct MaterialTypedLayoutField{
    Name fieldName = NAME_NONE;
    MaterialLayoutFieldType::Enum fieldType = MaterialLayoutFieldType::None;
    u32 offset = 0u;
    UInt4U defaultValue = {};
};

// A cooked material keeps resource identity separate from its numeric/default typed payload. `constantByteOffset`
// points at the four-byte slot word the renderer patches after it has resolved the device-lifetime descriptor handle.
// Exactly one typed asset reference is valid, selected by resourceKind.
struct MaterialResourceReference{
    Name blockName = NAME_NONE;
    Name fieldName = NAME_NONE;
    Core::Assets::AssetRef<Texture> textureAsset;
    Core::Assets::AssetRef<Sampler> samplerAsset;
    MaterialResourceKind::Enum resourceKind = MaterialResourceKind::None;
    MaterialResourceSource::Enum resourceSource = MaterialResourceSource::None;
    u32 constantByteOffset = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Material final : public Core::Assets::TypedAsset<Material>{
public:
    NWB_DEFINE_ASSET_TYPE("material")


public:
    static constexpr auto s_ShaderStageCount = static_cast<usize>(Core::ShaderType::Count);

    using StageShaderArray = Array<Core::Assets::AssetRef<Shader>, s_ShaderStageCount>;
    using TypedLayoutBlockVector = Core::Assets::AssetVector<MaterialTypedLayoutBlock>;
    using TypedLayoutFieldVector = Core::Assets::AssetVector<MaterialTypedLayoutField>;
    using TypedBlockByteVector = Core::Assets::AssetVector<u8>;
    using ResourceReferenceVector = Core::Assets::AssetVector<MaterialResourceReference>;


public:
    explicit Material(Core::Assets::AssetArena& arena)
        : m_shaderVariant(arena)
        , m_typedLayoutBlocks(arena)
        , m_typedLayoutFields(arena)
        , m_typedBlockBytes(arena)
        , m_resourceReferences(arena)
    {}
    Material(Core::Assets::AssetArena& arena, const Name& virtualPath)
        : Core::Assets::TypedAsset<Material>(virtualPath)
        , m_shaderVariant(arena)
        , m_typedLayoutBlocks(arena)
        , m_typedLayoutFields(arena)
        , m_typedBlockBytes(arena)
        , m_resourceReferences(arena)
    {}


public:
    bool loadBinary(const Core::Assets::AssetBytes& binary);

public:
    void setShaderVariant(AStringView variantName){ m_shaderVariant.assign(variantName); }
    void setMaterialInterface(const Name& materialInterface){ m_materialInterface = materialInterface; }
    void setShadingModelId(const u32 shadingModelId){ m_shadingModelId = shadingModelId; }
    void setShadowTransmittanceModelId(const u32 shadowTransmittanceModelId){ m_shadowTransmittanceModelId = shadowTransmittanceModelId; }
    void setAvboitAccumulatePixelShader(const Core::Assets::AssetRef<Shader>& shaderAsset){ m_avboitAccumulatePixelShader = shaderAsset; }
    void setAvboitOccupancyPixelShader(const Core::Assets::AssetRef<Shader>& shaderAsset){ m_avboitOccupancyPixelShader = shaderAsset; }
    void setAvboitExtinctionPixelShader(const Core::Assets::AssetRef<Shader>& shaderAsset){ m_avboitExtinctionPixelShader = shaderAsset; }
    void setTransparent(const bool transparent){ m_transparent = transparent; }
    void setTwoSided(const bool twoSided){ m_twoSided = twoSided; }
    void setRefractive(const bool refractive){ m_refractive = refractive; }
    void setTypedLayout(
        u64 layoutHash,
        const TypedLayoutBlockVector& blocks,
        const TypedLayoutFieldVector& fields,
        const TypedBlockByteVector& blockBytes
    );
    void setResourceReferences(const ResourceReferenceVector& resourceReferences);
    bool setShaderForStage(Core::ShaderType::Enum shaderType, const Core::Assets::AssetRef<Shader>& shaderAsset);

    bool findShaderForStage(Core::ShaderType::Enum shaderType, Core::Assets::AssetRef<Shader>& outShaderAsset)const;

public:
    [[nodiscard]] const Core::Assets::AssetString& shaderVariant()const{ return m_shaderVariant; }
    [[nodiscard]] const Name& materialInterface()const{ return m_materialInterface; }
    [[nodiscard]] u32 shadingModelId()const{ return m_shadingModelId; }
    [[nodiscard]] u32 shadowTransmittanceModelId()const{ return m_shadowTransmittanceModelId; }
    [[nodiscard]] u64 typedLayoutHash()const{ return m_typedLayoutHash; }
    [[nodiscard]] const TypedLayoutBlockVector& typedLayoutBlocks()const{ return m_typedLayoutBlocks; }
    [[nodiscard]] const TypedLayoutFieldVector& typedLayoutFields()const{ return m_typedLayoutFields; }
    [[nodiscard]] const TypedBlockByteVector& typedBlockBytes()const{ return m_typedBlockBytes; }
    [[nodiscard]] const ResourceReferenceVector& resourceReferences()const{ return m_resourceReferences; }
    [[nodiscard]] const StageShaderArray& stageShaders()const{ return m_stageShaders; }
    [[nodiscard]] u32 stageShaderCount()const{ return m_stageShaderCount; }
    // The cook-generated per-material AVBOIT accumulate pixel shader bound for this material's transparent draw
    // (the transparent-pass twin of the G-buffer pixel shader). Valid for a transparent material authored with a
    // `surface`; invalid for an opaque material. A missing shader on a transparent material is a cook/runtime
    // contract failure. Unlike the stage shaders this is not a graphics stage -- a material has a single pixel
    // stage (the G-buffer PS); this is a second, transparent-only pixel shader the renderer selects by pass.
    [[nodiscard]] const Core::Assets::AssetRef<Shader>& avboitAccumulatePixelShader()const{ return m_avboitAccumulatePixelShader; }
    // The occupancy/extinction twins of avboitAccumulatePixelShader, bound for this material's transparent draw so
    // all three AVBOIT passes read the material's SAME shader-decided surface.renderCoverage. They are valid for a
    // surface-authored transparent material and invalid for an opaque material. A missing shader on a transparent
    // material is a cook/runtime contract failure.
    [[nodiscard]] const Core::Assets::AssetRef<Shader>& avboitOccupancyPixelShader()const{ return m_avboitOccupancyPixelShader; }
    [[nodiscard]] const Core::Assets::AssetRef<Shader>& avboitExtinctionPixelShader()const{ return m_avboitExtinctionPixelShader; }
    [[nodiscard]] bool transparent()const{ return m_transparent; }
    [[nodiscard]] bool twoSided()const{ return m_twoSided; }
    // The dedicated refractive-caster classification flag (SEPARATE from `transparent`). The material decides only
    // this boolean; the refraction VALUES (refractionIor / shadowAbsorptionTint) are shader-side, returned by the `.surface`
    // hook via NwbMeshSurface. Authored metadata must provide the value explicitly.
    [[nodiscard]] bool refractive()const{ return m_refractive; }


private:
    void clearStageShaders();


private:
    Core::Assets::AssetString m_shaderVariant;
    Name m_materialInterface = NAME_NONE;
    u32 m_shadingModelId = 0u;
    u32 m_shadowTransmittanceModelId = 0u;
    u64 m_typedLayoutHash = 0u;
    TypedLayoutBlockVector m_typedLayoutBlocks;
    TypedLayoutFieldVector m_typedLayoutFields;
    TypedBlockByteVector m_typedBlockBytes;
    ResourceReferenceVector m_resourceReferences;
    StageShaderArray m_stageShaders;
    u32 m_stageShaderCount = 0;
    bool m_transparent = false;
    bool m_twoSided = false;
    bool m_refractive = false;
    Core::Assets::AssetRef<Shader> m_avboitAccumulatePixelShader;
    Core::Assets::AssetRef<Shader> m_avboitOccupancyPixelShader;
    Core::Assets::AssetRef<Shader> m_avboitExtinctionPixelShader;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class MaterialAssetCodec final : public Core::Assets::AssetCodec<Material>{
public:
    MaterialAssetCodec() = default;


#if defined(NWB_COOK)
public:
    virtual bool serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const override;
#endif
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

