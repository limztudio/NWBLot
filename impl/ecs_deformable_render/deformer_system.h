// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/assets/asset_manager.h>
#include <core/ecs/system.h>
#include <core/graphics/graphics.h>
#include <impl/ecs_deformable/deformable_runtime_mesh_cache.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererSystem;
class Shader;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class DeformerSystem final : public Core::ECS::ISystem, public Core::IRenderPass{
public:
    struct alignas(Float4) DeformerVertexMorphRangeGpu{
        u32 firstDelta = 0;
        u32 deltaCount = 0;
        u32 padding0 = 0;
        u32 padding1 = 0;
    };

    struct alignas(Float4) DeformerBlendedMorphDeltaGpu{
        Float4 deltaPosition = Float4(0.0f, 0.0f, 0.0f, 0.0f);
        Float4 deltaNormal = Float4(0.0f, 0.0f, 0.0f, 0.0f);
        Float4 deltaTangent = Float4(0.0f, 0.0f, 0.0f, 0.0f);
    };

    struct alignas(Float4) DeformerSkinInfluenceGpu{
        u32 joint[4] = {};
        Float4 weight = Float4(0.0f, 0.0f, 0.0f, 0.0f);
    };


private:
    struct RuntimeResources{
        RuntimeMeshHandle handle;
        u32 editRevision = 0;
        u32 vertexCount = 0;
        u32 morphRangeCount = 0;
        u32 deltaCount = 0;
        u32 skinCount = 0;
        u32 jointCount = 0;
        usize morphSignature = 0;
        Name displacementTextureName = NAME_NONE;
        Core::BufferHandle morphRangeBuffer;
        Core::BufferHandle morphDeltaBuffer;
        Core::BufferHandle skinBuffer;
        Core::BufferHandle jointPaletteBuffer;
        Core::TextureHandle displacementTexture;
        Core::BindingSetHandle bindingSet;
    };

    struct RuntimePayloadViews{
        const DeformerVertexMorphRangeGpu* morphRanges = nullptr;
        const DeformerBlendedMorphDeltaGpu* morphDeltas = nullptr;
        const DeformerSkinInfluenceGpu* skinInfluences = nullptr;
        const DeformableJointMatrix* jointPalette = nullptr;
        usize morphRangeCount = 0;
        usize morphDeltaCount = 0;
        usize skinInfluenceCount = 0;
        usize jointPaletteCount = 0;

        [[nodiscard]] bool hasActiveMorphs()const{ return morphRangeCount != 0u && morphDeltaCount != 0u; }
        [[nodiscard]] bool hasActiveSkin()const{ return skinInfluenceCount != 0u && jointPaletteCount != 0u; }
    };


public:
    using ShaderPathResolveCallback = Function<
        bool(const Name& shaderName, AStringView variantName, const Name& stageName, Name& outVirtualPath)
    >;


public:
    DeformerSystem(
        Core::Alloc::CustomArena& arena,
        Core::ECS::World& world,
        Core::Graphics& graphics,
        Core::Assets::AssetManager& assetManager,
        RendererSystem& rendererSystem,
        ShaderPathResolveCallback shaderPathResolver
    );
    virtual ~DeformerSystem()override;


public:
    virtual void update(Core::ECS::World& world, f32 delta)override;
    virtual void render(Core::IFramebuffer* framebuffer)override;


private:
    [[nodiscard]] bool ensurePipeline();
    [[nodiscard]] bool ensureShaderLoaded(
        Core::ShaderHandle& outShader,
        const Name& shaderName,
        AStringView variantName,
        Core::ShaderType::Mask shaderType,
        const Name& debugName
    );
    [[nodiscard]] bool dispatchRuntimeMesh(
        Core::ICommandList& commandList,
        DeformableRuntimeMeshInstance& instance,
        const DeformableMorphWeightsComponent* morphWeights,
        const DeformableJointPaletteComponent* jointPalette,
        const DeformableSkeletonPoseComponent* skeletonPose,
        const DeformableDisplacement& resolvedDisplacement
    );
    [[nodiscard]] bool copyRestToDeformed(Core::ICommandList& commandList, DeformableRuntimeMeshInstance& instance);
    [[nodiscard]] bool ensureRuntimeResources(
        DeformableRuntimeMeshInstance& instance,
        const RuntimePayloadViews& payloadViews,
        const DeformableDisplacement& displacement,
        bool hasDisplacement,
        usize morphSignature,
        RuntimeResources*& outResources,
        bool& outResourcesRebuilt
    );
    [[nodiscard]] bool ensureDefaultDeformerBuffers();
    [[nodiscard]] bool ensureDefaultDisplacementResources();
    [[nodiscard]] bool ensureDisplacementTexture(const DeformableDisplacement& displacement, Core::TextureHandle& outTexture);


private:
    using RuntimeResourceMapAllocator = Core::Alloc::CustomAllocator<Pair<const u64, RuntimeResources>>;

    Core::ECS::World& m_world;
    Core::Graphics& m_graphics;
    Core::Assets::AssetManager& m_assetManager;
    RendererSystem& m_rendererSystem;
    ShaderPathResolveCallback m_shaderPathResolver;

    HashMap<u64, RuntimeResources, Hasher<u64>, EqualTo<u64>, RuntimeResourceMapAllocator> m_runtimeResources;
    Core::BindingLayoutHandle m_bindingLayout;
    Core::ShaderHandle m_computeShader;
    Core::ComputePipelineHandle m_computePipeline;
    Core::BufferHandle m_defaultMorphRangeBuffer;
    Core::BufferHandle m_defaultMorphDeltaBuffer;
    Core::BufferHandle m_defaultSkinBuffer;
    Core::BufferHandle m_defaultJointPaletteBuffer;
    Core::TextureHandle m_defaultDisplacementTexture;
    Core::SamplerHandle m_displacementSampler;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

