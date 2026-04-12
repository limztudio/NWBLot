// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "common.h"


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
    virtual void render(IFramebuffer*){}
    virtual void animate(f32){}
    virtual void backBufferResizing(){}
    virtual void backBufferResized(u32, u32, u32){}
    virtual void displayScaleChanged(f32, f32){}

    virtual bool keyboardUpdate(i32, i32, i32, i32){ return false; }
    virtual bool keyboardCharInput(u32, i32){ return false; }
    virtual bool mousePosUpdate(f64, f64){ return false; }
    virtual bool mouseScrollUpdate(f64, f64){ return false; }
    virtual bool mouseButtonUpdate(i32, i32, i32){ return false; }

    [[nodiscard]] Graphics& getGraphics()const{ return m_graphics; }


private:
    Graphics& m_graphics;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
