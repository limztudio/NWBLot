// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "module.h"


NWB_VULKAN_BEGIN


class CommandList;
class Framebuffer;

class FramebufferRenderingPreparation final{
public:
    explicit FramebufferRenderingPreparation(CommandList& commandList)noexcept : m_commandList(commandList){}
    [[nodiscard]] b8 validateFramebuffer(Framebuffer* framebuffer, TStringView operationName);
    [[nodiscard]] b8 validateRenderPassBegin(Framebuffer& framebuffer, const RenderPassParameters& params, TStringView operationName);
    [[nodiscard]] b8 prepareFramebuffer(Framebuffer* framebuffer, TStringView operationName);

private:
    CommandList& m_commandList;
};


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

