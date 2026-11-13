// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <global/assert.h>

#include <core/graphics/api.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GraphicsRuntime;
using FramebufferPtr = Framebuffer*;
using TexturePtr = Texture*;

[[nodiscard]] inline b8 IsValidFramebufferPtr(const FramebufferPtr framebuffer)noexcept{
    return framebuffer != nullptr;
}
[[nodiscard]] inline Framebuffer& DerefFramebufferPtr(const FramebufferPtr framebuffer)noexcept{
    FOUNDATION_ASSERT_MSG(framebuffer != nullptr, "Attempted to dereference a null FramebufferPtr.");
    return *framebuffer;
}

[[nodiscard]] inline b8 IsValidTexturePtr(const TexturePtr texture)noexcept{
    return texture != nullptr;
}
[[nodiscard]] inline Texture& DerefTexturePtr(const TexturePtr texture)noexcept{
    FOUNDATION_ASSERT_MSG(texture != nullptr, "Attempted to dereference a null TexturePtr.");
    return *texture;
}

class IRenderPass{
public:
    explicit IRenderPass(GraphicsRuntime& graphics)noexcept
        : m_graphics(graphics)
    {}
    virtual ~IRenderPass()noexcept = default;


public:
    virtual b8 shouldRenderUnfocused(){ return false; }

    virtual b8 validateResources(u32, u32, u32){ return true; }

    virtual void invalidateResources(){}

    virtual b8 prepareResources(Framebuffer*){ return true; }

    virtual void render(Framebuffer*){}

    virtual void animate(f32){}

    virtual void backBufferResizing(){}

    virtual void backBufferResized(u32, u32, u32){}

    virtual void displayScaleChanged(f32, f32){}

    [[nodiscard]] GraphicsRuntime& getGraphics()const noexcept{ return m_graphics; }


private:
    GraphicsRuntime& m_graphics;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

