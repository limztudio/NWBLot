// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "api.h"

#include <core/telemetry/frame_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Graphics;

class IRenderPass{
public:
    explicit IRenderPass(Graphics& graphics)
        : m_graphics(graphics)
    {}
    virtual ~IRenderPass() = default;


public:
    virtual void setLatewarpOptions(){}
    virtual bool shouldRenderUnfocused(){ return false; }

    virtual bool validateResources(u32, u32, u32){ return true; }
    virtual void invalidateResources(){}

    virtual bool prepareResources(Framebuffer*){ return true; }
    virtual void render(Framebuffer*){}
    virtual bool appendFrameGraph(
        Telemetry::FrameGraphNodeDescs&,
        Telemetry::FrameGraphEdgeDescs&,
        u32&,
        u32&
    ){
        return false;
    }
    virtual void animate(f32){}

    virtual void backBufferResizing(){}
    virtual void backBufferResized(u32, u32, u32){}
    virtual void displayScaleChanged(f32, f32){}

    [[nodiscard]] Graphics& getGraphics()const{ return m_graphics; }


private:
    Graphics& m_graphics;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

