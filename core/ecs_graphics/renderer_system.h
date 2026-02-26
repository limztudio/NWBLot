// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "components.h"

#include <core/ecs/world.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_GRAPHICS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererSystem final : public ECS::ISystem, public IRenderPass{
public:
    RendererSystem(ECS::World& world, Graphics& graphics);
    virtual ~RendererSystem()override;


public:
    virtual void update(ECS::World& world, f32 delta)override;

    virtual void render(IFramebuffer* framebuffer)override;
    virtual void backBufferResizing()override;
    virtual void backBufferResized(u32 width, u32 height, u32 sampleCount)override;


private:
    [[nodiscard]] bool ensurePipeline(IFramebuffer* framebuffer);
    [[nodiscard]] Graphics::MeshResource createCubeMesh(const CubeComponent& cube)const;


private:
    ECS::World& m_world;
    Graphics& m_graphics;

    GraphicsPipelineHandle m_pipeline;
    ShaderHandle m_vertexShader;
    ShaderHandle m_pixelShader;
    InputLayoutHandle m_inputLayout;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_GRAPHICS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

