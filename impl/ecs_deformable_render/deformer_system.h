// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "deformer_gpu_payload.h"

#include <core/ecs/system.h>
#include <core/graphics/render_pass.h>
#include <impl/ecs_deformable_render/deformable_runtime_mesh_cache.h>
#include <impl/ecs_geometry/runtime_geometry.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Shader;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class DeformerSystem final
    : public Core::ECS::ISystem
    , public Core::IRenderPass
    , public IRuntimeGeometryProvider
    , public IDeformableRuntimeMeshProvider
{
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
        IRuntimeGeometryRegistry& runtimeGeometryRegistry,
        ShaderPathResolveCallback shaderPathResolver
    );
    virtual ~DeformerSystem()override;


public:
    virtual void update(Core::ECS::World& world, f32 delta)override;
    virtual void render(Core::IFramebuffer* framebuffer)override;

    virtual usize runtimeGeometryCandidateCount()override;
    virtual void forEachRuntimeGeometry(const RuntimeGeometryVisitor& visitor)override;
    virtual bool resolveRuntimeGeometry(Core::ECS::EntityID entity, RuntimeGeometryDesc& outGeometry)override;
    virtual bool containsRuntimeGeometry(const Name& geometryKey, u64 version)override;

    [[nodiscard]] virtual RuntimeMeshHandle deformableRuntimeMeshHandle(Core::ECS::EntityID entity)const override;
    [[nodiscard]] virtual u32 deformableRuntimeMeshEditRevision(RuntimeMeshHandle handle)const override;
    [[nodiscard]] virtual bool bumpDeformableRuntimeMeshRevision(
        RuntimeMeshHandle handle,
        RuntimeMeshDirtyFlags dirtyFlags = RuntimeMeshDirtyFlag::All
    )override;
    [[nodiscard]] virtual DeformableRuntimeMeshInstance* findDeformableRuntimeMesh(RuntimeMeshHandle handle)override;
    [[nodiscard]] virtual const DeformableRuntimeMeshInstance* findDeformableRuntimeMesh(
        RuntimeMeshHandle handle
    )const override;


private:
    [[nodiscard]] bool ensurePipeline();
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
    IRuntimeGeometryRegistry& m_runtimeGeometryRegistry;
    ShaderPathResolveCallback m_shaderPathResolver;
    DeformableRuntimeMeshCache m_runtimeMeshCache;

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

