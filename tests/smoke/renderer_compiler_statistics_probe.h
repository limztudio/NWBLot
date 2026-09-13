// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/alloc/general.h>
#include <core/graphics/runtime/render_pass.h>
#include <core/task/gpu/compiled_graph.h>

#include <global/filesystem.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


namespace Impl{
class RendererSystem;
};


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
        bool valid = false;
    };

    static constexpr usize s_Capacity = 1024u;


public:
    RendererCompilerStatisticsProbe(Core::GraphicsRuntime& graphics, Core::Alloc::GlobalArena& arena);
    virtual ~RendererCompilerStatisticsProbe()override;


public:
    // Registration follows the actual renderer; stop releases its optional borrow before world destruction.
    [[nodiscard]] bool start(Impl::RendererSystem& renderer, NotNull<const char*> path);
    void stop();
    [[nodiscard]] bool write()const;
    virtual bool shouldRenderUnfocused()override{ return true; }
    virtual void render(Core::Framebuffer*)override;


private:
    Vector<Sample, Core::Alloc::GlobalArena> m_samples;
    AString<Core::Alloc::GlobalArena> m_output;
    Impl::RendererSystem* m_renderer = nullptr;
    u64 m_attempts = 0u;
    bool m_enabled = false;
    bool m_registered = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

