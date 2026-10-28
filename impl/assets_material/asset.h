// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"
#include "material_typed_constants.h"
#include <impl/assets_material/types.adhd.h>

#include <core/assets/module.h>
#include <core/assets/paths.h>
#include <core/assets/ref.h>
#include <core/graphics/api.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Sampler;
interface IShader;
class PixelShader;
class Texture;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Transparent materials need all three pass shaders; opaque materials carry none.
[[nodiscard]] inline bool HasValidMaterialAvboitPixelShaderContract(
    const bool transparent,
    const Core::Assets::AssetRef<PixelShader>& accumulatePixelShader,
    const Core::Assets::AssetRef<PixelShader>& occupancyPixelShader,
    const Core::Assets::AssetRef<PixelShader>& extinctionPixelShader
)noexcept{
    if(transparent){
        return accumulatePixelShader.valid() && occupancyPixelShader.valid() && extinctionPixelShader.valid();
    }

    return !accumulatePixelShader.valid() && !occupancyPixelShader.valid() && !extinctionPixelShader.valid();
}

// Materials hold only mesh and pixel stages. The compute emulation shader is found by the mesh shader name, so it is never stored here.
[[nodiscard]] inline constexpr bool IsSupportedMaterialShaderStage(const Core::ShaderType::Enum shaderType)noexcept{
    return shaderType == Core::ShaderType::MeshStage || shaderType == Core::ShaderType::PixelStage;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static_assert(
    s_MaterialLayoutFieldComponentsPerValueType == NWB_MATERIAL_TYPED_VALUE_COMPONENT_COUNT,
    "Material layout field component count drifted"
);
static_assert(
    static_cast<u32>(MaterialParameterValueType::Float) - s_MaterialParameterFirstValueTypeId
        == (static_cast<u32>(MaterialLayoutFieldType::Float4) - s_MaterialLayoutFieldFirstTypeId) / s_MaterialLayoutFieldComponentsPerValueType,
    "Material layout field/value type ordering must remain contiguous"
);

// Resource fields name an engine/project asset resolved to a heap slot at runtime.
[[nodiscard]] inline bool IsMaterialAssetReference(const AStringView resourceName)noexcept{
    const usize rootEnd = resourceName.find('/');
    if(rootEnd == AStringView::npos || rootEnd == 0u || rootEnd + 1u >= resourceName.size())
        return false;

    const AStringView virtualRoot = resourceName.substr(0u, rootEnd);
    if(virtualRoot != Core::Assets::s_EngineVirtualRoot && virtualRoot != Core::Assets::s_ProjectVirtualRoot)
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

[[nodiscard]] inline bool IsSupportedMaterialResourceReference(const MaterialResourceKind::Enum resourceKind, const AStringView resourceName)noexcept{
    return IsValidMaterialResourceKind(resourceKind) && IsMaterialAssetReference(resourceName);
}

// Serialized resource paths arrive as Name hashes; their original text is validated during cooking.
[[nodiscard]] inline bool IsValidSerializedMaterialResourceReference(const MaterialResourceKind::Enum resourceKind, const Name& resourceName)noexcept{
    return resourceName && IsValidMaterialResourceKind(resourceKind);
}

[[nodiscard]] inline Expected<u32> AlignMaterialLayoutFieldOffset(const u32 byteOffset, const MaterialLayoutFieldType::Enum fieldType)noexcept{
    const u32 alignment = MaterialLayoutFieldAlignment(fieldType);
    if(alignment == 0u)
        return MakeUnexpected(Failure{});

    return AlignUpU32Checked(byteOffset, alignment);
}

[[nodiscard]] inline Expected<u32> AlignMaterialLayoutBlockByteSize(const u32 byteSize)noexcept{
    return AlignUpU32Checked(byteSize, NWB_MATERIAL_TYPED_WORD_BYTES);
}

[[nodiscard]] inline bool AssignMaterialResourceReferenceAsset(MaterialResourceReference& reference, const MaterialResourceKind::Enum resourceKind, const Name& resourceName)noexcept{
    switch(resourceKind){
    case MaterialResourceKind::SampledImage2D:
        reference.textureAsset.virtualPath = resourceName;
        return true;
    case MaterialResourceKind::Sampler:
        reference.samplerAsset.virtualPath = resourceName;
        return true;
    default:
        NWB_ASSERT(false);
        return false;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Material final : public Core::Assets::TypedAsset<Material>{
public:
    using StageShaderArray = Array<Core::Assets::AssetRef<IShader>, static_cast<usize>(Core::ShaderType::Count)>;
    using TypedLayoutBlockVector = Core::Assets::AssetVector<MaterialTypedLayoutBlock>;
    using TypedLayoutFieldVector = Core::Assets::AssetVector<MaterialTypedLayoutField>;
    using TypedBlockByteVector = Core::Assets::AssetVector<u8>;
    using ResourceReferenceVector = Core::Assets::AssetVector<MaterialResourceReference>;


public:
    static constexpr auto s_ShaderStageCount = static_cast<usize>(Core::ShaderType::Count);


public:
    NWB_DEFINE_ASSET_TYPE("material")


public:
    explicit Material(Core::Assets::AssetArena& arena)noexcept
        : m_shaderVariant(arena)
        , m_typedLayoutBlocks(arena)
        , m_typedLayoutFields(arena)
        , m_typedBlockBytes(arena)
        , m_resourceReferences(arena)
    {}
    Material(Core::Assets::AssetArena& arena, const Name& virtualPath)noexcept
        : Core::Assets::TypedAsset<Material>(virtualPath)
        , m_shaderVariant(arena)
        , m_typedLayoutBlocks(arena)
        , m_typedLayoutFields(arena)
        , m_typedBlockBytes(arena)
        , m_resourceReferences(arena)
    {}


public:
    bool loadBinary(Span<const u8> binary);

public:
    void setShaderVariant(AStringView variantName){ m_shaderVariant.assign(variantName); }

    void setMaterialInterface(const Name& materialInterface)noexcept{ m_materialInterface = materialInterface; }

    void setShadingModelId(const u32 shadingModelId)noexcept{ m_shadingModelId = shadingModelId; }

    void setSurfaceDispatchId(const u32 surfaceDispatchId)noexcept{ m_surfaceDispatchId = surfaceDispatchId; }

    void setAvboitAccumulatePixelShader(const Core::Assets::AssetRef<PixelShader>& shaderAsset)noexcept{ m_avboitAccumulatePixelShader = shaderAsset; }

    void setAvboitOccupancyPixelShader(const Core::Assets::AssetRef<PixelShader>& shaderAsset)noexcept{ m_avboitOccupancyPixelShader = shaderAsset; }

    void setAvboitExtinctionPixelShader(const Core::Assets::AssetRef<PixelShader>& shaderAsset)noexcept{ m_avboitExtinctionPixelShader = shaderAsset; }

    void setTransparent(const bool transparent)noexcept{ m_transparent = transparent; }

    void setTwoSided(const bool twoSided)noexcept{ m_twoSided = twoSided; }

    void setRefractive(const bool refractive)noexcept{ m_refractive = refractive; }
    void setTypedLayout(u64 layoutHash, Span<const MaterialTypedLayoutBlock> blocks, Span<const MaterialTypedLayoutField> fields, Span<const u8> blockBytes);
    void setResourceReferences(Span<const MaterialResourceReference> resourceReferences);
    bool setShaderForStage(Core::ShaderType::Enum shaderType, const Core::Assets::AssetRef<IShader>& shaderAsset)noexcept;

    template<typename TShader>
        requires(IsBaseOf_V<IShader, TShader> && requires{ TShader::s_Stage; })
    [[nodiscard]] Expected<Core::Assets::AssetRef<TShader>> findShader()const noexcept{
        static_assert(IsSupportedMaterialShaderStage(TShader::s_Stage));
        Core::Assets::AssetRef<TShader> shaderAsset;
        shaderAsset.virtualPath = m_stageShaders[Core::ShaderType::ToIndex(TShader::s_Stage)].virtualPath;
        if(!shaderAsset.valid())
            return MakeUnexpected(Failure{});
        return shaderAsset;
    }

public:
    [[nodiscard]] const Core::Assets::AssetString& shaderVariant()const noexcept{ return m_shaderVariant; }

    [[nodiscard]] const Name& materialInterface()const noexcept{ return m_materialInterface; }

    [[nodiscard]] u32 shadingModelId()const noexcept{ return m_shadingModelId; }

    [[nodiscard]] u32 surfaceDispatchId()const noexcept{ return m_surfaceDispatchId; }

    [[nodiscard]] u64 typedLayoutHash()const noexcept{ return m_typedLayoutHash; }

    [[nodiscard]] const TypedLayoutBlockVector& typedLayoutBlocks()const noexcept{ return m_typedLayoutBlocks; }

    [[nodiscard]] const TypedLayoutFieldVector& typedLayoutFields()const noexcept{ return m_typedLayoutFields; }

    [[nodiscard]] const TypedBlockByteVector& typedBlockBytes()const noexcept{ return m_typedBlockBytes; }

    [[nodiscard]] const ResourceReferenceVector& resourceReferences()const noexcept{ return m_resourceReferences; }

    [[nodiscard]] const StageShaderArray& stageShaders()const noexcept{ return m_stageShaders; }

    [[nodiscard]] u32 stageShaderCount()const noexcept{ return m_stageShaderCount; }
    // Cook-generated AVBOIT accumulate pixel shader for this material's transparent draw.
    // Valid only for surface-authored transparent materials; missing means a cook/runtime contract failure.
    // Not a graphics stage: the material has one pixel stage, and this is the transparent-only shader the renderer selects by pass.
    [[nodiscard]] const Core::Assets::AssetRef<PixelShader>& avboitAccumulatePixelShader()const noexcept{ return m_avboitAccumulatePixelShader; }
    // Accumulate twins for AVBOIT passes; same surface renderCoverage and validity as above.
    [[nodiscard]] const Core::Assets::AssetRef<PixelShader>& avboitOccupancyPixelShader()const noexcept{ return m_avboitOccupancyPixelShader; }

    [[nodiscard]] const Core::Assets::AssetRef<PixelShader>& avboitExtinctionPixelShader()const noexcept{ return m_avboitExtinctionPixelShader; }

    [[nodiscard]] bool transparent()const noexcept{ return m_transparent; }

    [[nodiscard]] bool twoSided()const noexcept{ return m_twoSided; }
    // Refractive-caster flag, separate from `transparent`. Refraction values stay shader-side (NwbMeshSurface).
    [[nodiscard]] bool refractive()const noexcept{ return m_refractive; }


private:
    Core::Assets::AssetString m_shaderVariant;
    Name m_materialInterface = s_NameNone;
    u32 m_shadingModelId = 0u;
    u32 m_surfaceDispatchId = 0u;
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
    Core::Assets::AssetRef<PixelShader> m_avboitAccumulatePixelShader;
    Core::Assets::AssetRef<PixelShader> m_avboitOccupancyPixelShader;
    Core::Assets::AssetRef<PixelShader> m_avboitExtinctionPixelShader;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_ASSET_CODEC(MaterialAssetCodec, Material);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

