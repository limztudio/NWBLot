// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"
#include <impl/assets_material/types.adhd.h>

#include <core/assets/module.h>
#include <core/assets/paths.h>
#include <core/assets/ref.h>
#include <core/graphics/api.h>

#include <global/hash_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Sampler;
interface IShader;
class PixelShader;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static_assert(sizeof(Core::ShaderType::Enum) == sizeof(u8), "Material shader stage indices must stay byte-sized");
static_assert(s_MaterialShaderEntryBytes == sizeof(Core::ShaderType::Enum) + sizeof(NameHash), "Material shader entry byte size drifted");
static_assert(sizeof(MaterialTypedLayoutBlockBinary) == s_MaterialTypedLayoutBlockBytes, "MaterialTypedLayoutBlockBinary layout drifted");
static_assert(sizeof(MaterialTypedLayoutFieldBinary) == s_MaterialTypedLayoutFieldBytes, "MaterialTypedLayoutFieldBinary layout drifted");
static_assert(sizeof(MaterialResourceReferenceBinary) == s_MaterialResourceReferenceBytes, "MaterialResourceReferenceBinary layout drifted");

// Materials hold only mesh and pixel stages. The compute emulation shader is found by the mesh shader name, so it is never stored here.
[[nodiscard]] inline constexpr b8 IsSupportedMaterialShaderStage(const Core::ShaderType::Enum shaderType)noexcept{
    return shaderType == Core::ShaderType::MeshStage || shaderType == Core::ShaderType::PixelStage;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static_assert(s_MaterialLayoutFieldComponentsPerValueType == s_MaterialTypedValueComponentCount, "Material layout field component count drifted");
static_assert(
    static_cast<u32>(MaterialParameterValueType::f32) - s_MaterialParameterFirstValueTypeId
        == (static_cast<u32>(MaterialLayoutFieldType::f32x4) - s_MaterialLayoutFieldFirstTypeId) / s_MaterialLayoutFieldComponentsPerValueType,
    "Material layout field/value type ordering must remain contiguous"
);


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
    b8 loadBinary(Span<const u8> binary);


public:
    void setShaderVariant(AStringView variantName){ m_shaderVariant.assign(variantName); }

    void setMaterialInterface(const Name& materialInterface)noexcept{ m_materialInterface = materialInterface; }

    void setShadingModelId(const u32 shadingModelId)noexcept{ m_shadingModelId = shadingModelId; }

    void setSurfaceDispatchId(const u32 surfaceDispatchId)noexcept{ m_surfaceDispatchId = surfaceDispatchId; }

    void setAvboitAccumulatePixelShader(const Core::Assets::AssetRef<PixelShader>& shaderAsset)noexcept{ m_avboitAccumulatePixelShader = shaderAsset; }

    void setAvboitOccupancyPixelShader(const Core::Assets::AssetRef<PixelShader>& shaderAsset)noexcept{ m_avboitOccupancyPixelShader = shaderAsset; }

    void setAvboitExtinctionPixelShader(const Core::Assets::AssetRef<PixelShader>& shaderAsset)noexcept{ m_avboitExtinctionPixelShader = shaderAsset; }

    void setTransparent(const b8 transparent)noexcept{ m_transparent = transparent; }

    void setTwoSided(const b8 twoSided)noexcept{ m_twoSided = twoSided; }

    void setRefractive(const b8 refractive)noexcept{ m_refractive = refractive; }
    void setTypedLayout(u64 layoutHash, Span<const MaterialTypedLayoutBlock> blocks, Span<const MaterialTypedLayoutField> fields, Span<const u8> blockBytes);
    void setResourceReferences(Span<const MaterialResourceReference> resourceReferences);
    b8 setShaderForStage(Core::ShaderType::Enum shaderType, const Core::Assets::AssetRef<IShader>& shaderAsset)noexcept;

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

    [[nodiscard]] b8 transparent()const noexcept{ return m_transparent; }

    [[nodiscard]] b8 twoSided()const noexcept{ return m_twoSided; }
    // Refractive-caster flag, separate from `transparent`. Refraction values stay shader-side (NwbMeshSurface).
    [[nodiscard]] b8 refractive()const noexcept{ return m_refractive; }


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
    b8 m_transparent = false;
    b8 m_twoSided = false;
    b8 m_refractive = false;
    Core::Assets::AssetRef<PixelShader> m_avboitAccumulatePixelShader;
    Core::Assets::AssetRef<PixelShader> m_avboitOccupancyPixelShader;
    Core::Assets::AssetRef<PixelShader> m_avboitExtinctionPixelShader;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_ASSET_CODEC(MaterialAssetCodec, Material);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

