// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "components.h"

#include <core/ecs/world.h>
#include <core/assets/asset_manager.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererSystem final : public Core::ECS::ISystem, public Core::IRenderPass{
public:
    using ShaderPathResolveCallback = Function<bool(AStringView shaderName, AStringView variantName, AStringView stageName, AString& outVirtualPath)>;

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
    [[nodiscard]] bool ensurePipeline(Core::IFramebuffer* framebuffer);
    [[nodiscard]] bool ensureShaderLoaded(
        Core::ShaderHandle& outShader,
        AStringView shaderName,
        Core::ShaderType::Mask shaderType,
        const Name& debugName
    );
    [[nodiscard]] Core::Graphics::MeshResource createCubeMesh(const CubeComponent& cube)const;


private:
    Core::ECS::World& m_world;
    Core::Graphics& m_graphics;
    Core::Assets::AssetManager& m_assetManager;
    ShaderPathResolveCallback m_shaderPathResolver;

    Core::GraphicsPipelineHandle m_pipeline;
    Core::ShaderHandle m_vertexShader;
    Core::ShaderHandle m_pixelShader;
    Core::InputLayoutHandle m_inputLayout;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

