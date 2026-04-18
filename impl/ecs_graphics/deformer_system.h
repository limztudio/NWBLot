// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "deformable_runtime_mesh_cache.h"

#include <core/assets/asset_manager.h>
#include <core/ecs/world.h>
#include <core/graphics/graphics.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererSystem;
class Shader;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class DeformerSystem final : public Core::ECS::ISystem, public Core::IRenderPass{
public:
    struct DeformerMorphRangeGpu{
        u32 firstDelta = 0;
        u32 deltaCount = 0;
        f32 weight = 0.0f;
        u32 padding = 0;
    };

    struct DeformerMorphDeltaGpu{
        u32 vertexId = 0;
        u32 padding[3] = {};
        Float4Data deltaPosition;
        Float4Data deltaNormal;
        Float4Data deltaTangent;
    };


private:
    struct RuntimeResources{
        RuntimeMeshHandle handle;
        u32 editRevision = 0;
        u32 vertexCount = 0;
        u32 morphCount = 0;
        u32 deltaCount = 0;
        usize morphSignature = 0;
        Core::BufferHandle morphRangeBuffer;
        Core::BufferHandle morphDeltaBuffer;
        Core::BindingSetHandle bindingSet;
    };


public:
    using ShaderPathResolveCallback = Function<bool(const Name& shaderName, AStringView variantName, const Name& stageName, Name& outVirtualPath)>;


public:
    DeformerSystem(
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
        const DeformableMorphWeightsComponent* morphWeights
    );
    [[nodiscard]] bool copyRestToDeformed(Core::ICommandList& commandList, DeformableRuntimeMeshInstance& instance);
    [[nodiscard]] bool ensureRuntimeResources(
        DeformableRuntimeMeshInstance& instance,
        const Vector<DeformerMorphRangeGpu>& morphRanges,
        const Vector<DeformerMorphDeltaGpu>& morphDeltas,
        usize morphSignature,
        RuntimeResources*& outResources
    );


private:
    Core::ECS::World& m_world;
    Core::Graphics& m_graphics;
    Core::Assets::AssetManager& m_assetManager;
    RendererSystem& m_rendererSystem;
    ShaderPathResolveCallback m_shaderPathResolver;

    HashMap<u64, RuntimeResources, Hasher<u64>, EqualTo<u64>> m_runtimeResources;
    Core::BindingLayoutHandle m_bindingLayout;
    Core::ShaderHandle m_computeShader;
    Core::ComputePipelineHandle m_computePipeline;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
