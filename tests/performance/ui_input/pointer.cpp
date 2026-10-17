// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "benchmark.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UI_INPUT_BENCHMARK_BEGIN
namespace __hidden_ui_pointer_benchmark{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl::Ui;

namespace WorkloadKind{
    enum Enum : u8{ CursorHover, TouchGesture, CursorGesture, PressedSweep, CompetingContacts, CommitTargets };
};

static constexpr AStringView s_Names[]{ "cursor_hover", "touch_gesture", "cursor_gesture", "pressed_sweep", "competing_contacts", "commit_targets" };
static constexpr Point s_FirstPosition{ 8.0f, 8.0f };
static constexpr Point s_LastPosition{ 9.0f, 8.0f };
static constexpr InputSource s_FirstSource{ 97u, 1u };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Workload final{
public:
    Workload(const usize targetCount, const WorkloadKind::Enum kind, const usize contactCount = 1u)
        : m_arena(Name("tests/performance/ui_input/pointer"))
        , m_router(m_arena)
        , m_targets(m_arena)
        , m_kind(kind)
        , m_contactCount(contactCount)
        , m_hoverCapable(kind == WorkloadKind::CursorHover || kind == WorkloadKind::CursorGesture || kind == WorkloadKind::PressedSweep)
    {
        m_targets.reserve(targetCount);
        m_setupChecksum = MixChecksum(14695981039346656037u, targetCount);
        m_setupChecksum = MixChecksum(m_setupChecksum, kind);
        m_setupChecksum = MixChecksum(m_setupChecksum, contactCount);
        for(usize index = 0u; index < targetCount; ++index){
            HitTarget target;
            target.id = { static_cast<u64>(index + 1u) };
            target.declarationGeneration = 1u;
            target.rectangle = { static_cast<f32>((index % 64u) * 24u), static_cast<f32>((index / 64u) * 24u), 20.0f, 20.0f };
            target.clip = { 0.0f, 0.0f, 1536.0f, 1536.0f };
            target.paintOrder = static_cast<u32>(index);
            target.focusable = true;
            target.activatable = true;
            target.pointerGesture = true;
            m_targets.push_back(target);
            m_setupChecksum = MixChecksum(m_setupChecksum, target.id.value);
            m_setupChecksum = MixChecksum(m_setupChecksum, static_cast<u64>(target.rectangle.x));
            m_setupChecksum = MixChecksum(m_setupChecksum, static_cast<u64>(target.rectangle.y));
            m_setupChecksum = MixChecksum(m_setupChecksum, target.paintOrder);
        }
    }


public:
    [[nodiscard]] bool initialize(){
        if(!m_router.commitTargets(m_targets.data(), m_targets.size(), ++m_generation))
            return false;
        if(m_kind == WorkloadKind::CursorHover || m_kind == WorkloadKind::CommitTargets)
            return true;
        for(usize contact = 1u; contact <= m_contactCount; ++contact){
            PointerInput input;
            input.phase = PointerPhase::Press;
            input.source = { s_FirstSource.device, static_cast<u64>(contact) };
            input.position = s_FirstPosition;
            input.hoverCapable = m_hoverCapable;
            if(!m_router.queuePointer(input))
                return false;
            const InputRoutingResult result = m_router.process();
            if(!result.pointerConsumed || result.pointerRouted != (contact == 1u) || result.capture != WidgetId{ 1u })
                return false;
        }
        const auto gesture = m_router.consumePointerGesture({ 1u }, 1u);
        return gesture && gesture->inputSource == s_FirstSource && move(s_LastPosition);
    }

    [[nodiscard]] bool operation(){
        if(m_kind == WorkloadKind::CommitTargets)
            return m_router.commitTargets(m_targets.data(), m_targets.size(), ++m_generation);
        if(m_kind == WorkloadKind::PressedSweep){
            usize pressed = 0u;
            for(const HitTarget& target : m_targets)
                pressed += m_router.pressed(target.id) ? 1u : 0u;
            m_pressedCount = pressed;
            return pressed == 1u;
        }
        if(!move(s_FirstPosition) || !move(s_LastPosition))
            return false;
        if(m_kind == WorkloadKind::CompetingContacts){
            PointerInput input;
            input.source = { s_FirstSource.device, static_cast<u64>(m_contactCount) };
            input.position = { 32.0f, 8.0f };
            if(!m_router.queuePointer(input))
                return false;
            const InputRoutingResult result = m_router.process();
            if(!result.pointerConsumed || result.pointerRouted || result.capture != WidgetId{ 1u } || result.hover.valid())
                return false;
        }
        return true;
    }

    [[nodiscard]] u64 checksum()const noexcept{
        u64 checksum = MixChecksum(m_setupChecksum, m_router.targets().size());
        checksum = MixChecksum(checksum, m_router.hover().value);
        checksum = MixChecksum(checksum, m_router.focus().value);
        checksum = MixChecksum(checksum, m_router.capture().value);
        checksum = MixChecksum(checksum, m_router.primaryDown());
        checksum = MixChecksum(checksum, m_router.actions().size());
        checksum = MixChecksum(checksum, m_pressedCount);
        if(m_kind != WorkloadKind::CommitTargets && m_kind != WorkloadKind::CursorHover){
            for(usize contact = 1u; contact <= m_contactCount; ++contact)
                checksum = MixChecksum(checksum, m_router.ownsPointerSource({ s_FirstSource.device, static_cast<u64>(contact) }));
        }
        return checksum;
    }

    [[nodiscard]] bool measure(){
        if(!initialize())
            return false;
        const auto name = StringFormat(m_arena, "{}_targets_{}_sources_{}", s_Names[m_kind], m_targets.size(), m_contactCount);
        return Measure(name, m_setupChecksum, [&](){ return operation(); }, [&](){ return checksum(); });
    }

private:
    [[nodiscard]] bool move(const Point& position){
        PointerInput input;
        input.source = s_FirstSource;
        input.position = position;
        input.hoverCapable = m_hoverCapable;
        if(!m_router.queuePointer(input))
            return false;
        const InputRoutingResult result = m_router.process();
        const bool active = m_kind != WorkloadKind::CursorHover;
        if(!result.pointerConsumed || !result.pointerRouted || result.capture != (active ? WidgetId{ 1u } : WidgetId{}) || result.hover != (m_hoverCapable ? WidgetId{ 1u } : WidgetId{}))
            return false;
        if(active){
            const auto gesture = m_router.consumePointerGesture({ 1u }, 1u);
            if(!gesture || gesture->state != PointerGestureState::Active || gesture->inputSource != s_FirstSource || gesture->position.x != position.x || gesture->position.y != position.y)
                return false;
        }
        return !result.activationOverflow && !result.gestureOverflow;
    }


private:
    Core::Alloc::GlobalArena m_arena;
    InputRouter m_router;
    InputVector<HitTarget> m_targets;
    WorkloadKind::Enum m_kind;
    usize m_contactCount;
    bool m_hoverCapable;
    u64 m_setupChecksum = 0u;
    u64 m_generation = 0u;
    usize m_pressedCount = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RunPointerBenchmarks(){
    using namespace __hidden_ui_pointer_benchmark;
    constexpr usize s_Counts[]{ 128u, 1024u, 4096u };
    for(const usize count : s_Counts){
        for(u8 kind = WorkloadKind::CursorHover; kind <= WorkloadKind::PressedSweep; ++kind){
            Workload workload(count, static_cast<WorkloadKind::Enum>(kind));
            if(!workload.measure())
                return false;
        }
    }
    for(const usize contacts : { 16u, 256u }){
        Workload workload(1024u, WorkloadKind::CompetingContacts, contacts);
        if(!workload.measure())
            return false;
    }
    Workload publication(1024u, WorkloadKind::CommitTargets);
    return publication.measure();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UI_INPUT_BENCHMARK_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

