// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "round_trip_fixture.h"
#include "timing_scopes_test_support.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Records a timing scope inside dynamic rendering, where vkCmdResetQueryPool is illegal. The scope can therefore
// only receive a query when Graphics submitted its timer-query reset preamble before invoking this render pass.
class FrameTimingPreambleProbePass final : public IRenderPass{
public:
    explicit FrameTimingPreambleProbePass(
        GraphicsRuntime& graphics,
        const GpuTimingScopeDefinition& timingScope = s_FrameTimingPreambleScope
    )
        : IRenderPass(graphics)
        , m_timingScope(timingScope)
    {}


public:
    [[nodiscard]] bool initialize(){
        auto& device = getGraphics().getDevice();

        m_target = device.createTexture(
            TextureDesc()
                .setWidth(4u)
                .setHeight(4u)
                .setFormat(Format::RGBA8_UNORM)
                .setInRenderTarget(true)
                .setInitialState(ResourceStates::Common)
        );
        if(!m_target)
            return false;

        m_framebuffer = device.createFramebuffer(FramebufferDesc().addColorAttachment(m_target.get()));
        if(!m_framebuffer)
            return false;

        m_commandList = device.createCommandList();
        return m_commandList != nullptr;
    }

    virtual void render(Framebuffer*)override{
        if(m_recorded || !m_framebuffer || !m_commandList)
            return;

        auto& device = getGraphics().getDevice();
        CommandList* const commandList = m_commandList.get();

        {
            GpuTimingSubmissionTicket timingTicket(getGraphics().gpuTiming());
            {
                GpuTimingSubmissionTicket::RecordingScope timingRecording(timingTicket);

                commandList->open();
                GraphicsState graphicsState;
                graphicsState.setFramebuffer(m_framebuffer.get());
                commandList->setGraphicsState(graphicsState);
                {
                    GpuTimingMeasure timing(getGraphics().gpuTiming(), m_timingScope, device, *commandList);
                }
                commandList->endRenderPass();
                commandList->close();
            }

            CommandList* commandLists[] = { commandList };
            m_recorded = timingTicket.submit(device, commandLists, 1u);
        }
    }

    [[nodiscard]] bool recorded()const{ return m_recorded; }


private:
    const GpuTimingScopeDefinition& m_timingScope;
    TextureHandle m_target;
    FramebufferHandle m_framebuffer;
    CommandListHandle m_commandList;
    bool m_recorded = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

