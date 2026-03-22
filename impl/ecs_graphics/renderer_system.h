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

    struct MaterialPipelineResources{
        Core::GraphicsPipelineHandle pipeline;
        Core::ShaderHandle vertexShader;
        Core::ShaderHandle pixelShader;
        Core::InputLayoutHandle inputLayout;
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
    [[nodiscard]] bool ensureGeometryLoaded(const Core::Assets::AssetRef<Geometry>& geometryAsset, Core::Graphics::MeshResource& outMesh);
    [[nodiscard]] bool ensureRendererPipeline(const RendererComponent& renderer, Core::IFramebuffer* framebuffer, MaterialPipelineResources*& outResources);
    [[nodiscard]] bool ensureShaderLoaded(
        Core::ShaderHandle& outShader,
        const Name& shaderName,
        AStringView variantName,
        Core::ShaderType::Mask shaderType,
        const Name& debugName
    );


private:
    Core::ECS::World& m_world;
    Core::Graphics& m_graphics;
    Core::Assets::AssetManager& m_assetManager;
    ShaderPathResolveCallback m_shaderPathResolver;

    HashMap<Name, Core::Graphics::MeshResource, Hasher<Name>, EqualTo<Name>> m_geometryMeshes;
    HashMap<MaterialPipelineKey, MaterialPipelineResources, MaterialPipelineKeyHasher, MaterialPipelineKeyEqualTo> m_materialPipelines;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

