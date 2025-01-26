// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/global.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class VulkanEngine;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Graphics{
public:
    Graphics();
    ~Graphics();


public:
    bool init(u16 width, u16 height);
    void destroy();


private:
    std::unique_ptr<VulkanEngine> m_engine;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

