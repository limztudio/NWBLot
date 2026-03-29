// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "components.h"

#include <core/ecs/world.h>
#include <core/assets/asset_manager.h>
#include <core/graphics/graphics.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererSystem final : public Core::ECS::ISystem, public Core::IRenderPass{
private:
    enum class RenderPath : u8{
        MeshShader,
        ComputeEmulation,
    };

    struct MaterialPipelineKey{
        Name material = NAME_NONE;
        Core::FramebufferInfo framebufferInfo;
    };
    struct MaterialPipelineKeyHasher{
        usize operator()(const MaterialPipelineKey& key)const;
    };
    struct MaterialPipelineKeyEqualTo{
        bool operator()(const MaterialPipelineKey& lhs, const MaterialPipelineKey& rhs)const;
    };

    struct GeometryResources{
        Name geometryName = NAME_NONE;
        Core::BufferHandle shaderVertexBuffer;
        Core::BufferHandle shaderIndexBuffer;
        Core::BindingSetHandle meshBindingSet;
        Core::BufferHandle emulationVertexBuffer;
        Core::BindingSetHandle computeBindingSet;
        u32 indexCount = 0;
        u32 triangleCount = 0;
        u32 dispatchGroupCount = 0;

        [[nodiscard]] bool valid()const noexcept{
            return geometryName != NAME_NONE
                && shaderVertexBuffer != nullptr
                && shaderIndexBuffer != nullptr
                && indexCount > 0
                && triangleCount > 0
                && dispatchGroupCount > 0;
        }
    };

    struct MaterialPipelineResources{
        RenderPath renderPath = RenderPath::MeshShader;
        Core::GraphicsPipelineHandle emulationPipeline;
        Core::MeshletPipelineHandle meshletPipeline;
        Core::ComputePipelineHandle computePipeline;
        Core::ShaderHandle pixelShader;
        Core::ShaderHandle meshShader;
        Core::ShaderHandle computeShader;
    };


public:
    using ShaderPathResolveCallback = Function<bool(const Name& shaderName, AStringView variantName, const Name& stageName, Name& outVirtualPath)>;


public:
    RendererSystem(
        Core::ECS::World& world,
        Core::Graphics& graphics,
        Core::Assets::AssetManager& assetManager,
        ShaderPathResolveCallback shaderPathResolver
    );
    virtual ~RendererSystem()override;


public:
    virtual void update(Core::ECS::World& world, f32 delta)override;

    virtual void render(Core::IFramebuffer* framebuffer)override;
    virtual void backBufferResizing()override;
    virtual void backBufferResized(u32 width, u32 height, u32 sampleCount)override;


private:
    [[nodiscard]] bool ensureGeometryLoaded(const Core::Assets::AssetRef<Geometry>& geometryAsset, GeometryResources*& outGeometry);
    [[nodiscard]] bool ensureMeshShaderResources();
    [[nodiscard]] bool ensureComputeEmulationResources();
    [[nodiscard]] bool ensureMeshBindingSet(GeometryResources& geometry);
    [[nodiscard]] bool ensureComputeBindingSet(GeometryResources& geometry);
    [[nodiscard]] bool ensureRendererPipeline(const RendererComponent& renderer, Core::IFramebuffer* framebuffer, MaterialPipelineResources*& outResources);
    void logMaterialRenderPathDecision(const Name& materialKey, RenderPath renderPath, bool meshSupported);
    [[nodiscard]] bool ensureShaderLoaded(
        Core::ShaderHandle& outShader,
        const Name& shaderName,
        AStringView variantName,
        Core::ShaderType::Mask shaderType,
        const Name& debugName,
        const Name* archiveStageName = nullptr
    );


private:
    Core::ECS::World& m_world;
    Core::Graphics& m_graphics;
    Core::Assets::AssetManager& m_assetManager;
    ShaderPathResolveCallback m_shaderPathResolver;

    HashMap<Name, GeometryResources, Hasher<Name>, EqualTo<Name>> m_geometryMeshes;
    HashMap<MaterialPipelineKey, MaterialPipelineResources, MaterialPipelineKeyHasher, MaterialPipelineKeyEqualTo> m_materialPipelines;
    HashMap<Name, RenderPath, Hasher<Name>, EqualTo<Name>> m_loggedMaterialPaths;
    Core::BindingLayoutHandle m_meshBindingLayout;
    Core::BindingLayoutHandle m_computeBindingLayout;
    Core::ShaderHandle m_emulationVertexShader;
    Core::InputLayoutHandle m_emulationInputLayout;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

