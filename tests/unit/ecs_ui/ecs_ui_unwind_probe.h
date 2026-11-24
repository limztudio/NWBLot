// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/ecs/system.h>
#include <core/ecs/world.h>
#include <impl/ecs_ui/edit_box_host.adhd.h>
#include <impl/ecs_ui/edit_clipboard_controller.adhd.h>
#include <impl/ecs_ui/text_edit_session.adhd.h>
#include <tests/common/gtest_sdk.h>
#include <tests/common/memory_sdk.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN
namespace Tests::EcsUiUnwind{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class NthFailEcsUiArena final : public ArenaBase{
public:
    NthFailEcsUiArena()
        : ArenaBase(Name("tests/ecs_ui/nth_fail"))
        , m_backing(Name("tests/ecs_ui/nth_fail_backing"))
    {}

    void arm(const usize failOnCall)noexcept{
        m_armed = true;
        m_remaining = failOnCall;
        m_hit = false;
    }

    void disarm()noexcept{
        m_armed = false;
        m_remaining = 0u;
        m_hit = false;
    }

    [[nodiscard]] b8 hit()const noexcept{ return m_hit; }

    void* allocate(usize align, usize size)override{
        if(shouldFail())
            return nullptr;
        return m_backing.allocate(align, size);
    }

    void* reallocate(void* memory, usize align, usize size)override{
        if(shouldFail())
            return nullptr;
        return m_backing.reallocate(memory, align, size);
    }

