// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "benchmark.h"

#include <impl/ecs_ui/toolkit/paint.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UI_INPUT_BENCHMARK_BEGIN
namespace __hidden_ui_paint_benchmark{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;
using namespace NWB::Impl::Ui;

class PaintCase final{
public:
    PaintCase(const u32 commands, const bool descending)
        : m_arena(Name("tests/performance/ui_input/paint"))
        , m_skin(m_arena, Name("tests/performance/ui_input/skin"))
        , m_builder(m_arena)
        , m_snapshot(m_arena)
        , m_commands(commands)
        , m_descending(descending)
    {
        UiSkin::RegionVector regions(m_arena);
        m_skin.setAtlas(Core::Assets::AssetRef<Texture>("tests/performance/ui_input/texture"), 8u, 8u, 1.0f, Move(regions));
    }


public:
    [[nodiscard]] bool operation(){
        m_builder.begin({ 128.0f, 128.0f }, 1u, 1u, Core::Assets::AssetRef<UiSkin>("tests/performance/ui_input/skin"), m_skin);
        m_builder.reserve(m_commands);
        for(u32 command = 0u; command < m_commands; ++command){
            if(m_descending && !m_builder.beginOverlay(m_commands - command))
                return false;
            m_builder.pushClip({ static_cast<f32>(command & 1u), 0.0f, 64.0f, 64.0f });
            m_builder.fillRect({ 0.0f, 0.0f, 100.0f, 100.0f });
            if(!m_builder.popClip() || (m_descending && !m_builder.endOverlay()))
                return false;
        }
        m_snapshot = m_builder.freeze();
        return m_snapshot.commands().size() == m_commands && m_snapshot.indices().size() == static_cast<usize>(m_commands) * 6u && m_snapshot.vertices().size() == static_cast<usize>(m_commands) * 4u;
    }

    [[nodiscard]] Expected<u64> checksum()const noexcept{
        if(m_snapshot.commands().size() != m_commands || m_snapshot.indices().size() != static_cast<usize>(m_commands) * 6u || m_snapshot.vertices().size() != static_cast<usize>(m_commands) * 4u)
            return MakeUnexpected(Failure{});
        u64 hash = MixChecksum(1469598103934665603u, m_commands);
        for(u32 index = 0u; index < m_commands; ++index){
            const DrawCommand& command = m_snapshot.commands()[index];
            const u32 expectedLayer = m_descending ? index + 1u : 0u;
            const u32 expectedFirst = (m_descending ? m_commands - index - 1u : index) * 6u;
            if(command.layer != expectedLayer || command.firstIndex != expectedFirst || command.indexCount != 6u || command.material != PaintMaterial::Solid)
                return MakeUnexpected(Failure{});
            hash = MixChecksum(hash, command.layer);
            hash = MixChecksum(hash, command.firstIndex);
            hash = MixChecksum(hash, command.indexCount);
            hash = MixChecksum(hash, static_cast<u64>(command.clip.x));
        }
        for(const Vertex& vertex : m_snapshot.vertices()){
            hash = MixChecksum(hash, static_cast<u64>(vertex.position.x));
            hash = MixChecksum(hash, static_cast<u64>(vertex.position.y));
        }
        for(const u32 index : m_snapshot.indices())
            hash = MixChecksum(hash, index);
        return hash;
    }


private:
    Core::Alloc::GlobalArena m_arena;
    UiSkin m_skin;
    PaintBuilder m_builder;
    DrawSnapshot m_snapshot;
    const u32 m_commands;
    const bool m_descending;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RunPaintBenchmarks(){
    constexpr u32 s_Counts[]{ 64u, 256u, 1024u };
    constexpr AStringView s_MonotonicNames[]{ "paint.monotonic.64", "paint.monotonic.256", "paint.monotonic.1024" };
    constexpr AStringView s_DescendingNames[]{ "paint.descending.64", "paint.descending.256", "paint.descending.1024" };
    for(usize index = 0u; index < 3u; ++index){
        for(const bool descending : { false, true }){
            __hidden_ui_paint_benchmark::PaintCase benchmark(s_Counts[index], descending);
            if(!benchmark.operation() || !benchmark.checksum())
                return false;
            const u64 setup = MixChecksum(MixChecksum(1469598103934665603u, s_Counts[index]), descending ? 1u : 0u);
            if(!Measure(descending ? s_DescendingNames[index] : s_MonotonicNames[index], setup, [&benchmark](){ return benchmark.operation(); }, [&benchmark](){ return benchmark.checksum(); }))
                return false;
        }
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UI_INPUT_BENCHMARK_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

