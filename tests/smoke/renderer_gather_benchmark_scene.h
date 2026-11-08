// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "smoke_skinned_scene_helpers.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


namespace Tests::Smoke{


class RendererGatherBenchmarkScene final : NoCopy{
public:
    struct Counts{
        u32 renderers = 0u;
        u32 runtimeRenderers = 0u;
        u32 runtimeOwners = 0u;
        u32 transparentRenderers = 0u;
    };


public:
    explicit RendererGatherBenchmarkScene(Core::ECS::World& world)noexcept : m_world(world){}
    [[nodiscard]] b8 build(AStringView workload, b8 runtime, Core::Alloc::ScratchArena& scratchArena);
    [[nodiscard]] Counts& counts()noexcept{ return m_counts; }


private:
    [[nodiscard]] b8 createBackdrop();
    [[nodiscard]] b8 createRuntime(u32 index, const Float4& position);


private:
    Core::ECS::World& m_world;
    Counts m_counts;
    b8 m_runtime = false;
};


};


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
