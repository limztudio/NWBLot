// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "combo_fixture.h"

#include <impl/ecs_ui/toolkit/widgets/text_area.h>
#include <impl/ecs_ui/toolkit/input/bindings.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UiTextAreaTests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace UiComboTests;

namespace AreaEventKind{
    enum Enum : u8{ Text, Key, Action, Selection };
};

struct AreaEvent{
    AString<Core::Alloc::GlobalArena> text;
    InputCommandIntent intent;
    usize anchor = 0u;
    usize caret = 0u;
    EditAction::Enum action = EditAction::Submit;
    AreaEventKind::Enum kind = AreaEventKind::Text;


public:
    explicit AreaEvent(Core::Alloc::GlobalArena& arena) : text(arena){}
};

struct AreaPublication{
    AString<Core::Alloc::GlobalArena> text;
    EditBoxPlacement placement;
    PopupToken popup;


public:
    explicit AreaPublication(Core::Alloc::GlobalArena& arena) : text(arena){}
};

struct AreaResolution{
    AString<Core::Alloc::GlobalArena> text;
    EditNavigationSnapshot preferred;
    Expected<EditNavigationResult> result = MakeUnexpected(Failure{});
    usize caret = 0u;


public:
    explicit AreaResolution(Core::Alloc::GlobalArena& arena) : text(arena){}
};

class TextAreaHost final : public IEditBoxHost{
public:
    TextAreaHost(Core::Alloc::GlobalArena& arena, const Context& context)
        : publications(arena)
        , resolutions(arena)
        , actionTexts(arena)
        , m_arena(arena)
        , m_context(context)
        , m_events(arena)
        , m_bindings(arena)
    {}
    virtual ~TextAreaHost()override = default;


public:
    void text(const AStringView value){
        AreaEvent event(m_arena);
        event.text.assign(value.data(), value.size());
        m_events.push_back(Move(event));
    }

    void key(const i32 value, const bool control = false, const bool shift = false){
        AreaEvent event(m_arena);
        event.kind = AreaEventKind::Key;
        const i32 modifiers = (control ? Core::InputModifier::Control : 0) | (shift ? Core::InputModifier::Shift : 0);
        event.intent = m_bindings.resolve(value, modifiers);
        m_events.push_back(Move(event));
    }

    void selection(const usize anchor, const usize caret){
        AreaEvent event(m_arena);
        event.kind = AreaEventKind::Selection;
        event.anchor = anchor;
        event.caret = caret;
        m_events.push_back(Move(event));
    }

    void action(const EditAction::Enum value){
        AreaEvent event(m_arena);
        event.kind = AreaEventKind::Action;
        event.action = value;
        m_events.push_back(Move(event));
    }

    [[nodiscard]] virtual EditBoxResult edit(const WidgetState&, EditModel&, const EditBoxOptions&)noexcept override{
        ++ordinaryLoans;
        return {};
    }

    [[nodiscard]] virtual EditBoxResult editInPopup(const WidgetState&, EditModel&, const EditBoxOptions&, const PopupToken&)noexcept override{ return {}; }

    [[nodiscard]] virtual EditBoxResult editActions(const WidgetState&, EditModel&, const EditBoxOptions&, const PopupToken&, IEditActionSink&)noexcept override{
        ++actionLoans;
        return {};
    }