    void deallocate(void* memory, usize align, usize size)noexcept override{
        m_backing.deallocate(memory, align, size);
    }

private:
    [[nodiscard]] b8 shouldFail()noexcept{
        if(!m_armed)
            return false;
        if(m_remaining <= 1u){
            m_hit = true;
            return true;
        }
        --m_remaining;
        return false;
    }

private:
    Core::Alloc::GlobalArena m_backing;
    usize m_remaining = 0u;
    b8 m_armed = false;
    b8 m_hit = false;
};

inline void TriggerProbeOom(){
    NthFailEcsUiArena arena;
    Vector<u8, NthFailEcsUiArena> probe(arena);
    arena.arm(1u);
    probe.reserve(128u);
}

[[nodiscard]] inline b8 ExecuteSchedulerExpectingOom(Core::ECS::SystemScheduler& scheduler, Core::ECS::World& world){
    return static_cast<b8>(ThrowsException<AllocationException>([&](){
        return scheduler.execute(world, 0.016f).has_value();
    }));
}

[[nodiscard]] inline b8 VerifyUiEditBoxHostEditOom(Core::ITextInputService& textInput, Core::IClipboardService& clipboard){
    Core::Alloc::GlobalArena support(Name("tests/ecs_ui/host_oom_support"));
    Impl::Ui::EditModel model(support);
    AString<Core::Alloc::GlobalArena> longText(512u, 'a', support);
    if(!model.setText(AStringView(longText)))
        return false;

    usize newEntryHits = 0u;
    for(usize failAt = 1u; failAt <= 32u; ++failAt){
        NthFailEcsUiArena hostArena;
        Impl::Ui::Context context(support);
        Impl::UiEditBoxHost host(hostArena, context, textInput, clipboard);

        Impl::Ui::DisplayMetrics metrics{};
        metrics.logicalWidth = 500.0f;
        metrics.logicalHeight = 200.0f;
        Impl::Ui::WidgetRoot root{};
        root.value = 17u;
        root.generation = 1u;

        if(!context.beginFrame(1u))
            return false;
        host.beginFrame(1u, metrics);
        if(!context.beginRoot(root))
            return false;
        auto widget = context.declare("edit", Impl::Ui::WidgetKind::EditBox);
        if(!widget.has_value())
            return false;

        Impl::Ui::EditBoxOptions options{};
        hostArena.arm(failAt);
        Impl::Ui::EditBoxResult result{};
        const b8 threw = static_cast<b8>(ThrowsException<AllocationException>([&](){
            result = host.edit(*widget.value(), options, model);
            return result.valid;
        }));
        const b8 hit = hostArena.hit();
        hostArena.disarm();

        if(!threw){
            if(hit || !result.valid || newEntryHits == 0u)
                return false;
            if(!context.endRoot() || !context.finishFrame() || !context.commitFrame(1u))
                return false;
            break;
        }
        if(!hit)
            return false;
        ++newEntryHits;

        host.finishFrame();
        if(!context.endRoot() || !context.finishFrame())
            return false;
        host.commitFrame(1u);
        if(!context.commitFrame(1u))
            return false;

        if(!context.beginFrame(2u))
            return false;
        host.beginFrame(2u, metrics);
        if(!context.beginRoot(root))
            return false;
        auto widget2 = context.declare("edit", Impl::Ui::WidgetKind::EditBox);
        if(!widget2.has_value())
            return false;
        const auto recovered = host.edit(*widget2.value(), options, model);
        if(!recovered.valid)
            return false;
        host.finishFrame();
        if(!context.endRoot() || !context.finishFrame())
            return false;
        host.commitFrame(2u);
        if(!context.commitFrame(2u))
            return false;
    }

    NthFailEcsUiArena hostArena;
    Impl::Ui::Context context(support);
    Impl::UiEditBoxHost host(hostArena, context, textInput, clipboard);
    Impl::Ui::EditModel growingModel(support);
    if(!growingModel.setText("seed"))
        return false;

    Impl::Ui::DisplayMetrics metrics{};
    metrics.logicalWidth = 500.0f;
    metrics.logicalHeight = 200.0f;
    Impl::Ui::WidgetRoot root{};
    root.value = 17u;
    root.generation = 1u;
    Impl::Ui::EditBoxOptions options{};

    if(!context.beginFrame(1u))
        return false;
    host.beginFrame(1u, metrics);
    if(!context.beginRoot(root))
        return false;
    auto widget = context.declare("edit", Impl::Ui::WidgetKind::EditBox);
    if(!widget.has_value() || !host.edit(*widget.value(), options, growingModel).valid)
        return false;
    host.finishFrame();
    if(!context.endRoot() || !context.finishFrame())
        return false;
    host.commitFrame(1u);
    if(!context.commitFrame(1u))
        return false;

    if(!growingModel.setText(AStringView(longText)))
        return false;

    if(!context.beginFrame(2u))
        return false;
    host.beginFrame(2u, metrics);
    if(!context.beginRoot(root))
        return false;
    auto widget2 = context.declare("edit", Impl::Ui::WidgetKind::EditBox);
    if(!widget2.has_value())
        return false;

    hostArena.arm(1u);
    const b8 threwExisting = static_cast<b8>(ThrowsException<AllocationException>([&](){
        return host.edit(*widget2.value(), options, growingModel).valid;
    }));
    const b8 hitExisting = hostArena.hit();
    hostArena.disarm();
    if(!threwExisting || !hitExisting)
        return false;

    host.finishFrame();
    if(!context.endRoot() || !context.finishFrame())
        return false;
    host.commitFrame(2u);
    if(!context.commitFrame(2u))
        return false;

    if(!context.beginFrame(3u))
        return false;
    host.beginFrame(3u, metrics);
    if(!context.beginRoot(root))
        return false;
    auto widget3 = context.declare("edit", Impl::Ui::WidgetKind::EditBox);
    if(!widget3.has_value() || !host.edit(*widget3.value(), options, growingModel).valid)
        return false;
    host.finishFrame();
    if(!context.endRoot() || !context.finishFrame())
        return false;
    host.commitFrame(3u);
    return context.commitFrame(3u);
}

[[nodiscard]] inline b8 VerifyUiTextEditSessionOom(Core::ITextInputService& textInput){
    Core::Alloc::GlobalArena modelArena(Name("tests/ecs_ui/session_model_oom"));
    Impl::Ui::EditModel model(modelArena);
    AString<Core::Alloc::GlobalArena> longText(512u, 'm', modelArena);
    if(!model.setText(AStringView(longText)))
        return false;

    Impl::UiTextEditOwner owner{};
    owner.widget.value = 41u;
    owner.declarationGeneration = 1u;
    owner.modelGeneration = 1u;
    const Core::TextInputRect caret(10, 20, 1, 18);

    usize hits = 0u;
    for(usize failAt = 1u; failAt <= 16u; ++failAt){
        NthFailEcsUiArena sessionArena;
        Impl::UiTextEditSession session(sessionArena, textInput);
        sessionArena.arm(failAt);
        Core::TextInputAdmission::Enum admission = Core::TextInputAdmission::Unavailable;
        const b8 threw = static_cast<b8>(ThrowsException<AllocationException>([&](){
            admission = session.begin(owner, caret, model);
            return admission == Core::TextInputAdmission::Accepted;
        }));
        const b8 hit = sessionArena.hit();
        sessionArena.disarm();

        if(!threw){
            if(hit || admission != Core::TextInputAdmission::Accepted || !session.token().valid() || hits == 0u)
                return false;
            return session.end(owner, model);
        }
        if(!hit || session.token().valid())
            return false;
        ++hits;

        if(session.begin(owner, caret, model) != Core::TextInputAdmission::Accepted || !session.token().valid())
            return false;
        if(!session.end(owner, model))
            return false;
    }
    return false;
}

[[nodiscard]] inline b8 VerifyUiEditClipboardControllerOom(Core::IClipboardService& clipboard){
    Core::Alloc::GlobalArena modelArena(Name("tests/ecs_ui/clipboard_model_oom"));
    Impl::Ui::EditModel model(modelArena);
    AString<Core::Alloc::GlobalArena> longText(512u, 'c', modelArena);
    if(!model.setText(AStringView(longText)) || !model.selectAll())
        return false;

    Impl::UiTextEditOwner owner{};
    owner.widget.value = 51u;
    owner.declarationGeneration = 1u;
    owner.modelGeneration = 1u;

    usize hits = 0u;
    for(usize failAt = 1u; failAt <= 16u; ++failAt){
        NthFailEcsUiArena clipArena;
        Impl::UiEditClipboardController clip(clipArena, clipboard);
        clipArena.arm(failAt);
        Impl::UiEditClipboardResult res{};
        const b8 threw = static_cast<b8>(ThrowsException<AllocationException>([&](){
            res = clip.request(owner, model, Impl::Ui::EditClipboardAction::Cut, Core::ClipboardChannel::Clipboard, false);
            return res.status == Impl::UiEditClipboardStatus::Pending;
        }));
        const b8 hit = clipArena.hit();
        clipArena.disarm();

        if(!threw){
            if(hit || res.status != Impl::UiEditClipboardStatus::Pending || !clip.pending() || !clip.token().valid() || hits == 0u)
                return false;
            return clip.cancel() && !clip.pending();
        }
        if(!hit || clip.pending() || clip.token().valid())
            return false;
        ++hits;

        const auto retry = clip.request(owner, model, Impl::Ui::EditClipboardAction::Cut, Core::ClipboardChannel::Clipboard, false);
        if(retry.status != Impl::UiEditClipboardStatus::Pending || !clip.pending() || !clip.token().valid())
            return false;
        if(!clip.cancel() || clip.pending())
            return false;
    }
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};
NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

