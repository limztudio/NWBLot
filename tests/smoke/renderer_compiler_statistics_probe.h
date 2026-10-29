// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/alloc/general.h>
#include <core/graphics/runtime/render_pass.h>
#include <core/task/gpu/compiled_graph.h>

#include <global/filesystem.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Impl{
    class RendererSystem;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Optional test-only observation of existing immutable compiler statistics, with no timer or GPU query of its own.
class RendererCompilerStatisticsProbe final : public Core::IRenderPass{
private:
    struct Sample{
        Core::GpuTaskGraphCompileStatistics compile;
        u64 sourceFrame = 0u;
        u64 recordingAttempt = 0u;
        usize acceptedPackets = 0u;
        usize acceptedTasks = 0u;
        usize rejectedPackets = 0u;
        usize rejectedTasks = 0u;
        b8 valid = false;
    };


private:
    static constexpr usize s_Capacity = 1024u;


public:
    RendererCompilerStatisticsProbe(Core::GraphicsRuntime& graphics, Core::Alloc::GlobalArena& arena)noexcept;
    virtual ~RendererCompilerStatisticsProbe()noexcept override;


public:
    // Release the borrowed renderer pointer before world destruction.
    [[nodiscard]] b8 start(Impl::RendererSystem& renderer, AStringView path);
    void stop();
    [[nodiscard]] b8 write()const;
    virtual b8 shouldRenderUnfocused()noexcept override{ return true; }
    virtual void render(Core::Framebuffer*)override;


private:
    Vector<Sample, Core::Alloc::GlobalArena> m_samples;
    AString<Core::Alloc::GlobalArena> m_output;
    Impl::RendererSystem* m_renderer = nullptr;
    u64 m_attempts = 0u;
    b8 m_enabled = false;
    b8 m_registered = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