    [[nodiscard]] virtual EditBoxResult editNavigated(const WidgetState& widget, EditModel& model, const EditBoxOptions& options, const PopupToken& popup, EditNavigationState& navigation, IEditNavigationResolver& resolver, IEditActionSink& sink)override{
        ++loans;
        lastPopup = popup;
        EditBoxResult result;
        result.valid = true;
        result.focused = options.enabled && focused;
        const u64 revision = model.revision();
        const usize anchor = model.anchor();
        const usize caret = model.caret();
        if(seedColumn)
            result.valid = navigation.setPreferredX(seededColumn);
        seedColumn = false;
        for(const auto& event : m_events){
            if(!result.valid)
                break;
            const u64 beforeRevision = model.revision();
            const u64 beforeSelection = model.selectionGeneration();
            const u64 beforeComposition = model.compositionGeneration();
            bool vertical = false;
            if(event.kind == AreaEventKind::Action){
                result.valid = applyAction(model, navigation, sink, event.action, options.readOnly, result);
            }
            else if(options.enabled && event.kind == AreaEventKind::Text && !options.readOnly)
                result.valid = model.replaceSelection(event.text);
            else if(options.enabled && event.kind == AreaEventKind::Selection)
                result.valid = model.setSelection(event.anchor, event.caret);
            else if(options.enabled && event.kind == AreaEventKind::Key){
                const auto direction = TranslateEditNavigation(event.intent);
                vertical = direction.has_value();
                if(vertical && !model.composition().active){
                    resolutions.emplace_back(m_arena);
                    AreaResolution& call = resolutions.back();
                    call.text.assign(model.text().data(), model.text().size());
                    call.caret = model.caret();
                    call.preferred = navigation.snapshot();
                    call.result = resolver.resolve(model, *direction, call.preferred, viewportHeight);
                    if(call.result){
                        const usize targetAnchor = event.intent.extend ? model.anchor() : call.result->committedByte;
                        result.valid = model.setSelection(targetAnchor, call.result->committedByte) && navigation.setPreferredX(call.result->preferredX);
                    }
                    else
                        result.valid = false;
                }
                else if(!vertical){
                    const EditCommandRequest request = TranslateEditCommand(event.intent, false, model.textMode());
                    const auto command = ApplyEditCommand(model, request, options.readOnly);
                    if(command.submitted || command.cancelled){
                        const auto action = command.submitted ? EditAction::Submit : EditAction::Cancel;
                        result.valid = applyAction(model, navigation, sink, action, options.readOnly, result);
                    }
                }
            }
            if(!vertical && (model.revision() != beforeRevision || model.selectionGeneration() != beforeSelection || model.compositionGeneration() != beforeComposition))
                navigation.reset();
        }
        m_events.clear();
        if(loanHook && (!loanHookWidget.valid() || loanHookWidget == widget.id)){
            Function<void()> hook = Move(loanHook);
            loanHook = {};
            hook();
        }
        result.textChanged = model.revision() != revision;
        result.selectionChanged = model.anchor() != anchor || model.caret() != caret;
        return result;
    }

