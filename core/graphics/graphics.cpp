// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "graphics.h"

#include <logger/client/logger.h>

#include "vulkan/engine.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Graphics::Graphics()
    : m_engine(std::make_unique<VulkanEngine>())
{}
Graphics::~Graphics(){}

bool Graphics::init(u16 width, u16 height){ return m_engine->init(width, height); }
void Graphics::destroy(){ m_engine.release(); }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