    [[nodiscard]] virtual bool publish(const WidgetState& widget, const EditBoxView& view, const EditBoxPlacement& placement, const EditBoxOptions&)override{
        ++publishes;
        publications.emplace_back(m_arena);
        AreaPublication& record = publications.back();
        record.text.assign(view.displayText().data(), view.displayText().size());
        record.placement = placement;
        record.popup = m_context.popupToken();
        viewportHeight = placement.content.height;
        if(publishHook && (!publishHookWidget.valid() || publishHookWidget == widget.id)){
            Function<void()> hook = Move(publishHook);
            publishHook = {};
            hook();
        }
        return true;
    }


private:
    [[nodiscard]] bool applyAction(EditModel& model, EditNavigationState& navigation, IEditActionSink& sink, const EditAction::Enum action, const bool readOnly, EditBoxResult& result){
        actionTexts.emplace_back(m_arena);
        actionTexts.back().assign(model.text().data(), model.text().size());
        if(!sink.apply(model, action, readOnly))
            return false;
        result.submitted |= action == EditAction::Submit;
        result.cancelled |= action == EditAction::Cancel;
        result.blurred |= action == EditAction::Blur;
        result.abandoned |= action == EditAction::Abandon;
        if(action == EditAction::Cancel || action == EditAction::Blur || action == EditAction::Abandon){
            navigation.reset();
            if(action == EditAction::Cancel || action == EditAction::Blur)
                result.focused = false;
        }
        return !m_context.failed();
    }


public:
    PaintVector<AreaPublication> publications;
    PaintVector<AreaResolution> resolutions;
    PaintVector<AString<Core::Alloc::GlobalArena>> actionTexts;
    Function<void()> loanHook;
    Function<void()> publishHook;
    WidgetId loanHookWidget;
    WidgetId publishHookWidget;
    PopupToken lastPopup;
    f32 viewportHeight = 80.0f;
    f32 seededColumn = 0.0f;
    usize loans = 0u;
    usize ordinaryLoans = 0u;
    usize actionLoans = 0u;
    usize publishes = 0u;
    bool seedColumn = false;
    bool focused = true;


private:
    Core::Alloc::GlobalArena& m_arena;
    const Context& m_context;
    PaintVector<AreaEvent> m_events;
    InputBindings m_bindings;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class TextAreaFixture : public WidgetFixture{
public:
    TextAreaFixture()
        : m_model(m_arena, {}, EditTextMode::Multiline)
        , m_host(m_arena, m_context)
    {}


protected:
    [[nodiscard]] bool beginArea(const u64 generation, const Rect& bounds = { 20.0f, 20.0f, 740.0f, 520.0f }){
        return begin(generation) && m_builder.beginPanel("panel", bounds);
    }

    [[nodiscard]] bool acceptArea(){ return finishPanel() && m_context.commitFrame(m_context.readyGeneration()); }

    [[nodiscard]] bool frameArea(const u64 generation, const TextAreaOptions& options = {}, const Rect& bounds = { 20.0f, 20.0f, 740.0f, 520.0f }){
        if(!beginArea(generation, bounds))
            return false;
        m_result = m_builder.textArea("area", m_model, m_state, options);
        return m_result.valid && acceptArea();
    }

    void useHost()noexcept{ m_builder.setEditHost(&m_host); }


protected:
    EditModel m_model;
    TextAreaState m_state;
    TextAreaHost m_host;
    EditBoxResult m_result;
};

struct AcceptedAreaFrame{
    Array<HitTarget, 16u> targets{};
    usize count = 0u;
    usize popupCount = 0u;
    u64 generation = 0u;
};

class TextAreaLoanFixture : public TextAreaFixture{
protected:
    [[nodiscard]] static TextAreaOptions AreaOptions()noexcept{
        TextAreaOptions options;
        options.height = { LayoutSizePolicy::Fixed, 96.0f };
        return options;
    }


protected:
    [[nodiscard]] bool acceptBaseline(){
        useHost();
        if(!m_model.setText("alpha\nbeta") || !frameArea(1u, AreaOptions()))
            return false;
        m_host.publications.clear();
        m_host.publishes = 0u;
        m_host.loans = 0u;
        return true;
    }

    [[nodiscard]] bool beginParent(const u64 generation){
        return begin(generation) && m_builder.beginPopup("parent", m_parent, UiWidgetTests::LargeLoanParentOptions());
    }

    [[nodiscard]] bool finishRoot(){ return m_context.endRoot() && m_context.finishFrame(); }

    [[nodiscard]] AcceptedAreaFrame accepted()const noexcept{
        AcceptedAreaFrame frame;
        frame.count = m_context.input().targets().size();
        frame.popupCount = m_context.input().popupCount();
        frame.generation = m_context.input().layoutGeneration();
        for(usize index = 0u; index < Min(frame.count, frame.targets.size()); ++index)
            frame.targets[index] = m_context.input().targets()[index];
        return frame;
    }

    void expectRejected(const AcceptedAreaFrame& frame){
        EXPECT_TRUE(m_builder.failed());
        EXPECT_TRUE(m_context.failed());
        EXPECT_FALSE(m_context.ready());
        EXPECT_FALSE(m_context.commitFrame(2u));
        EXPECT_EQ(m_context.input().layoutGeneration(), frame.generation);
        EXPECT_EQ(m_context.input().popupCount(), frame.popupCount);
        ASSERT_EQ(m_context.input().targets().size(), frame.count);
        ASSERT_LE(frame.count, frame.targets.size());
        for(usize index = 0u; index < frame.count; ++index){
            const HitTarget& before = frame.targets[index];
            const HitTarget& after = m_context.input().targets()[index];
            UiWidgetTests::ExpectHitTargetIdentity(after, before);
            EXPECT_EQ(after.ownerDeclarationGeneration, before.ownerDeclarationGeneration);
            EXPECT_EQ(after.popup, before.popup);
            UiComboTests::ExpectRect(after.rectangle, before.rectangle);
            UiComboTests::ExpectRect(after.clip, before.clip);
        }
    }


protected:
    PopupState m_parent;
    PopupState m_child;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

