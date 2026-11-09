// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/smoke/ui_layer/snapshots.adhd.h>
#include <impl/ecs_ui/toolkit/widgets/list.h>
#include <impl/ecs_ui/paint_context.h>
#include <impl/ecs_ui/toolkit/widgets/combo.h>
#include <core/alloc/general.h>
#include <core/input/module.h>
#include <impl/ecs_ui/toolkit/widgets/edit_box_state.h>
#include <impl/ecs_ui/toolkit/widgets/edit_box.h>
#include <impl/ecs_ui/toolkit/paint.h>
#include <engine_namespace.h>
#include <global/array.h>
#include <global/simd/vector_arithmetic.h>
#include <impl/ecs_ui/toolkit/widgets/search_combo.h>
#include <impl/ecs_ui/toolkit/widgets/context_menu.h>
#include <impl/ecs_ui/toolkit/edit/numeric_types.adhd.h>
#include <impl/ecs_ui/toolkit/edit/integer_model.h>
#include <impl/ecs_ui/toolkit/edit/float_model.h>
#include <impl/ecs_ui/toolkit/layout/validation.adhd.h>
#include <core/common/log.h>
#include <global/simdmath.h>
#include <impl/ecs_ui/toolkit/widgets/popup.h>
#include <impl/ecs_ui/toolkit/widgets/tooltip.h>
#include <loader/project_entry.h>
#include <core/graphics/runtime/render_pass.h>
#include <impl/ecs_ui/toolkit/widgets/radio_group.h>
#include <impl/ecs_ui/toolkit/widgets/slider.h>
#include <impl/ecs_ui/toolkit/widgets/text_area.h>
#include <impl/ecs_ui/toolkit/widgets/label.h>
#include <impl/ecs_ui/toolkit/images/image_loader.h>
#include <impl/ecs_ui/toolkit/widgets/window_style.adhd.h>
#include "../smoke_environment.h"
#include <global/hash_utils.h>
#include <impl/ecs_ui/toolkit/edit/text_normalization.adhd.h>
#include <global/simd/vector.h>
#include <impl/ecs_ui/toolkit/builder.h>
#include <global/simplemath.h>
#include <tests/unit/ui/arithmetic_fixture_rows.adhd.h>
#include <global/text_utils.h>
#include <global/assert.h>
#include <global/bit.h>
#include "../smoke_capture_factory.h"
#include "../framebuffer_capture.h"
#include <impl/ecs_ui/layer_system.h>
#include <core/graphics/backend_selection/backend.h>
#include <core/task/gpu/persistent_state.h>
#include <global/termination.h>
#include <tests/smoke/presentation_types.adhd.h>
#include <impl/assets_scene/asset.h>
#include <impl/assets_ui_skin/types.adhd.h>
#include <core/ecs/entity.h>
#include <core/graphics/runtime/runtime.h>
#include <impl/assets_graphics/bindless/runtime_abi.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Arithmetic row lookup keeps the fixture independent of total row count and allocates labels on demand.
class UiListSmokeSource final : public Impl::Ui::IListDataSource, NoCopy{
public:
    virtual u64 instanceGeneration()const noexcept override{ return 1u; }

    virtual u64 revision()const noexcept override{ return m_revision; }

    virtual u64 rowCount()const noexcept override{ return m_removed == 0u ? 100000u : 99999u; }
    virtual u64 key(u64 index)const noexcept override;
    virtual Expected<u64> indexOf(u64 keyValue)const noexcept override;
    virtual Expected<u64> findEnabled(u64 start, b8 reverse)const noexcept override;
    virtual StringView text(u64 index)const noexcept override;
    virtual b8 enabled(u64 index)const noexcept override;


public:
    void reverse()noexcept;
    void remove(u64 keyValue)noexcept;
    void beginFrame()noexcept{ m_labelReads = 0u; }

    [[nodiscard]] b8 reversed()const noexcept{ return m_reversed; }

    [[nodiscard]] u64 removedKey()const noexcept{ return m_removed; }

    [[nodiscard]] u32 labelReads()const noexcept{ return m_labelReads; }


private:
    u64 m_revision = 1u;
    u64 m_removed = 0u;
    b8 m_reversed = false;
    mutable Array<char, 32u> m_label{};
    mutable u32 m_labelReads = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// This wrapper changes source lifetime and emptiness without storing the 100000 arithmetic rows.
class UiComboSmokeSource final : public Impl::Ui::IListDataSource, NoCopy{
public:
    virtual u64 instanceGeneration()const noexcept override{ return m_generation; }

    virtual u64 revision()const noexcept override{ return m_revision + m_rows.revision(); }

    virtual u64 rowCount()const noexcept override{ return m_empty ? 0u : m_rows.rowCount(); }

    virtual u64 key(u64 index)const noexcept override{ return m_empty ? 0u : m_rows.key(index); }

    virtual Expected<u64> indexOf(u64 key)const noexcept override{ return m_empty ? MakeUnexpected(Failure{}) : m_rows.indexOf(key); }
    virtual Expected<u64> findEnabled(u64 start, b8 reverse)const noexcept override;
    virtual StringView text(u64 index)const noexcept override{ return m_empty ? StringView{} : m_rows.text(index); }

    virtual b8 enabled(u64 index)const noexcept override{ return !m_empty && m_rows.enabled(index); }


public:
    void reverse()noexcept{ m_rows.reverse(); }

    void remove(u64 key)noexcept{ m_rows.remove(key); }

    void beginFrame()noexcept{ m_rows.beginFrame(); }

    void toggleEmpty()noexcept;

    void replace()noexcept{ ++m_generation; }

    [[nodiscard]] b8 reversed()const noexcept{ return m_rows.reversed(); }

    [[nodiscard]] u64 removedKey()const noexcept{ return m_rows.removedKey(); }

    [[nodiscard]] u32 labelReads()const noexcept{ return m_rows.labelReads(); }


private:
    UiListSmokeSource m_rows;
    u64 m_generation = 1u;
    u64 m_revision = 1u;
    b8 m_empty = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiComboSmokeScene : public Core::IInputEventHandler, NoCopy{
public:
    explicit UiComboSmokeScene(Core::InputDispatcher& input);
    virtual ~UiComboSmokeScene()noexcept override;


public:
    [[nodiscard]] b8 paint(Impl::UiPaintContext& context);
    virtual ::NWB::Core::KeyboardInputResult keyboardUpdate(i32 key, i32 scancode, i32 action, i32 mods)noexcept override;


private:
    [[nodiscard]] Array<u64, 16u> values()const noexcept;
    [[nodiscard]] Impl::Ui::Rect cursorRow()const noexcept;
    void observeState();


private:
    Core::InputDispatcher& m_input;
    UiComboSmokeSource m_source;
    Impl::Ui::ComboState m_state;
    Impl::Ui::ScrollPlacement m_lastPlacement;
    Array<u64, 16u> m_lastValues{};
    Impl::Ui::PopupPlacement m_lastPopup;
    Impl::Ui::Rect m_lastBounds;
    Impl::Ui::DisplayMetrics m_lastDisplay;
    u32 m_sequence = 0u;
    u32 m_commits = 0u;
    u32 m_underlying = 0u;
    b8 m_focused = false;
    b8 m_visible = true;
    b8 m_enabled = true;
    b8 m_bottom = false;
    b8 m_displayChanged = false;
};

using SharedUiComboSmokeScene = RefCountPtr< RefCounter<UiComboSmokeScene>, ArenaRefDeleter<RefCounter<UiComboSmokeScene>, Core::Alloc::GlobalArena> >;

[[nodiscard]] b8 IsUiLayerComboSmokeEnabled();
[[nodiscard]] b8 IsUiLayerComboSkinSmokeEnabled();
[[nodiscard]] SharedUiComboSmokeScene CreateUiComboSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiEditSmokeScene : public Core::IInputEventHandler, NoCopy{
public:
    UiEditSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input);
    virtual ~UiEditSmokeScene()noexcept override;


public:
    [[nodiscard]] b8 paint(Impl::UiPaintContext& context);
    virtual ::NWB::Core::KeyboardInputResult keyboardUpdate(i32 key, i32 scancode, i32 action, i32 mods)override;


private:
    void resetModels();
    [[nodiscard]] Array<u32, 12u> values()const noexcept;
    void observeState(Impl::Ui::TextService& text);


private:
    Core::Alloc::GlobalArena& m_arena;
    Core::InputDispatcher& m_input;
    Impl::Ui::EditModel m_primary;
    Impl::Ui::EditModel m_secondary;
    Impl::Ui::EditBoxState m_primaryState;
    Impl::Ui::EditBoxState m_secondaryState;
    Impl::Ui::EditBoxPlacement m_lastPrimary;
    Impl::Ui::EditBoxPlacement m_lastSecondary;
    Impl::Ui::Rect m_lastPrimarySelection;
    Impl::Ui::Rect m_lastSecondarySelection;
    Impl::Ui::DisplayMetrics m_lastDisplay;
    Array<u32, 12u> m_lastValues{};
    u32 m_sequence = 0u;
    b8 m_primaryFocused = false;
    b8 m_secondaryFocused = false;
    b8 m_readOnly = false;
    b8 m_visible = true;
    b8 m_displayChanged = false;
};

using SharedUiEditSmokeScene = RefCountPtr< RefCounter<UiEditSmokeScene>, ArenaRefDeleter<RefCounter<UiEditSmokeScene>, Core::Alloc::GlobalArena> >;

[[nodiscard]] b8 IsUiLayerEditSmokeEnabled();
[[nodiscard]] SharedUiEditSmokeScene CreateUiEditSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Reconstruct a single-line fixture's selection from the canonical geometry for its framebuffer probe.
[[nodiscard]] Impl::Ui::Rect CaptureEditSelection(Core::Alloc::GlobalArena& arena, Impl::Ui::TextService& text, const Impl::Ui::EditModel& model, const Impl::Ui::EditBoxPlacement& placement, f32 fontSize);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Scene fixture for passive image region, size, and tint declarations.
class UiImageSmokeScene : public Core::IInputEventHandler, NoCopy{
public:
    explicit UiImageSmokeScene(Core::InputDispatcher& input);
    virtual ~UiImageSmokeScene()noexcept override;


public:
    [[nodiscard]] b8 paint(Impl::UiPaintContext& context);
    virtual ::NWB::Core::KeyboardInputResult keyboardUpdate(i32 key, i32 scancode, i32 action, i32 mods)noexcept override;


private:
    [[nodiscard]] b8 paintMain(Impl::Ui::Builder& ui, const Impl::Ui::Rect& bounds);
    [[nodiscard]] b8 paintFrozen(Impl::Ui::Builder& ui, const Impl::Ui::Rect& bounds);
    [[nodiscard]] b8 paintExternal(Impl::UiPaintContext& context, const Impl::Ui::Rect& bounds);
    [[nodiscard]] b8 paintPopups(Impl::Ui::Builder& ui, f32 right);
    void observeState(Impl::UiPaintContext& context);
    void paintMarkers(Impl::UiPaintContext& context)const;


private:
    Core::InputDispatcher& m_input;
    Impl::Ui::PopupState m_parent;
    Impl::Ui::PopupState m_child;
    UiImageSnapshot m_declared;
    UiImageSnapshot m_snapshot;
    u32 m_beforeClicks = 0u;
    u32 m_afterClicks = 0u;
    b8 m_phase = false;
};

using SharedUiImageSmokeScene = RefCountPtr< RefCounter<UiImageSmokeScene>, ArenaRefDeleter<RefCounter<UiImageSmokeScene>, Core::Alloc::GlobalArena> >;

[[nodiscard]] b8 IsUiLayerImageSmokeEnabled();
[[nodiscard]] b8 IsUiLayerImageSkinSmokeEnabled();
[[nodiscard]] SharedUiImageSmokeScene CreateUiImageSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Application-owned model markers let the native smoke verify controls without recognizing text or atlas pixels.
class UiInteractiveSmokeScene : NoCopy{
public:
    [[nodiscard]] b8 paint(Impl::UiPaintContext& context);


private:
    void observeDisplay(const Impl::Ui::DisplayMetrics& display);
    void logAction(TStringView action);
    void paintMarkers(Impl::UiPaintContext& context)const;


private:
    Impl::Ui::DisplayMetrics m_lastDisplay;
    u32 m_count = 0u;
    u32 m_actions = 0u;
    b8 m_enabled = true;
};

using SharedUiInteractiveSmokeScene = RefCountPtr< RefCounter<UiInteractiveSmokeScene>, ArenaRefDeleter<RefCounter<UiInteractiveSmokeScene>, Core::Alloc::GlobalArena> >;

[[nodiscard]] b8 IsUiLayerInteractionSmokeEnabled();
[[nodiscard]] SharedUiInteractiveSmokeScene CreateUiInteractiveSmokeScene(Core::Alloc::GlobalArena& arena);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiListSmokeScene : public Core::IInputEventHandler, NoCopy{
public:
    explicit UiListSmokeScene(Core::InputDispatcher& input);
    virtual ~UiListSmokeScene()noexcept override;


public:
    [[nodiscard]] b8 paint(Impl::UiPaintContext& context);
    virtual ::NWB::Core::KeyboardInputResult keyboardUpdate(i32 key, i32 scancode, i32 action, i32 mods)noexcept override;


private:
    [[nodiscard]] Array<u64, 12u> values()const noexcept;
    [[nodiscard]] Impl::Ui::Rect selectedRow()const noexcept;
    void observeState();


private:
    Core::InputDispatcher& m_input;
    UiListSmokeSource m_source;
    Impl::Ui::ListState m_state;
    Impl::Ui::ScrollPlacement m_lastPlacement;
    Impl::Ui::DisplayMetrics m_lastDisplay;
    Array<u64, 12u> m_lastValues{};
    u32 m_sequence = 0u;
    u32 m_commits = 0u;
    u32 m_underlying = 0u;
    b8 m_focused = false;
    b8 m_visible = true;
    b8 m_displayChanged = false;
};

using SharedUiListSmokeScene = RefCountPtr< RefCounter<UiListSmokeScene>, ArenaRefDeleter<RefCounter<UiListSmokeScene>, Core::Alloc::GlobalArena> >;

[[nodiscard]] b8 IsUiLayerListSmokeEnabled();
[[nodiscard]] b8 IsUiLayerListSkinSmokeEnabled();
[[nodiscard]] SharedUiListSmokeScene CreateUiListSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Admit only marker grids whose integer coordinates are exact in FP32.
template<usize Count, u32 Origin, u32 Step>
[[nodiscard]] inline Array<f32, Count> BuildSmokeMarkerPositions()noexcept{
    constexpr u32 s_ExactIntegerLimit = 1u << 24u;
    static_assert(Count > 0u && Step > 0u);
    static_assert(Origin <= s_ExactIntegerLimit && Step <= s_ExactIntegerLimit);
    static_assert(Count - 1u <= (s_ExactIntegerLimit - Origin) / Step);

    Array<f32, Count> positions{};
    usize index = 0u;
    for(; index < Count; index += 4u){
        // Repeating the last real index pads a partial packet without reading beyond the fixed grid or adding any floating effects.
        const SIMDVectorSingle coordinates = VectorMultiplyAddExpression(
            VectorSet(static_cast<f32>(index), static_cast<f32>(index + 1u < Count ? index + 1u : Count - 1u), static_cast<f32>(index + 2u < Count ? index + 2u : Count - 1u), static_cast<f32>(index + 3u < Count ? index + 3u : Count - 1u)),
            VectorReplicate(static_cast<f32>(Step)), VectorReplicate(static_cast<f32>(Origin))
        );
        positions[index] = VectorGetX(coordinates);
        if(index + 1u < Count)
            positions[index + 1u] = VectorGetY(coordinates);
        if(index + 2u < Count)
            positions[index + 2u] = VectorGetZ(coordinates);
        if(index + 3u < Count)
            positions[index + 3u] = VectorGetW(coordinates);
    }
    return positions;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Fixed five-key combo source with a filtered view.
class UiNestedPopupSmokeSource final : public Impl::Ui::ISearchableListDataSource, NoCopy{
private:
    class View final : public Impl::Ui::IListDataSource, NoCopy{
    public:
        explicit View(const UiNestedPopupSmokeSource& source)noexcept : m_source(source){}


    public:
        virtual u64 instanceGeneration()const noexcept override{ return 2u; }

        virtual u64 revision()const noexcept override{ return m_source.m_viewRevision; }

        virtual u64 rowCount()const noexcept override{ return m_source.m_count; }
        virtual u64 key(u64 index)const noexcept override;
        virtual Expected<u64> indexOf(u64 keyValue)const noexcept override;
        virtual Expected<u64> findEnabled(u64 start, b8 reverse)const noexcept override;
        virtual StringView text(u64 index)const noexcept override;
        virtual b8 enabled(u64 index)const noexcept override;


    private:
        const UiNestedPopupSmokeSource& m_source;
    };


public:
    explicit UiNestedPopupSmokeSource(Core::Alloc::GlobalArena& arena)noexcept;


public:
    virtual u64 instanceGeneration()const noexcept override{ return 1u; }

    virtual u64 revision()const noexcept override{ return 1u; }

    virtual u64 rowCount()const noexcept override{ return 5u; }

    virtual u64 key(u64 index)const noexcept override{ return index < rowCount() ? index + 1u : 0u; }
    virtual Expected<u64> indexOf(u64 keyValue)const noexcept override;
    virtual Expected<u64> findEnabled(u64 start, b8 reverse)const noexcept override;
    virtual StringView text(u64 index)const noexcept override;
    virtual b8 enabled(u64 index)const noexcept override{ return index < rowCount() && key(index) != 3u; }
    [[nodiscard]] virtual b8 filter(AStringView query)override;
    [[nodiscard]] virtual const Impl::Ui::IListDataSource& filtered()const noexcept override{ return m_view; }

    void beginFrame()noexcept{ m_labelReads = 0u; }

    [[nodiscard]] u32 labelReads()const noexcept{ return m_labelReads; }


private:
    AString<Core::Alloc::GlobalArena> m_query;
    View m_view;
    Array<u64, 5u> m_indices{};
    u64 m_count = 0u;
    u64 m_viewRevision = 0u;
    mutable u32 m_labelReads = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Every nested state is a member and remains lent through the outermost endPopup.
class UiNestedPopupSmokeScene : public Core::IInputEventHandler, NoCopy{
public:
    UiNestedPopupSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input);
    virtual ~UiNestedPopupSmokeScene()noexcept override;


public:
    [[nodiscard]] b8 paint(Impl::UiPaintContext& context);
    virtual ::NWB::Core::KeyboardInputResult keyboardUpdate(i32 key, i32 scancode, i32 action, i32 mods)noexcept override;


private:
    [[nodiscard]] b8 paintRoot(Impl::Ui::Builder& ui);
    [[nodiscard]] b8 paintParent(Impl::Ui::Builder& ui);
    [[nodiscard]] b8 paintChild(Impl::Ui::Builder& ui);
    // Copy accepted popup observations during the callback.
    void observeState(Impl::UiPaintContext& context);
    void paintMarkers(Impl::UiPaintContext& context)const;
    [[nodiscard]] Array<u64, 29u> values()const noexcept;
    [[nodiscard]] Impl::Ui::Rect rowBounds(const Impl::Ui::ListState& state, const Impl::Ui::IListDataSource& source, u64 key)const;


private:
    Core::InputDispatcher& m_input;
    UiNestedPopupSmokeSource m_source;
    Impl::Ui::PopupState m_parent;
    Impl::Ui::PopupState m_child;
    Impl::Ui::ListState m_beforeList;
    Impl::Ui::ListState m_afterList;
    Impl::Ui::ComboState m_combo;
    Impl::Ui::SearchComboState m_search;
    Impl::Ui::ContextMenuState m_menu;
    Impl::Ui::EditModel m_beforeText;
    Impl::Ui::EditModel m_afterText;
    Impl::Ui::EditModel m_childText;
    Impl::Ui::EditBoxState m_beforeEdit;
    Impl::Ui::EditBoxState m_afterEdit;
    Impl::Ui::EditBoxState m_childEdit;
    Impl::Ui::DisplayMetrics m_lastDisplay;
    Array<Impl::Ui::Rect, 26u> m_rectangles{};
    Array<Impl::Ui::Rect, 26u> m_lastRectangles{};
    Array<u64, 29u> m_lastValues{};
    u64 m_command = 0u;
    u32 m_sequence = 0u;
    u32 m_beforeClicks = 0u;
    u32 m_afterClicks = 0u;
    u32 m_childClicks = 0u;
    u32 m_outside = 0u;
    u32 m_focusScope = 0u;
    u32 m_focusCode = 0u;
    u32 m_popupTargets = 0u;
    u32 m_popupCount = 0u;
    b8 m_childParentScope = false;
    b8 m_childDeclared = false;
    b8 m_displayChanged = false;
};

using SharedUiNestedPopupSmokeScene = RefCountPtr< RefCounter<UiNestedPopupSmokeScene>, ArenaRefDeleter<RefCounter<UiNestedPopupSmokeScene>, Core::Alloc::GlobalArena> >;

[[nodiscard]] b8 IsUiLayerNestedPopupSmokeEnabled();
[[nodiscard]] b8 IsUiLayerNestedPopupSkinSmokeEnabled();
[[nodiscard]] SharedUiNestedPopupSmokeScene CreateUiNestedPopupSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiNumericEditSmokeScene : public Core::IInputEventHandler, NoCopy{
public:
    UiNumericEditSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input);
    virtual ~UiNumericEditSmokeScene()noexcept override;


public:
    [[nodiscard]] b8 paint(Impl::UiPaintContext& context);
    virtual ::NWB::Core::KeyboardInputResult keyboardUpdate(i32 key, i32 scancode, i32 action, i32 mods)override;


private:
    void resetModels();
    [[nodiscard]] Array<u64, 30u> values()const noexcept;
    void observeState(const Impl::Ui::DisplayMetrics& display, Impl::Ui::TextService& text);
    void paintMarkers(Impl::UiPaintContext& context)const;
    void count(const Impl::Ui::NumericEditResult& result)noexcept;
    [[nodiscard]] Impl::Ui::FloatBounds floatBounds()const noexcept;


private:
    Core::Alloc::GlobalArena& m_arena;
    Core::InputDispatcher& m_input;
    Impl::Ui::IntegerEditModel m_integer;
    Impl::Ui::FloatEditModel m_float;
    Impl::Ui::EditModel m_clipboard;
    Array<Impl::Ui::EditBoxState, 3u> m_states{};
    UiNumericEditSnapshot m_snapshot;
    u32 m_commits = 0u;
    u32 m_cancels = 0u;
    u32 m_rejects = 0u;
    u32 m_clamps = 0u;
    u32 m_restored = 0u;
    b8 m_enabled = true;
    b8 m_readOnly = false;
    b8 m_clamp = true;
};

using SharedUiNumericEditSmokeScene = RefCountPtr<RefCounter<UiNumericEditSmokeScene>, ArenaRefDeleter<RefCounter<UiNumericEditSmokeScene>, Core::Alloc::GlobalArena>>;

[[nodiscard]] b8 IsUiLayerNumericEditSmokeEnabled();
[[nodiscard]] b8 IsUiLayerNumericEditSkinSmokeEnabled();
[[nodiscard]] SharedUiNumericEditSmokeScene CreateUiNumericEditSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void PaintUiLayerSmokeScene(Impl::UiPaintContext& context);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename Source>
[[nodiscard]] Expected<u64> FindAdjacentEnabledRow(const Source& source, const u64 start, const b8 reverse)noexcept(
    noexcept(static_cast<b8>(start >= source.rowCount()))
    && noexcept(static_cast<b8>(start + 1u >= source.rowCount()))
    && noexcept(static_cast<b8>(source.enabled(start)))
    && noexcept(static_cast<b8>(source.enabled(DeclVal<u64&>())))
){
    u64 index = 0u;
    if(start >= source.rowCount())
        return MakeUnexpected(Failure{});
    if(source.enabled(start)){
        index = start;
        return index;
    }
    if(reverse){
        if(start == 0u)
            return MakeUnexpected(Failure{});
        index = start - 1u;
    }
    else{
        if(start + 1u >= source.rowCount())
            return MakeUnexpected(Failure{});
        index = start + 1u;
    }
    return source.enabled(index) ? Expected<u64>{ index } : MakeUnexpected(Failure{});
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Impl::Ui::Color EncodeSmokeColor(u64 value)noexcept;

[[nodiscard]] inline Impl::Ui::Color EncodeSmokeColor(const u32 value)noexcept{ return EncodeSmokeColor(static_cast<u64>(value)); }

[[nodiscard]] inline Impl::Ui::Color EncodeSmokeColor(const f32 value)noexcept{ return EncodeSmokeColor(static_cast<u64>(Max(0.0f, value) + 0.5f)); }


// Compute expected row height from the accepted content extent.
[[nodiscard]] Expected<Impl::Ui::Rect> SmokeListRowBounds(const Impl::Ui::ScrollPlacement& placement, u64 index)noexcept;

[[nodiscard]] Impl::Ui::Rect VisibleSmokeListRow(const Impl::Ui::ScrollPlacement& placement, u64 index)noexcept;

void LogSmokeRect(TStringView tag, u32 sequence, TStringView name, const Impl::Ui::Rect& rectangle);

void ObserveSmokeDisplay(TStringView tag, const Impl::Ui::DisplayMetrics& display, Impl::Ui::DisplayMetrics& inOutLastDisplay, b8& inOutDisplayChanged);

template<typename ValuesT>
inline void PaintSingleSmokeValueMarkers(const ValuesT& values, const u32 sequence, Impl::UiPaintContext& context){
    const auto positions = BuildSmokeMarkerPositions<TupleSize_V<ValuesT> + 1u, 12u, 20u>();
    const f32 y = context.display.logicalHeight - 20.0f;
    for(usize index = 0u; index <= values.size(); ++index){
        const u32 value = index == values.size() ? sequence : values[index];
        context.paint.fillRect({ positions[index], y, 14.0f, 12.0f }, EncodeSmokeColor(value));
    }
}

template<u32 Step, u32 Width, u32 Height = 12u, u32 BottomOffset = 20u, typename ValuesT>
inline void PaintKeyedSmokeValueMarkers(const ValuesT& values, const u64 sequence, Impl::UiPaintContext& context){
    const auto positions = BuildSmokeMarkerPositions<(TupleSize_V<ValuesT> + 1u) * 2u, 12u, Step>();
    const f32 y = context.display.logicalHeight - static_cast<f32>(BottomOffset);
    for(usize index = 0u; index <= values.size(); ++index){
        const u64 value = index == values.size() ? sequence : values[index];
        for(u32 part = 0u; part < 2u; ++part){
            const usize marker = index * 2u + part;
            context.paint.fillRect({ positions[marker], y, static_cast<f32>(Width), static_cast<f32>(Height) }, EncodeSmokeColor(value >> (part * 12u)));
        }
    }
}

// Progress, slider, and image snapshots paint one 12-bit marker swatch per value part plus sequence swatches along the bottom edge.
template<typename ValuesT, typename BitsT>
inline void PaintSmokeValueMarkers(const ValuesT& values, const BitsT& bits, const u64 sequence, Impl::UiPaintContext& context){
    const f32 y = context.display.logicalHeight - 18.0f;
    const auto positions = BuildSmokeMarkerPositions<TupleSize_V<ValuesT> * 2u + TupleSize_V<BitsT> * 6u + 2u, 12u, 12u>();
    usize marker = 0u;
    for(const u64 value : values){
        for(u32 part = 0u; part < 2u; ++part){
            context.paint.fillRect({ positions[marker++], y, 8.0f, 8.0f }, EncodeSmokeColor(value >> (part * 12u)));
        }
    }
    for(const u64 bit : bits){
        for(u32 part = 0u; part < 6u; ++part){
            context.paint.fillRect({ positions[marker++], y, 8.0f, 8.0f }, EncodeSmokeColor(bit >> (part * 12u)));
        }
    }
    for(u32 part = 0u; part < 2u; ++part){
        context.paint.fillRect({ positions[marker++], y, 8.0f, 8.0f }, EncodeSmokeColor(sequence >> (part * 12u)));
    }
}

[[nodiscard]] inline b8 SameSmokePlacement(const Impl::Ui::EditBoxPlacement& left, const Impl::Ui::EditBoxPlacement& right)noexcept{
    return SameSmokeRect(left.bounds, right.bounds) && SameSmokeRect(left.content, right.content) && SameSmokeRect(left.caret, right.caret) && left.scroll == right.scroll;
}

void LogSmokeEditGeometry(TStringView tag, u32 sequence, TStringView field, const Impl::Ui::EditBoxPlacement& placement, const Impl::Ui::Rect& selection);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



template<typename RectanglesT, typename IncludeIndexT>
[[nodiscard]] inline PassiveSmokeTargetObservation ObservePassiveSmokeTargets(
    const Impl::Ui::InputRouter& input,
    const u64 parentGeneration,
    const u64 childGeneration,
    const usize beginIndex,
    const usize endIndex,
    const IncludeIndexT& includeIndex,
    RectanglesT& rectangles
)noexcept(
    noexcept(rectangles[DeclVal<u32&>()] = DeclVal<const Impl::Ui::Rect&>())
    && noexcept(static_cast<b8>(!includeIndex(DeclVal<usize&>()) || rectangles[DeclVal<usize&>()].width == 0.0f))
    && noexcept(SameSmokeRect(DeclVal<const Impl::Ui::Rect&>(), rectangles[DeclVal<usize&>()]))
){
    PassiveSmokeTargetObservation observation;
    u32 rootButton = 0u;
    for(const Impl::Ui::HitTarget& target : input.targets()){
        u32 code = 0u;
        if(target.activatable && !target.popup.valid() && rootButton < 2u){
            rectangles[rootButton] = target.rectangle;
            code = rootButton + 1u;
            ++rootButton;
        }
        else if(target.activatable && target.popup.instanceGeneration == parentGeneration)
            code = 3u;
        else if(target.activatable && target.popup.instanceGeneration == childGeneration)
            code = 4u;
        if(target.id == input.focus())
            observation.focusCode = code;
        for(usize index = beginIndex; index < endIndex; ++index){
            if(!includeIndex(index) || rectangles[index].width == 0.0f)
                continue;
            if(SameSmokeRect(target.rectangle, rectangles[index]))
                ++observation.passiveTargets;
        }
    }
    return observation;
}

template<typename SnapshotT, typename StateChangedT>
[[nodiscard]] inline PassiveSmokeSnapshotUpdate PublishPassiveSmokeSnapshot(const StateChangedT& stateChanged, SnapshotT& current, SnapshotT& snapshot)noexcept(
    noexcept(DeclVal<void(*)(const Impl::Ui::DisplayMetrics&)noexcept>()(snapshot.display))
    && noexcept(DeclVal<void(*)(b8)noexcept>()(
        current.display.logicalWidth != DeclVal<const Impl::Ui::DisplayMetrics&>().logicalWidth
        || current.display.logicalHeight != DeclVal<const Impl::Ui::DisplayMetrics&>().logicalHeight
        || current.display.pixelScaleX != DeclVal<const Impl::Ui::DisplayMetrics&>().pixelScaleX
        || current.display.pixelScaleY != DeclVal<const Impl::Ui::DisplayMetrics&>().pixelScaleY
    ))
    && noexcept(DeclVal<void(*)(b8)noexcept>()(stateChanged()))
    && noexcept(static_cast<b8>(DeclVal<usize&>() < current.rectangles.size()))
    && noexcept(SameSmokeRect(current.rectangles[DeclVal<usize&>()], snapshot.rectangles[DeclVal<usize&>()]))
    && noexcept(static_cast<b8>(snapshot.sequence != 0u && !b8{} && !b8{}))
    && noexcept(current.sequence = snapshot.sequence + 1u)
    && noexcept(snapshot = current)
){
    const Impl::Ui::DisplayMetrics& previous = snapshot.display;
    const b8 displayChanged = current.display.logicalWidth != previous.logicalWidth || current.display.logicalHeight != previous.logicalHeight || current.display.pixelScaleX != previous.pixelScaleX || current.display.pixelScaleY != previous.pixelScaleY;
    b8 changed = stateChanged();
    for(usize index = 0u; index < current.rectangles.size(); ++index)
        changed |= !SameSmokeRect(current.rectangles[index], snapshot.rectangles[index]);
    if(snapshot.sequence != 0u && !changed && !displayChanged)
        return {};
    current.sequence = snapshot.sequence + 1u;
    snapshot = current;
    return { .published = true, .displayChanged = displayChanged };
}

[[nodiscard]] b8 HandlePassiveSmokeKeyboard(i32 key, i32 action, b8& phase, Impl::Ui::PopupState& parent, Impl::Ui::PopupState& child)noexcept;

template<typename ValuesT>
inline void PaintPassiveSmokeValueMarkers(const ValuesT& values, const u64 sequence, Impl::UiPaintContext& context){
    PaintKeyedSmokeValueMarkers<12u, 8u, 8u, 18u>(values, sequence, context);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiPopupSmokeScene : public Core::IInputEventHandler, NoCopy{
public:
    UiPopupSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input);
    virtual ~UiPopupSmokeScene()noexcept override;


public:
    [[nodiscard]] b8 paint(Impl::UiPaintContext& context);
    [[nodiscard]] b8 paintLaterRoot(Impl::UiPaintContext& context)const;
    virtual ::NWB::Core::KeyboardInputResult keyboardUpdate(i32 key, i32 scancode, i32 action, i32 mods)noexcept override;


private:
    [[nodiscard]] Array<u32, 12u> values()const noexcept;
    void observeState();


private:
    Core::InputDispatcher& m_input;
    Impl::Ui::PopupState m_popup;
    Impl::Ui::EditModel m_popupText;
    Impl::Ui::EditModel m_outsideText;
    Impl::Ui::EditBoxState m_popupEdit;
    Impl::Ui::EditBoxState m_outsideEdit;
    Impl::Ui::PopupPlacement m_lastPlacement;
    Impl::Ui::DisplayMetrics m_lastDisplay;
    Array<u32, 12u> m_lastValues{};
    u32 m_sequence = 0u;
    u32 m_underlying = 0u;
    u32 m_selected = 0u;
    b8 m_modal = false;
    b8 m_checked = false;
    b8 m_popupFocused = false;
    b8 m_outsideFocused = false;
    b8 m_edge = false;
    b8 m_right = false;
    b8 m_visible = true;
    b8 m_displayChanged = false;
};

using SharedUiPopupSmokeScene = RefCountPtr< RefCounter<UiPopupSmokeScene>, ArenaRefDeleter<RefCounter<UiPopupSmokeScene>, Core::Alloc::GlobalArena> >;

[[nodiscard]] b8 IsUiLayerPopupSmokeEnabled();
[[nodiscard]] b8 IsUiLayerPopupSkinSmokeEnabled();
[[nodiscard]] SharedUiPopupSmokeScene CreateUiPopupSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiPopupToolsSmokeSource final : public Impl::Ui::IListDataSource, NoCopy{
public:
    virtual u64 instanceGeneration()const noexcept override{ return m_generation; }

    virtual u64 revision()const noexcept override{ return m_revision; }

    virtual u64 rowCount()const noexcept override{ return 5u; }
    virtual u64 key(u64 index)const noexcept override;
    virtual Expected<u64> indexOf(u64 keyValue)const noexcept override;
    virtual Expected<u64> findEnabled(u64 start, b8 reverse)const noexcept override;
    virtual StringView text(u64 index)const noexcept override;
    virtual b8 enabled(u64 index)const noexcept override{ return index < rowCount() && key(index) != 3u; }


public:
    void reverse()noexcept;

    void replace()noexcept;

    void beginFrame()noexcept{ m_labelReads = 0u; }

    [[nodiscard]] b8 reversed()const noexcept{ return m_reversed; }

    [[nodiscard]] u32 labelReads()const noexcept{ return m_labelReads; }


private:
    u64 m_generation = 1u;
    u64 m_revision = 1u;
    b8 m_reversed = false;
    mutable u32 m_labelReads = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiPopupToolsSmokeScene : public Core::IInputEventHandler, NoCopy{
public:
    UiPopupToolsSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input);
    virtual ~UiPopupToolsSmokeScene()noexcept override;


public:
    [[nodiscard]] b8 paint(Impl::UiPaintContext& context);
    virtual ::NWB::Core::KeyboardInputResult keyboardUpdate(i32 key, i32 scancode, i32 action, i32 mods)noexcept override;


private:
    [[nodiscard]] Array<u64, 15u> values()const noexcept;
    [[nodiscard]] Impl::Ui::Rect rowBounds(u64 key)const noexcept;
    void observeState();


private:
    Core::InputDispatcher& m_input;
    UiPopupToolsSmokeSource m_source;
    Impl::Ui::ContextMenuState m_menu;
    Impl::Ui::TooltipState m_tooltip;
    Impl::Ui::EditModel m_sentinel;
    Impl::Ui::EditBoxState m_sentinelState;
    Impl::Ui::ScrollPlacement m_lastList;
    Impl::Ui::PopupPlacement m_lastMenu;
    Impl::Ui::PopupPlacement m_lastTooltip;
    Impl::Ui::Rect m_lastSentinel;
    Impl::Ui::DisplayMetrics m_lastDisplay;
    Array<u64, 15u> m_lastValues{};
    u64 m_command = 0u;
    u32 m_sequence = 0u;
    u32 m_commits = 0u;
    u32 m_anchorClicks = 0u;
    u32 m_underlying = 0u;
    b8 m_sentinelFocused = false;
    b8 m_enabled = true;
    b8 m_displayChanged = false;
};

using SharedUiPopupToolsSmokeScene = RefCountPtr< RefCounter<UiPopupToolsSmokeScene>, ArenaRefDeleter<RefCounter<UiPopupToolsSmokeScene>, Core::Alloc::GlobalArena> >;

[[nodiscard]] b8 IsUiLayerPopupToolsSmokeEnabled();
[[nodiscard]] b8 IsUiLayerPopupToolsSkinSmokeEnabled();
[[nodiscard]] SharedUiPopupToolsSmokeScene CreateUiPopupToolsSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Scene fixture for passive progress bar declarations.
class UiProgressSmokeScene : public Core::IInputEventHandler, NoCopy{
public:
    explicit UiProgressSmokeScene(Core::InputDispatcher& input);
    virtual ~UiProgressSmokeScene()noexcept override;


public:
    [[nodiscard]] b8 paint(Impl::UiPaintContext& context);
    virtual ::NWB::Core::KeyboardInputResult keyboardUpdate(i32 key, i32 scancode, i32 action, i32 mods)noexcept override;


private:
    [[nodiscard]] b8 paintMain(Impl::Ui::Builder& ui, const Impl::Ui::Rect& bounds);
    [[nodiscard]] b8 paintFrozen(Impl::Ui::Builder& ui, const Impl::Ui::Rect& bounds);
    [[nodiscard]] b8 paintExternal(Impl::UiPaintContext& context, const Impl::Ui::Rect& bounds);
    [[nodiscard]] b8 paintPopups(Impl::Ui::Builder& ui, f32 right);
    void observeState(Impl::UiPaintContext& context);
    void paintMarkers(Impl::UiPaintContext& context)const;


private:
    Core::InputDispatcher& m_input;
    Impl::Ui::PopupState m_parent;
    Impl::Ui::PopupState m_child;
    UiProgressSnapshot m_declared;
    UiProgressSnapshot m_snapshot;
    u32 m_beforeClicks = 0u;
    u32 m_afterClicks = 0u;
    b8 m_phase = false;
};

using SharedUiProgressSmokeScene = RefCountPtr< RefCounter<UiProgressSmokeScene>, ArenaRefDeleter<RefCounter<UiProgressSmokeScene>, Core::Alloc::GlobalArena> >;

[[nodiscard]] b8 IsUiLayerProgressSmokeEnabled();
[[nodiscard]] b8 IsUiLayerProgressSkinSmokeEnabled();
[[nodiscard]] SharedUiProgressSmokeScene CreateUiProgressSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Expected<NotNullUniquePtr<Core::ECS::World>> CreateUiLayerSmokeWorld(ProjectRuntimeContext& context);
void ClearUiLayerSmokeWorld(ProjectRuntimeContext& context, Core::ECS::World& world);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class FramebufferCapture;

class UiLayerResizeRenderPass final : public Core::IRenderPass{
public:
    using Core::IRenderPass::IRenderPass;
    virtual b8 shouldRenderUnfocused()noexcept override{ return true; }
};

class UiLayerSmokeProject final : public IProjectEntryCallbacks{
public:
    explicit UiLayerSmokeProject(NotNullUniquePtr<Core::ECS::World> world, ProjectRuntimeContext& context)noexcept;
    virtual ~UiLayerSmokeProject()noexcept override;


public:
    virtual b8 onStartup()override;
    virtual void onShutdown()override;
    virtual b8 onUpdate(f32 delta)override;


private:
    void destroyRuntime();


private:
    NotNullUniquePtr<Core::ECS::World> m_world;
    UniquePtr<FramebufferCapture> m_framebufferCapture;
    ProjectRuntimeContext& m_context;
    UiLayerResizeRenderPass m_resizeRenderPass;
    b8 m_resizeRenderPassRegistered = false;
    b8 m_resizePresentationReported = false;
    u64 m_resizePresentationBaseline = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiRadioGroupSmokeSource final : public Impl::Ui::IListDataSource, NoCopy{
public:
    [[nodiscard]] virtual u64 instanceGeneration()const noexcept override;
    [[nodiscard]] virtual u64 revision()const noexcept override;
    [[nodiscard]] virtual u64 rowCount()const noexcept override;
    [[nodiscard]] virtual u64 key(u64 index)const noexcept override;
    [[nodiscard]] virtual Expected<u64> indexOf(u64 keyValue)const noexcept override;
    [[nodiscard]] virtual Expected<u64> findEnabled(u64 start, b8 reverseValue)const noexcept override;
    [[nodiscard]] virtual StringView text(u64 index)const noexcept override;
    [[nodiscard]] virtual b8 enabled(u64 index)const noexcept override;


public:
    void reverse()noexcept;
    void remove(u64 keyValue)noexcept;
    void replace()noexcept;
    void beginFrame()noexcept{ m_labelReads = 0u; }

    [[nodiscard]] b8 reversed()const noexcept{ return m_reversed; }

    [[nodiscard]] u64 removedKey()const noexcept{ return m_removed; }

    [[nodiscard]] u32 labelReads()const noexcept{ return m_labelReads; }


private:
    [[nodiscard]] u64 rawKey(u64 index)const noexcept;
    void invalidateText()const noexcept;


private:
    u64 m_generation = 1701u;
    u64 m_revision = 1u;
    u64 m_removed = 0u;
    b8 m_reversed = false;
    mutable Array<char, 32u> m_label{};
    mutable u32 m_labelReads = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Scene fixture owning radio group sources and states across Builder loans.
class UiRadioGroupSmokeScene : public Core::IInputEventHandler, NoCopy{
public:
    explicit UiRadioGroupSmokeScene(Core::InputDispatcher& input);
    virtual ~UiRadioGroupSmokeScene()noexcept override;


public:
    [[nodiscard]] b8 paint(Impl::UiPaintContext& context);
    virtual ::NWB::Core::KeyboardInputResult keyboardUpdate(i32 key, i32 scancode, i32 action, i32 mods)noexcept override;


private:
    [[nodiscard]] b8 paintMain(Impl::Ui::Builder& ui);
    [[nodiscard]] b8 paintDisabled(Impl::Ui::Builder& ui);
    [[nodiscard]] b8 paintPopup(Impl::Ui::Builder& ui);
    void observeState(Impl::UiPaintContext& context);
    void paintMarkers(Impl::UiPaintContext& context)const;


private:
    Core::InputDispatcher& m_input;
    UiRadioGroupSmokeSource m_source;
    Impl::Ui::RadioGroupState m_state;
    Impl::Ui::RadioGroupState m_disabled;
    Impl::Ui::RadioGroupState m_popupRadio;
    Impl::Ui::PopupState m_parent;
    UiRadioGroupSnapshot m_snapshot;
    u32 m_changes = 0u;
    u32 m_activations = 0u;
    u32 m_beforeClicks = 0u;
    u32 m_afterClicks = 0u;
    u32 m_popupChanges = 0u;
    u32 m_popupActivations = 0u;
    b8 m_enabled = true;
};

using SharedUiRadioGroupSmokeScene = RefCountPtr< RefCounter<UiRadioGroupSmokeScene>, ArenaRefDeleter<RefCounter<UiRadioGroupSmokeScene>, Core::Alloc::GlobalArena> >;

[[nodiscard]] b8 IsUiLayerRadioGroupSmokeEnabled();
[[nodiscard]] b8 IsUiLayerRadioGroupSkinSmokeEnabled();
[[nodiscard]] SharedUiRadioGroupSmokeScene CreateUiRadioGroupSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// A numeric prefix is a maximum of six disjoint decimal intervals, independent of the 100000-row count.
class UiSearchComboSmokeSource final : public Impl::Ui::ISearchableListDataSource, NoCopy{
private:
    using Range = UiSearchComboRange;

    class View final : public Impl::Ui::IListDataSource, NoCopy{
    public:
        explicit View(const UiSearchComboSmokeSource& source)noexcept : m_source(source){}


    public:
        virtual u64 instanceGeneration()const noexcept override{ return 2u; }

        virtual u64 revision()const noexcept override{ return m_source.m_viewRevision; }

        virtual u64 rowCount()const noexcept override{ return m_source.m_filteredCount; }
        virtual u64 key(u64 index)const noexcept override;
        virtual Expected<u64> indexOf(u64 key)const noexcept override;
        virtual Expected<u64> findEnabled(u64 start, b8 reverse)const noexcept override;
        virtual StringView text(u64 index)const noexcept override;
        virtual b8 enabled(u64 index)const noexcept override;


    private:
        const UiSearchComboSmokeSource& m_source;
    };


public:
    explicit UiSearchComboSmokeSource(Core::Alloc::GlobalArena& arena)noexcept;


public:
    virtual u64 instanceGeneration()const noexcept override{ return m_rows.instanceGeneration(); }

    virtual u64 revision()const noexcept override{ return m_rows.revision(); }

    virtual u64 rowCount()const noexcept override{ return m_rows.rowCount(); }

    virtual u64 key(u64 index)const noexcept override{ return m_rows.key(index); }

    virtual Expected<u64> indexOf(u64 key)const noexcept override{ return m_rows.indexOf(key); }
    virtual Expected<u64> findEnabled(u64 start, b8 reverse)const noexcept override;
    virtual StringView text(u64 index)const noexcept override{ return m_rows.text(index); }

    virtual b8 enabled(u64 index)const noexcept override{ return m_rows.enabled(index); }
    [[nodiscard]] virtual b8 filter(AStringView query)override;
    [[nodiscard]] virtual const Impl::Ui::IListDataSource& filtered()const noexcept override{ return m_view; }


public:
    void reverse()noexcept{ m_rows.reverse(); }

    void remove(u64 key)noexcept{ m_rows.remove(key); }

    void beginFrame()noexcept{ m_rows.beginFrame(); }

    [[nodiscard]] b8 reversed()const noexcept{ return m_rows.reversed(); }

    [[nodiscard]] u64 removedKey()const noexcept{ return m_rows.removedKey(); }

    [[nodiscard]] u32 labelReads()const noexcept{ return m_rows.labelReads(); }


private:
    void addRange(u64 first, u64 past)noexcept;


private:
    UiListSmokeSource m_rows;
    AString<Core::Alloc::GlobalArena> m_query;
    View m_view;
    Array<Range, 6u> m_ranges{};
    usize m_rangeCount = 0u;
    u64 m_filteredCount = 0u;
    u64 m_viewRevision = 0u;
    u64 m_cachedSourceRevision = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiSearchComboSmokeScene : public Core::IInputEventHandler, NoCopy{
public:
    UiSearchComboSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input);
    virtual ~UiSearchComboSmokeScene()noexcept override;


public:
    [[nodiscard]] b8 paint(Impl::UiPaintContext& context);
    virtual ::NWB::Core::KeyboardInputResult keyboardUpdate(i32 key, i32 scancode, i32 action, i32 mods)override;


private:
    [[nodiscard]] Array<u64, 22u> values()const noexcept;
    [[nodiscard]] Impl::Ui::Rect cursorRow()const noexcept;
    void observeState(Impl::Ui::TextService& text);


private:
    Core::Alloc::GlobalArena& m_arena;
    Core::InputDispatcher& m_input;
    UiSearchComboSmokeSource m_source;
    Impl::Ui::SearchComboState m_state;
    Impl::Ui::ScrollPlacement m_lastPlacement;
    Impl::Ui::PopupPlacement m_lastPopup;
    Impl::Ui::Rect m_lastBounds;
    Impl::Ui::Rect m_lastQueryBounds;
    Impl::Ui::DisplayMetrics m_lastDisplay;
    Array<u64, 22u> m_lastValues{};
    u32 m_sequence = 0u;
    u32 m_commits = 0u;
    u32 m_underlying = 0u;
    b8 m_focused = false;
    b8 m_enabled = true;
    b8 m_displayChanged = false;
};

using SharedUiSearchComboSmokeScene = RefCountPtr< RefCounter<UiSearchComboSmokeScene>, ArenaRefDeleter<RefCounter<UiSearchComboSmokeScene>, Core::Alloc::GlobalArena> >;

[[nodiscard]] b8 IsUiLayerSearchComboSmokeEnabled();
[[nodiscard]] b8 IsUiLayerSearchComboSkinSmokeEnabled();
[[nodiscard]] SharedUiSearchComboSmokeScene CreateUiSearchComboSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Scene fixture owning slider values across enclosing Builder scopes.
class UiSliderSmokeScene : public Core::IInputEventHandler, NoCopy{
public:
    explicit UiSliderSmokeScene(Core::InputDispatcher& input);
    virtual ~UiSliderSmokeScene()noexcept override;


public:
    [[nodiscard]] b8 paint(Impl::UiPaintContext& context);
    virtual ::NWB::Core::KeyboardInputResult keyboardUpdate(i32 key, i32 scancode, i32 action, i32 mods)noexcept override;


private:
    [[nodiscard]] b8 paintMain(Impl::Ui::Builder& ui, const Impl::Ui::Rect& bounds);
    [[nodiscard]] b8 paintOther(Impl::Ui::Builder& ui, const Impl::Ui::Rect& bounds);
    [[nodiscard]] b8 paintPopup(Impl::Ui::Builder& ui, f32 right);
    void observeState(Impl::UiPaintContext& context);
    void paintMarkers(Impl::UiPaintContext& context)const;


private:
    Core::InputDispatcher& m_input;
    Impl::Ui::SliderState m_state;
    Impl::Ui::SliderState m_disabled;
    Impl::Ui::SliderState m_constant;
    Impl::Ui::SliderState m_popupSlider;
    Impl::Ui::PopupState m_parent;
    UiSliderSnapshot m_snapshot;
    u32 m_changes = 0u;
    u32 m_popupChanges = 0u;
    u32 m_beforeClicks = 0u;
    u32 m_afterClicks = 0u;
    u32 m_externalIntents = 0u;
    b8 m_enabled = true;
    b8 m_narrowRange = false;
    b8 m_coarseStep = false;
};

using SharedUiSliderSmokeScene = RefCountPtr< RefCounter<UiSliderSmokeScene>, ArenaRefDeleter<RefCounter<UiSliderSmokeScene>, Core::Alloc::GlobalArena> >;

[[nodiscard]] b8 IsUiLayerSliderSmokeEnabled();
[[nodiscard]] b8 IsUiLayerSliderSkinSmokeEnabled();
[[nodiscard]] SharedUiSliderSmokeScene CreateUiSliderSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiTextAreaSmokeScene : NoCopy{
public:
    UiTextAreaSmokeScene(Core::Alloc::GlobalArena& arena, Core::IClipboardService& clipboard);
    ~UiTextAreaSmokeScene()noexcept;


public:
    [[nodiscard]] b8 paint(Impl::UiPaintContext& context);


private:
    void resetModel();
    void longDocument();
    [[nodiscard]] b8 seedClipboard();
    [[nodiscard]] b8 drainClipboard();
    [[nodiscard]] b8 observeState(Impl::UiPaintContext& context);
    void paintMarkers(Impl::UiPaintContext& context)const;


private:
    Core::IClipboardService& m_clipboard;
    AString<Core::Alloc::GlobalArena> m_longText;
    Impl::Ui::EditModel m_model;
    Impl::Ui::TextAreaState m_state;
    Impl::Ui::EditBoxView m_observed;
    Core::ClipboardCompletion m_completion;
    Core::ClipboardRequestToken m_clipboardToken;
    UiTextAreaSnapshot m_snapshot;
    u32 m_submits = 0u;
    u32 m_cancels = 0u;
    u32 m_blurs = 0u;
    u32 m_abandons = 0u;
    u32 m_outside = 0u;
    u32 m_clipboardSeeds = 0u;
    b8 m_enabled = true;
    b8 m_readOnly = false;
    b8 m_compact = false;
    b8 m_longDocument = false;
};

using SharedUiTextAreaSmokeScene = RefCountPtr< RefCounter<UiTextAreaSmokeScene>, ArenaRefDeleter<RefCounter<UiTextAreaSmokeScene>, Core::Alloc::GlobalArena> >;

[[nodiscard]] SharedUiTextAreaSmokeScene CreateUiTextAreaSmokeScene(Core::Alloc::GlobalArena& arena, Core::IClipboardService& clipboard);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiTextSmokeSamples : NoCopy{
public:
    explicit UiTextSmokeSamples(Core::Alloc::GlobalArena& arena);


public:
    [[nodiscard]] b8 paint(Impl::UiPaintContext& context);


private:
    Impl::Ui::Label m_latin;
    Impl::Ui::Label m_korean;
    Impl::Ui::Label m_clipped;
    Impl::Ui::SharedGlyphPage m_coverage;
    Array<Impl::Ui::SharedSdfAtlasPage, 4u> m_sdf;
};

using SharedUiTextSmokeSamples = RefCountPtr< RefCounter<UiTextSmokeSamples>, ArenaRefDeleter<RefCounter<UiTextSmokeSamples>, Core::Alloc::GlobalArena> >;

[[nodiscard]] SharedUiTextSmokeSamples CreateUiTextSmokeSamples(Core::Alloc::GlobalArena& arena);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Scene fixture for primitive and Builder texture image declarations.
class UiTextureImageSmokeScene : public Core::IInputEventHandler, NoCopy{
public:
    UiTextureImageSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input, Core::Assets::AssetManager& assets);
    virtual ~UiTextureImageSmokeScene()noexcept override;


public:
    [[nodiscard]] b8 paint(Impl::UiPaintContext& context);
    [[nodiscard]] virtual ::NWB::Core::KeyboardInputResult keyboardUpdate(i32 key, i32 scancode, i32 action, i32 mods)override;


private:
    [[nodiscard]] b8 replaceVersion();
    [[nodiscard]] b8 paintControls(Impl::Ui::Builder& ui, const Impl::Ui::Rect& bounds);
    [[nodiscard]] b8 paintImages(Impl::UiPaintContext& context, f32 width, f32 right, f32 otherWidth);
    [[nodiscard]] b8 paintBuilderImages(Impl::Ui::Builder& ui, const Impl::Ui::Rect& bounds);
    [[nodiscard]] b8 paintPopups(Impl::UiPaintContext& context, f32 right);
    void observeState(Impl::UiPaintContext& context);
    void paintMarkers(Impl::UiPaintContext& context)const;


private:
    Core::Alloc::GlobalArena& m_arena;
    Core::InputDispatcher& m_input;
    Impl::Ui::SharedImageSource m_default;
    Impl::Ui::SharedImageSource m_alternate;
    Impl::Ui::SharedImageSource m_replacement;
    Impl::Ui::PopupState m_parent;
    Impl::Ui::PopupState m_child;
    UiTextureImageSnapshot m_declared;
    UiTextureImageSnapshot m_snapshot;
    u32 m_beforeClicks = 0u;
    u32 m_afterClicks = 0u;
    u32 m_replacementCount = 0u;
    u32 m_evictionRemaining = 0u;
    u32 m_evictionCompleted = 0u;
    b8 m_phase = false;
    b8 m_replacementAlternate = false;
    b8 m_sourcesValid = false;
};

using SharedUiTextureImageSmokeScene = RefCountPtr< RefCounter<UiTextureImageSmokeScene>, ArenaRefDeleter<RefCounter<UiTextureImageSmokeScene>, Core::Alloc::GlobalArena> >;

[[nodiscard]] b8 IsUiLayerTextureImageSmokeEnabled();
[[nodiscard]] b8 IsUiLayerTextureImageSkinSmokeEnabled();
[[nodiscard]] SharedUiTextureImageSmokeScene CreateUiTextureImageSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input, Core::Assets::AssetManager& assets);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiWindowSmokeScene : NoCopy{
public:
    [[nodiscard]] b8 paint(Impl::UiPaintContext& context);


private:
    void observeState(const Impl::Ui::WindowMetrics& metrics);
    void logAction(TStringView action);
    void paintMarkers(Impl::UiPaintContext& context, const Impl::Ui::WindowMetrics& metrics)const;


private:
    Impl::Ui::WindowState m_window;
    Impl::Ui::WindowState m_lastWindow;
    Impl::Ui::DisplayMetrics m_lastDisplay;
    u32 m_count = 0u;
    u32 m_lastCount = 0u;
    u32 m_actions = 0u;
    u32 m_sequence = 0u;
    b8 m_locked = false;
    b8 m_lastLocked = false;
    b8 m_displayChanged = false;
};

using SharedUiWindowSmokeScene = RefCountPtr< RefCounter<UiWindowSmokeScene>, ArenaRefDeleter<RefCounter<UiWindowSmokeScene>, Core::Alloc::GlobalArena> >;

[[nodiscard]] b8 IsUiLayerWindowSmokeEnabled();
[[nodiscard]] b8 IsUiLayerWindowSkinSmokeEnabled();
[[nodiscard]] SharedUiWindowSmokeScene CreateUiWindowSmokeScene(Core::Alloc::GlobalArena& arena);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_combo_smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr Impl::Ui::Rect s_Panel{ 24.0f, 24.0f, 400.0f, 160.0f };
static constexpr Impl::Ui::Rect s_Counter{ 456.0f, 32.0f, 130.0f, 36.0f };
static constexpr TStringView s_LogTag = NWB_TEXT("UiComboSmoke");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UiComboSmokeScene::UiComboSmokeScene(Core::InputDispatcher& input)
    : m_input(input)
{
    m_input.addHandlerToBack(*this);
}

UiComboSmokeScene::~UiComboSmokeScene()noexcept{
    m_input.removeHandler(*this);
}

b8 UiComboSmokeScene::paint(Impl::UiPaintContext& context){
    using namespace Impl::Ui;
    ObserveSmokeDisplay(__hidden_ui_combo_smoke::s_LogTag, context.display, m_lastDisplay, m_displayChanged);
    m_source.beginFrame();
    m_focused = false;
    Builder& ui = context.ui;
    ui.style().fontSize = 12.0f;
    ui.style().gap = 8.0f;
    ui.listStyle().padding = { 8.0f, 8.0f, 8.0f, 8.0f };
    Rect panel = __hidden_ui_combo_smoke::s_Panel;
    if(m_bottom)
        panel.y = context.display.logicalHeight - 192.0f;
    if(!ui.beginPanel("combo_fixture", panel))
        return false;
    const WidgetOptions title{ {}, { LayoutSizePolicy::Fixed, 24.0f } };
    if(!ui.label("title", "Combo: 100000 keyed rows, row5 disabled", title))
        return false;
    if(m_visible){
        ComboOptions options;
        options.height = { LayoutSizePolicy::Fixed, 36.0f };
        options.popupHeight = 240.0f;
        options.enabled = m_enabled;
        const ComboResult result = ui.comboBox("choice", m_source, m_state, options);
        if(!result.valid)
            return false;
        m_focused = result.focused;
        if(result.committed)
            ++m_commits;
    }
    else{
        const WidgetOptions spacer{ {}, { LayoutSizePolicy::Fixed, 36.0f } };
        if(!ui.label("omitted_choice", "Combo omitted", spacer))
            return false;
    }
    if(!ui.label("hint", "Preview arrows / Enter commit / Escape cancel", title) || !ui.endPanel())
        return false;
    if(!ui.beginPanel("outside_fixture", { 448.0f, 24.0f, 146.0f, 52.0f }))
        return false;
    const WidgetOptions counter{ { LayoutSizePolicy::Fixed, 130.0f }, { LayoutSizePolicy::Fixed, 36.0f } };
    if(ui.button("underlying", "Outside counter", counter))
        ++m_underlying;
    if(!ui.endPanel())
        return false;
    observeState();
    PaintKeyedSmokeValueMarkers<16u, 12u>(values(), m_sequence, context);
    return true;
}

::NWB::Core::KeyboardInputResult UiComboSmokeScene::keyboardUpdate(const i32 key, const i32, const i32 action, const i32)noexcept{
    if(key < Core::Key::F4 || key > Core::Key::F12)
        return false;
    if(action != Core::InputAction::Press)
        return true;
    switch(key){
    case Core::Key::F4:
        m_state.select(100000u);
        break;
    case Core::Key::F5:
        m_source.reverse();
        break;
    case Core::Key::F6:
        m_source.remove(m_state.selectedKey());
        break;
    case Core::Key::F7:
        m_visible = !m_visible;
        break;
    case Core::Key::F8:
        m_source.toggleEmpty();
        break;
    case Core::Key::F9:
        m_enabled = !m_enabled;
        break;
    case Core::Key::F10:
        m_bottom = !m_bottom;
        break;
    case Core::Key::F11:
        m_state.select(2u);
        break;
    case Core::Key::F12:
        m_source.replace();
        break;
    default:
        break;
    }
    return true;
}

Array<u64, 16u> UiComboSmokeScene::values()const noexcept{
    const auto& list = m_state.listState();
    const auto& placement = list.placement();
    return { m_state.selectedKey(), list.cursorKey(), m_source.rowCount(), placement.firstRow, placement.endRow,
        m_source.labelReads(), static_cast<u64>(m_visible && m_state.isOpen()), static_cast<u64>(m_focused),
        static_cast<u64>(m_source.reversed()), m_source.removedKey(), static_cast<u64>(m_visible), m_commits,
        m_underlying, static_cast<u64>(m_enabled), static_cast<u64>(m_bottom), m_source.instanceGeneration() };
}

Impl::Ui::Rect UiComboSmokeScene::cursorRow()const noexcept{
    const auto& list = m_state.listState();
    const auto& placement = list.placement();
    if(!m_visible || !m_state.isOpen())
        return {};
    const auto index = m_source.indexOf(list.cursorKey());
    if(!index || *index < placement.firstRow || *index >= placement.endRow)
        return {};
    const auto rectangle = SmokeListRowBounds(placement, *index);
    if(!rectangle)
        return {};
    return *rectangle;
}

void UiComboSmokeScene::observeState(){
    const auto current = values();
    const auto& placement = m_state.listState().placement();
    const auto& popup = m_state.placement();
    const auto& bounds = m_state.bounds();
    if(
        m_sequence != 0u && !m_displayChanged && current == m_lastValues
        && placement.offset == m_lastPlacement.offset && placement.contentHeight == m_lastPlacement.contentHeight
        && SameSmokeRect(placement.bounds, m_lastPlacement.bounds)
        && SameSmokeRect(placement.thumb, m_lastPlacement.thumb)
        && SameSmokeRect(popup.bounds, m_lastPopup.bounds)
        && SameSmokeRect(bounds, m_lastBounds)
    )
        return;
    ++m_sequence;
    m_lastValues = current;
    m_lastPlacement = placement;
    m_lastPopup = popup;
    m_lastBounds = bounds;
    m_displayChanged = false;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiComboSmoke: state sequence={} values={},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{}")
        , m_sequence, current[0], current[1], current[2], current[3], current[4], current[5], current[6], current[7]
        , current[8], current[9], current[10], current[11], current[12], current[13], current[14], current[15]
    );
    LogSmokeRect(__hidden_ui_combo_smoke::s_LogTag, m_sequence, NWB_TEXT("trigger"), bounds);
    LogSmokeRect(__hidden_ui_combo_smoke::s_LogTag, m_sequence, NWB_TEXT("popup"), popup.bounds);
    LogSmokeRect(__hidden_ui_combo_smoke::s_LogTag, m_sequence, NWB_TEXT("list"), placement.bounds);
    LogSmokeRect(__hidden_ui_combo_smoke::s_LogTag, m_sequence, NWB_TEXT("viewport"), placement.viewport);
    const auto first = SmokeListRowBounds(placement, placement.firstRow);
    LogSmokeRect(__hidden_ui_combo_smoke::s_LogTag, m_sequence, NWB_TEXT("first_row"), first ? *first : Impl::Ui::Rect{});
    LogSmokeRect(__hidden_ui_combo_smoke::s_LogTag, m_sequence, NWB_TEXT("track"), placement.track);
    LogSmokeRect(__hidden_ui_combo_smoke::s_LogTag, m_sequence, NWB_TEXT("thumb"), placement.thumb);
    LogSmokeRect(__hidden_ui_combo_smoke::s_LogTag, m_sequence, NWB_TEXT("cursor_row"), cursorRow());
    LogSmokeRect(__hidden_ui_combo_smoke::s_LogTag, m_sequence, NWB_TEXT("counter"), __hidden_ui_combo_smoke::s_Counter);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 IsUiLayerComboSmokeEnabled(){
    return ReadSmokeEnvironmentFlag("NWB_UI_LAYER_COMBO");
}

b8 IsUiLayerComboSkinSmokeEnabled(){
    return ReadSmokeEnvironmentFlag("NWB_UI_LAYER_COMBO_SKIN");
}

SharedUiComboSmokeScene CreateUiComboSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input){
    return SharedUiComboSmokeScene(NewArenaObject<RefCounter<UiComboSmokeScene>>(arena, input), ArenaRefDeleter<RefCounter<UiComboSmokeScene>, Core::Alloc::GlobalArena>(&arena), s_AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<u64> UiComboSmokeSource::findEnabled(const u64 start, const b8 reverse)const noexcept{
    if(m_empty)
        return MakeUnexpected(Failure{});
    return m_rows.findEnabled(start, reverse);
}

void UiComboSmokeSource::toggleEmpty()noexcept{
    m_empty = !m_empty;
    ++m_revision;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_edit_smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr TStringView s_PrimaryLabel = NWB_TEXT("primary");
constexpr AStringView s_PrimaryKey = "primary";
constexpr TStringView s_SecondaryLabel = NWB_TEXT("secondary");
constexpr AStringView s_SecondaryKey = "secondary";
constexpr TStringView s_LogTag = NWB_TEXT("UiEditSmoke");
static constexpr AStringView s_Primary = "Hello \xED\x95\x9C\xEA\xB8\x80";
static constexpr AStringView s_Secondary = "Target";
static constexpr Impl::Ui::Rect s_Panel{ 24.0f, 24.0f, 340.0f, 276.0f };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UiEditSmokeScene::UiEditSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input)
    : m_arena(arena)
    , m_input(input)
    , m_primary(arena)
    , m_secondary(arena)
{
    resetModels();
    m_input.addHandlerToBack(*this);
}

UiEditSmokeScene::~UiEditSmokeScene()noexcept{
    m_input.removeHandler(*this);
}

b8 UiEditSmokeScene::paint(Impl::UiPaintContext& context){
    using namespace Impl::Ui;
    ObserveSmokeDisplay(__hidden_ui_edit_smoke::s_LogTag, context.display, m_lastDisplay, m_displayChanged);
    Builder& ui = context.ui;
    ui.style().fontSize = 16.0f;
    ui.style().gap = 8.0f;
    if(!ui.beginPanel("edit_fixture", __hidden_ui_edit_smoke::s_Panel))
        return false;
    const WidgetOptions title{ {}, { LayoutSizePolicy::Fixed, 24.0f } };
    const WidgetOptions label{ {}, { LayoutSizePolicy::Fixed, 18.0f } };
    if(!ui.label("title", "Single-line custom edit boxes", title) || !ui.label("primary_caption", "Latin + Korean", label))
        return false;
    EditBoxOptions options;
    options.width = { LayoutSizePolicy::Fixed, 260.0f };
    options.height = { LayoutSizePolicy::Fixed, 40.0f };
    options.readOnly = m_readOnly;
    m_primaryFocused = false;
    if(m_visible){
        const EditBoxResult result = ui.editBox(__hidden_ui_edit_smoke::s_PrimaryKey, m_primary, m_primaryState, options);
        if(!result.valid)
            return false;
        m_primaryFocused = result.focused;
    }
    else if(!ui.label("primary_hidden", "Primary field is hidden", title))
        return false;
    if(!ui.label("secondary_caption", "Clipboard destination", label))
        return false;
    options.readOnly = false;
    const EditBoxResult secondary = ui.editBox(__hidden_ui_edit_smoke::s_SecondaryKey, m_secondary, m_secondaryState, options);
    if(!secondary.valid)
        return false;
    m_secondaryFocused = secondary.focused;
    if(!ui.label("read_only_hint", m_readOnly ? "F5: primary read-only" : "F5: primary editable", label) || !ui.label("visibility_hint", "F6: hide primary / F7: reset", label) || !ui.endPanel())
        return false;
    observeState(context.text);
    PaintSingleSmokeValueMarkers(values(), m_sequence, context);
    return true;
}

::NWB::Core::KeyboardInputResult UiEditSmokeScene::keyboardUpdate(const i32 key, const i32, const i32 action, const i32){
    if(key != Core::Key::F5 && key != Core::Key::F6 && key != Core::Key::F7)
        return false;
    if(action == Core::InputAction::Press){
        if(key == Core::Key::F5)
            m_readOnly = !m_readOnly;
        else if(key == Core::Key::F6)
            m_visible = !m_visible;
        else
            resetModels();
    }
    return true;
}

void UiEditSmokeScene::resetModels(){
    const b8 reset = m_primary.setText(__hidden_ui_edit_smoke::s_Primary) && m_secondary.setText(__hidden_ui_edit_smoke::s_Secondary);
    NWB_FATAL_ASSERT_MSG(reset, NWB_TEXT("UI edit smoke documents must be valid UTF8"));
}

Array<u32, 12u> UiEditSmokeScene::values()const noexcept{
    return { static_cast<u32>(m_primary.text().size()), ComputeFnv32TextHash(m_primary.text()),
        static_cast<u32>(m_primary.anchor()), static_cast<u32>(m_primary.caret()), static_cast<u32>(m_primaryFocused),
        static_cast<u32>(m_secondary.text().size()), ComputeFnv32TextHash(m_secondary.text()),
        static_cast<u32>(m_secondary.anchor()), static_cast<u32>(m_secondary.caret()), static_cast<u32>(m_secondaryFocused),
        static_cast<u32>(m_readOnly), static_cast<u32>(m_visible) };
}

void UiEditSmokeScene::observeState(Impl::Ui::TextService& text){
    const auto current = values();
    const Impl::Ui::EditBoxPlacement primary = m_visible ? m_primaryState.placement : Impl::Ui::EditBoxPlacement{};
    const auto& secondary = m_secondaryState.placement;
    const auto primarySelection = CaptureEditSelection(m_arena, text, m_primary, primary, 16.0f);
    const auto secondarySelection = CaptureEditSelection(m_arena, text, m_secondary, secondary, 16.0f);
    if(
        m_sequence != 0u && !m_displayChanged && current == m_lastValues
        && SameSmokePlacement(primary, m_lastPrimary)
        && SameSmokePlacement(secondary, m_lastSecondary)
        && SameSmokeRect(primarySelection, m_lastPrimarySelection)
        && SameSmokeRect(secondarySelection, m_lastSecondarySelection)
    )
        return;
    ++m_sequence;
    m_lastValues = current;
    m_lastPrimary = primary;
    m_lastSecondary = secondary;
    m_lastPrimarySelection = primarySelection;
    m_lastSecondarySelection = secondarySelection;
    m_displayChanged = false;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiEditSmoke: state sequence={} primary={},{},{},{},{} secondary={},{},{},{},{} readonly={} visible={}")
        , m_sequence, current[0], current[1], current[2], current[3], current[4]
        , current[5], current[6], current[7], current[8], current[9], current[10], current[11]
    );
    LogSmokeEditGeometry(__hidden_ui_edit_smoke::s_LogTag, m_sequence, __hidden_ui_edit_smoke::s_PrimaryLabel, primary, primarySelection);
    LogSmokeEditGeometry(__hidden_ui_edit_smoke::s_LogTag, m_sequence, __hidden_ui_edit_smoke::s_SecondaryLabel, secondary, secondarySelection);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 IsUiLayerEditSmokeEnabled(){
    return ReadSmokeEnvironmentFlag("NWB_UI_LAYER_EDIT");
}

SharedUiEditSmokeScene CreateUiEditSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input){
    return SharedUiEditSmokeScene(NewArenaObject<RefCounter<UiEditSmokeScene>>(arena, arena, input), ArenaRefDeleter<RefCounter<UiEditSmokeScene>, Core::Alloc::GlobalArena>(&arena), s_AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Impl::Ui::Rect CaptureEditSelection(Core::Alloc::GlobalArena& arena, Impl::Ui::TextService& text, const Impl::Ui::EditModel& model, const Impl::Ui::EditBoxPlacement& placement, const f32 fontSize){
    using namespace Impl::Ui;
    NWB_FATAL_ASSERT(model.textMode() == EditTextMode::SingleLine);
    EditBoxView view(arena);
    const b8 captured = view.snapshot(model);
    NWB_FATAL_ASSERT(captured);
    const EditBoxRange range = view.selectionRange();
    if(range.begin == range.end || placement.content.width <= 0.0f || placement.content.height <= 0.0f)
        return {};
    const b8 shaped = view.shape(text, { .fontSize = fontSize }) == TextLayoutStatus::Success;
    NWB_FATAL_ASSERT(shaped);
    auto selection = view.caretGeometry().rangeOnLine(range, 0u, placement.caret.width);
    NWB_FATAL_ASSERT(selection.has_value());
    if(selection->width > 0.0f){
        const SIMDVectorSingle origin = VectorAdd(VectorSet(selection->x, selection->y, 0.0f, 0.0f), VectorSet(placement.textOrigin.x, placement.textOrigin.y, 0.0f, 0.0f));
        selection->x = VectorGetX(origin);
        selection->y = VectorGetY(origin);
    }
    return *selection;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_image_smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr AStringView s_TitleKey = "title";
constexpr AStringView s_RadioRegion = "radio.mark";
constexpr AStringView s_ProgressRegion = "progress.fill";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UiImageSmokeScene::UiImageSmokeScene(Core::InputDispatcher& input)
    : m_input(input)
{
    m_input.addHandlerToBack(*this);
}

UiImageSmokeScene::~UiImageSmokeScene()noexcept{
    m_input.removeHandler(*this);
}

b8 UiImageSmokeScene::paint(Impl::UiPaintContext& context){
    auto& ui = context.ui;
    ui.style().fontSize = 14.0f;
    ui.style().gap = 8.0f;
    m_declared = {};
    const PassiveSmokeColumns columns = ResolvePassiveSmokeColumns(context.display);
    const f32 width = columns.mainWidth;
    const f32 right = columns.rightColumn;
    const f32 otherWidth = columns.rightWidth;
    if(!paintMain(ui, { 24.0f, 24.0f, width, 392.0f }) || !paintFrozen(ui, { right, 24.0f, otherWidth, 120.0f }) || !paintExternal(context, { right, 176.0f, otherWidth, 84.0f }) || !paintPopups(ui, right) || ui.failed())
        return false;
    observeState(context);
    paintMarkers(context);
    return true;
}

::NWB::Core::KeyboardInputResult UiImageSmokeScene::keyboardUpdate(const i32 key, const i32, const i32 action, const i32)noexcept{
    return HandlePassiveSmokeKeyboard(key, action, m_phase, m_parent, m_child);
}

b8 UiImageSmokeScene::paintMain(Impl::Ui::Builder& ui, const Impl::Ui::Rect& bounds){
    using namespace Impl::Ui;
    if(!ui.beginPanel("image_main", bounds))
        return false;
    const WidgetOptions caption{ {}, { LayoutSizePolicy::Fixed, 16.0f } };
    const WidgetOptions button{ { LayoutSizePolicy::Stretch, 1.0f }, { LayoutSizePolicy::Fixed, 28.0f } };
    if(!ui.label(__hidden_ui_image_smoke::s_TitleKey, "Atlas images / copied size and tint", caption))
        return false;
    if(ui.button("before", "Before images", button))
        ++m_beforeClicks;
    ImageOptions options;
    if(!ui.image("natural_sprite", Name(__hidden_ui_image_smoke::s_RadioRegion), options) || !ui.image("natural_slice", Name(__hidden_ui_image_smoke::s_ProgressRegion)))
        return false;
    const SIMDVectorSingle origin = VectorSet(bounds.x, bounds.y, 0.0f, 0.0f);
    m_declared.rectangles[2u] = Impl::Ui::StoreRect(VectorAdd(origin, VectorSet(8.0f, 68.0f, 24.0f, 24.0f)));
    m_declared.rectangles[3u] = Impl::Ui::StoreRect(VectorAdd(origin, VectorSet(8.0f, 100.0f, 24.0f, 24.0f)));
    options.width = { LayoutSizePolicy::Fixed, 96.0f };
    options.height = { LayoutSizePolicy::Fixed, 40.0f };
    if(!ui.image("fixed_sprite", Name(__hidden_ui_image_smoke::s_RadioRegion), options) || !ui.image("fixed_slice", Name(__hidden_ui_image_smoke::s_ProgressRegion), options))
        return false;
    m_declared.rectangles[4u] = Impl::Ui::StoreRect(VectorAdd(origin, VectorSet(8.0f, 132.0f, 96.0f, 40.0f)));
    m_declared.rectangles[5u] = Impl::Ui::StoreRect(VectorAdd(origin, VectorSet(8.0f, 180.0f, 96.0f, 40.0f)));
    options.width = { LayoutSizePolicy::Stretch, 1.0f };
    options.height = { LayoutSizePolicy::Fixed, 32.0f };
    if(!ui.image("stretch", Name(__hidden_ui_image_smoke::s_ProgressRegion), options))
        return false;
    m_declared.rectangles[6u] = Impl::Ui::StoreRect(VectorAdd(VectorSet(bounds.x, bounds.y, bounds.width, 0.0f), VectorSet(8.0f, 228.0f, -16.0f, 32.0f)));
    options.width = { LayoutSizePolicy::Fixed, 96.0f };
    options.height = { LayoutSizePolicy::Fixed, 40.0f };
    options.tint = { 0.4f, 0.7f, 0.9f, 0.5f };
    if(!ui.image("tinted", Name(__hidden_ui_image_smoke::s_RadioRegion), options))
        return false;
    m_declared.rectangles[7u] = Impl::Ui::StoreRect(VectorAdd(origin, VectorSet(8.0f, 268.0f, 96.0f, 40.0f)));
    options.height = { LayoutSizePolicy::Fixed, 24.0f };
    options.tint = { 1.0f, 1.0f, 1.0f, 0.0f };
    if(!ui.image("transparent", Name(__hidden_ui_image_smoke::s_RadioRegion), options))
        return false;
    m_declared.rectangles[8u] = Impl::Ui::StoreRect(VectorAdd(origin, VectorSet(8.0f, 316.0f, 96.0f, 24.0f)));
    if(ui.button("after", "After images", button))
        ++m_afterClicks;
    return ui.endPanel();
}

b8 UiImageSmokeScene::paintFrozen(Impl::Ui::Builder& ui, const Impl::Ui::Rect& bounds){
    using namespace Impl::Ui;
    if(!ui.beginPanel("image_frozen", bounds))
        return false;
    const WidgetOptions caption{ {}, { LayoutSizePolicy::Fixed, 16.0f } };
    if(!ui.label(__hidden_ui_image_smoke::s_TitleKey, "Declaration retains region, options and tint", caption))
        return false;
    Name region = m_phase ? Name(__hidden_ui_image_smoke::s_RadioRegion) : Name(__hidden_ui_image_smoke::s_ProgressRegion);
    ImageOptions options;
    options.width = { LayoutSizePolicy::Fixed, 96.0f };
    options.height = { LayoutSizePolicy::Fixed, 40.0f };
    options.tint = m_phase ? Color{ 0.5f, 0.75f, 1.0f, 0.5f } : Color{};
    m_declared.values[8u] = m_phase ? 2u : 1u;
    if(!ui.image("frozen", region, options))
        return false;
    m_declared.rectangles[9u] = Impl::Ui::StoreRect(VectorAdd(VectorSet(bounds.x, bounds.y, 0.0f, 0.0f), VectorSet(8.0f, 32.0f, 96.0f, 40.0f)));
    region = m_phase ? Name(__hidden_ui_image_smoke::s_ProgressRegion) : Name(__hidden_ui_image_smoke::s_RadioRegion);
    m_declared.values[9u] = m_phase ? 1u : 2u;
    options.width = { LayoutSizePolicy::Fixed, 70.0f };
    options.height = { LayoutSizePolicy::Fixed, 70.0f };
    options.tint = { 0.0f, 0.0f, 0.0f, 1.0f };
    return ui.endPanel();
}

b8 UiImageSmokeScene::paintExternal(Impl::UiPaintContext& context, const Impl::Ui::Rect& bounds){
    using namespace Impl::Ui;
    auto& ui = context.ui;
    const SIMDVectorSingle content = VectorAdd(VectorSet(bounds.x, bounds.y, bounds.width, 0.0f), VectorSet(8.0f, 8.0f, -16.0f, 40.0f));
    m_declared.rectangles[10u] = Impl::Ui::StoreRect(content);
    const Rect clip = Impl::Ui::StoreRect(VectorMultiply(content, VectorSet(1.0f, 1.0f, 0.5f, 1.0f)));
    m_declared.rectangles[11u] = clip;
    context.paint.fillRect(bounds, { 0.07f, 0.13f, 0.2f, 1.0f });
    context.paint.pushClip(clip);
    ImageOptions options;
    options.width = { LayoutSizePolicy::Stretch, 1.0f };
    options.height = { LayoutSizePolicy::Fixed, 40.0f };
    const b8 painted = ui.beginPanel("image_external", bounds) && ui.image("external", Name(__hidden_ui_image_smoke::s_ProgressRegion), options) && ui.endPanel();
    const b8 popped = context.paint.popClip();
    return painted && popped;
}

b8 UiImageSmokeScene::paintPopups(Impl::Ui::Builder& ui, const f32 right){
    using namespace Impl::Ui;
    PopupOptions parent;
    parent.anchor = { right, 300.0f, 8.0f, 24.0f };
    parent.size = { 280.0f, 128.0f };
    if(!ui.beginPopup("image_parent", m_parent, parent))
        return !ui.failed();
    const WidgetOptions button{ { LayoutSizePolicy::Stretch, 1.0f }, { LayoutSizePolicy::Fixed, 28.0f } };
    if(ui.button("parent_action", "Parent action", button))
        m_parent.close();
    ImageOptions options;
    options.width = { LayoutSizePolicy::Stretch, 1.0f };
    options.height = { LayoutSizePolicy::Fixed, 40.0f };
    if(!ui.image("parent_value", Name(__hidden_ui_image_smoke::s_ProgressRegion), options))
        return false;
    PopupOptions child;
    child.anchor = { right + 100.0f, 370.0f, 8.0f, 16.0f };
    child.size = { 240.0f, 128.0f };
    if(ui.beginPopup("image_child", m_child, child)){
        options.tint = { 0.5f, 1.0f, 0.5f, 0.75f };
        if(!ui.image("child_value", Name(__hidden_ui_image_smoke::s_RadioRegion), options))
            return false;
        if(ui.button("child_action", "Child action", button))
            m_child.close();
        if(!ui.endPopup())
            return false;
    }
    if(ui.failed() || !ui.endPopup())
        return false;
    const Rect& parentBounds = m_parent.placement().bounds;
    m_declared.rectangles[14u] = parentBounds;
    m_declared.rectangles[12u] = Impl::Ui::StoreRect(VectorAdd(VectorSet(parentBounds.x, parentBounds.y, parentBounds.width, 0.0f), VectorSet(8.0f, 44.0f, -16.0f, 40.0f)));
    if(m_child.isOpen()){
        const Rect& childBounds = m_child.placement().bounds;
        m_declared.rectangles[15u] = childBounds;
        m_declared.rectangles[13u] = Impl::Ui::StoreRect(VectorAdd(VectorSet(childBounds.x, childBounds.y, childBounds.width, 0.0f), VectorSet(8.0f, 8.0f, -16.0f, 40.0f)));
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 IsUiLayerImageSmokeEnabled(){
    return ReadSmokeEnvironmentFlag("NWB_UI_LAYER_IMAGE");
}

b8 IsUiLayerImageSkinSmokeEnabled(){
    return ReadSmokeEnvironmentFlag("NWB_UI_LAYER_IMAGE_SKIN");
}

SharedUiImageSmokeScene CreateUiImageSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input){
    return SharedUiImageSmokeScene(NewArenaObject<RefCounter<UiImageSmokeScene>>(arena, input), ArenaRefDeleter<RefCounter<UiImageSmokeScene>, Core::Alloc::GlobalArena>(&arena), s_AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_image_snapshot{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr Array<TStringView, 16u> s_RectNames{
    NWB_TEXT("before"), NWB_TEXT("after"), NWB_TEXT("natural_sprite"), NWB_TEXT("natural_slice"),
    NWB_TEXT("fixed_sprite"), NWB_TEXT("fixed_slice"), NWB_TEXT("stretch"), NWB_TEXT("tinted"), NWB_TEXT("transparent"),
    NWB_TEXT("frozen"), NWB_TEXT("external"), NWB_TEXT("external_clip"), NWB_TEXT("parent_image"), NWB_TEXT("child_image"),
    NWB_TEXT("parent"), NWB_TEXT("child")
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void UiImageSmokeScene::observeState(Impl::UiPaintContext& context){
    using namespace Impl::Ui;
    using namespace __hidden_ui_image_snapshot;
    UiImageSnapshot current = m_declared;
    current.display = context.display;
    const InputRouter& input = context.ui.input();
    const auto observation = ObservePassiveSmokeTargets(input, m_parent.instanceGeneration(), m_child.instanceGeneration(), 2u, 14u, [](const usize index)noexcept{ return index != 11u; }, current.rectangles);
    const u32 focusCode = observation.focusCode;
    const u32 imageTargets = observation.passiveTargets;
    current.values[0u] = static_cast<u64>(m_phase);
    current.values[1u] = focusCode;
    current.values[2u] = m_beforeClicks;
    current.values[3u] = m_afterClicks;
    current.values[4u] = static_cast<u64>(m_parent.isOpen());
    current.values[5u] = static_cast<u64>(m_child.isOpen());
    current.values[6u] = input.popupCount();
    current.values[7u] = imageTargets;
    const auto update = PublishPassiveSmokeSnapshot([&]()noexcept{ return current.values != m_snapshot.values; }, current, m_snapshot);
    if(!update.published)
        return;
    if(update.displayChanged){
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiImageSmoke: display logical={}x{} scale={}x{}"), current.display.logicalWidth, current.display.logicalHeight, current.display.pixelScaleX, current.display.pixelScaleY);
    }
    const auto& value = current.values;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiImageSmoke: state sequence={} values={},{},{},{},{},{},{},{},{},{}"), current.sequence, value[0], value[1], value[2], value[3], value[4], value[5], value[6], value[7], value[8], value[9]);
    for(usize index = 0u; index < current.rectangles.size(); ++index)
        LogSmokeRect(NWB_TEXT("UiImageSmoke"), current.sequence, s_RectNames[index], current.rectangles[index]);
}

void UiImageSmokeScene::paintMarkers(Impl::UiPaintContext& context)const{
    PaintPassiveSmokeValueMarkers(m_snapshot.values, m_snapshot.sequence, context);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_interactive_smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr TStringView s_IncreaseLabel = NWB_TEXT("increase");
constexpr AStringView s_IncreaseKey = "increase";
constexpr TStringView s_EnabledLabel = NWB_TEXT("enabled");
constexpr AStringView s_EnabledKey = "enabled";
constexpr TStringView s_ResetLabel = NWB_TEXT("reset");
constexpr AStringView s_ResetKey = "reset";
static constexpr Impl::Ui::Rect s_Panel{ 18.0f, 18.0f, 320.0f, 220.0f };
static constexpr Impl::Ui::Color s_Off{ 0.08f, 0.01f, 0.01f, 1.0f };
static constexpr Impl::Ui::Color s_On{ 0.02f, 0.70f, 0.12f, 1.0f };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 UiInteractiveSmokeScene::paint(Impl::UiPaintContext& context){
    using namespace Impl::Ui;
    observeDisplay(context.display);
    Builder& ui = context.ui;
    ui.style().gap = 8.0f;
    if(!ui.beginPanel("interaction", __hidden_ui_interactive_smoke::s_Panel))
        return false;
    const WidgetOptions caption{ {}, { LayoutSizePolicy::Fixed, 24.0f } };
    if(!ui.label("caption", "Custom UI interaction", caption))
        return false;
    ContainerOptions actions;
    actions.height = { LayoutSizePolicy::Fixed, 40.0f };
    if(!ui.beginRow("actions", actions))
        return false;
    const WidgetOptions increase{ { LayoutSizePolicy::Fixed, 130.0f }, { LayoutSizePolicy::Fixed, 36.0f }, m_enabled };
    if(ui.button(__hidden_ui_interactive_smoke::s_IncreaseKey, "Increase", increase)){
        m_count = Min(m_count + 1u, 255u);
        logAction(__hidden_ui_interactive_smoke::s_IncreaseLabel);
    }
    const WidgetOptions checkbox{ { LayoutSizePolicy::Fixed, 140.0f }, { LayoutSizePolicy::Fixed, 36.0f } };
    if(ui.checkbox(__hidden_ui_interactive_smoke::s_EnabledKey, "Enabled", m_enabled, checkbox))
        logAction(__hidden_ui_interactive_smoke::s_EnabledLabel);
    if(!ui.endContainer())
        return false;
    const WidgetOptions reset{ { LayoutSizePolicy::Fixed, 130.0f }, { LayoutSizePolicy::Fixed, 36.0f } };
    if(ui.button(__hidden_ui_interactive_smoke::s_ResetKey, "Reset", reset)){
        m_count = 0u;
        logAction(__hidden_ui_interactive_smoke::s_ResetLabel);
    }
    if(!ui.endPanel())
        return false;
    paintMarkers(context);
    return true;
}

void UiInteractiveSmokeScene::observeDisplay(const Impl::Ui::DisplayMetrics& display){
    if(m_lastDisplay.logicalWidth == display.logicalWidth && m_lastDisplay.logicalHeight == display.logicalHeight && m_lastDisplay.pixelScaleX == display.pixelScaleX && m_lastDisplay.pixelScaleY == display.pixelScaleY)
        return;
    m_lastDisplay = display;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiInteractiveSmoke: display logical={}x{} scale={}x{}"), display.logicalWidth, display.logicalHeight, display.pixelScaleX, display.pixelScaleY);
}

void UiInteractiveSmokeScene::logAction(const TStringView action){
    ++m_actions;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiInteractiveSmoke: action={} count={} enabled={} actions={}"), action, m_count, static_cast<u32>(m_enabled), m_actions);
}

void UiInteractiveSmokeScene::paintMarkers(Impl::UiPaintContext& context)const{
    const f32 y = context.display.logicalHeight - 20.0f;
    const auto positions = BuildSmokeMarkerPositions<8u, 18u, 20u>();
    for(u32 bit = 0u; bit < 8u; ++bit){
        const b8 set = (m_count & (1u << bit)) != 0u;
        const Impl::Ui::Rect marker{ positions[bit], y, 14.0f, 12.0f };
        context.paint.fillRect(marker, set ? __hidden_ui_interactive_smoke::s_On : __hidden_ui_interactive_smoke::s_Off);
    }
    const Impl::Ui::Rect enabledMarker{ 210.0f, y, 28.0f, 12.0f };
    context.paint.fillRect(enabledMarker, m_enabled ? __hidden_ui_interactive_smoke::s_On : __hidden_ui_interactive_smoke::s_Off);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 IsUiLayerInteractionSmokeEnabled(){
    return ReadSmokeEnvironmentFlag("NWB_UI_LAYER_INTERACTIVE");
}

SharedUiInteractiveSmokeScene CreateUiInteractiveSmokeScene(Core::Alloc::GlobalArena& arena){
    return SharedUiInteractiveSmokeScene(NewArenaObject<RefCounter<UiInteractiveSmokeScene>>(arena), ArenaRefDeleter<RefCounter<UiInteractiveSmokeScene>, Core::Alloc::GlobalArena>(&arena), s_AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_list_smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr Impl::Ui::Rect s_Panel{ 24.0f, 24.0f, 400.0f, 340.0f };
static constexpr Impl::Ui::Rect s_Counter{ 32.0f, 312.0f, 140.0f, 36.0f };
static constexpr TStringView s_LogTag = NWB_TEXT("UiListSmoke");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UiListSmokeScene::UiListSmokeScene(Core::InputDispatcher& input)
    : m_input(input)
{
    m_input.addHandlerToBack(*this);
}

UiListSmokeScene::~UiListSmokeScene()noexcept{
    m_input.removeHandler(*this);
}

b8 UiListSmokeScene::paint(Impl::UiPaintContext& context){
    using namespace Impl::Ui;
    ObserveSmokeDisplay(__hidden_ui_list_smoke::s_LogTag, context.display, m_lastDisplay, m_displayChanged);
    m_source.beginFrame();
    m_focused = false;
    Builder& ui = context.ui;
    ui.style().fontSize = 12.0f;
    ui.style().gap = 8.0f;
    ui.listStyle().padding = { 8.0f, 8.0f, 8.0f, 8.0f };
    if(!ui.beginPanel("list_fixture", __hidden_ui_list_smoke::s_Panel))
        return false;
    const WidgetOptions title{ {}, { LayoutSizePolicy::Fixed, 24.0f } };
    if(!ui.label("title", "100000 keyed rows: row5 disabled", title))
        return false;
    if(m_visible){
        ListOptions options;
        options.height = { LayoutSizePolicy::Fixed, 240.0f };
        const ListResult result = ui.virtualList("rows", m_source, m_state, options);
        if(!result.valid)
            return false;
        m_focused = result.focused;
        if(result.activated)
            ++m_commits;
    }
    else{
        const WidgetOptions spacer{ {}, { LayoutSizePolicy::Fixed, 240.0f } };
        if(!ui.label("omitted_rows", "Rows omitted", spacer))
            return false;
    }
    const WidgetOptions counter{ { LayoutSizePolicy::Fixed, 140.0f }, { LayoutSizePolicy::Fixed, 36.0f } };
    if(ui.button("underlying", "Outside counter", counter))
        ++m_underlying;
    if(!ui.endPanel())
        return false;
    observeState();
    PaintKeyedSmokeValueMarkers<20u, 14u>(values(), m_sequence, context);
    return true;
}

::NWB::Core::KeyboardInputResult UiListSmokeScene::keyboardUpdate(const i32 key, const i32, const i32 action, const i32)noexcept{
    if(key != Core::Key::F5 && key != Core::Key::F6 && key != Core::Key::F7)
        return false;
    if(action == Core::InputAction::Press){
        if(key == Core::Key::F5)
            m_source.reverse();
        else if(key == Core::Key::F6)
            m_source.remove(m_state.selectedKey());
        else
            m_visible = !m_visible;
    }
    return true;
}

Array<u64, 12u> UiListSmokeScene::values()const noexcept{
    const auto& placement = m_state.placement();
    return { m_state.selectedKey(), m_state.cursorKey(), m_source.rowCount(), placement.firstRow, placement.endRow,
        m_source.labelReads(), static_cast<u64>(m_focused), static_cast<u64>(m_source.reversed()),
        m_source.removedKey(), static_cast<u64>(m_visible), m_commits, m_underlying };
}

Impl::Ui::Rect UiListSmokeScene::selectedRow()const noexcept{
    const auto& placement = m_state.placement();
    if(!m_visible)
        return {};
    const auto index = m_source.indexOf(m_state.selectedKey());
    if(!index || *index < placement.firstRow || *index >= placement.endRow)
        return {};
    const auto rectangle = SmokeListRowBounds(placement, *index);
    if(!rectangle)
        return {};
    return *rectangle;
}

void UiListSmokeScene::observeState(){
    const auto current = values();
    const auto& placement = m_state.placement();
    if(
        m_sequence != 0u && !m_displayChanged && current == m_lastValues
        && placement.offset == m_lastPlacement.offset && placement.contentHeight == m_lastPlacement.contentHeight
        && SameSmokeRect(placement.bounds, m_lastPlacement.bounds) && SameSmokeRect(placement.thumb, m_lastPlacement.thumb)
    )
        return;
    ++m_sequence;
    m_lastValues = current;
    m_lastPlacement = placement;
    m_displayChanged = false;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiListSmoke: state sequence={} values={},{},{},{},{},{},{},{},{},{},{},{}")
        , m_sequence, current[0], current[1], current[2], current[3], current[4], current[5]
        , current[6], current[7], current[8], current[9], current[10], current[11]
    );
    LogSmokeRect(__hidden_ui_list_smoke::s_LogTag, m_sequence, NWB_TEXT("list"), placement.bounds);
    LogSmokeRect(__hidden_ui_list_smoke::s_LogTag, m_sequence, NWB_TEXT("viewport"), placement.viewport);
    const auto first = SmokeListRowBounds(placement, placement.firstRow);
    LogSmokeRect(__hidden_ui_list_smoke::s_LogTag, m_sequence, NWB_TEXT("first_row"), first ? *first : Impl::Ui::Rect{});
    LogSmokeRect(__hidden_ui_list_smoke::s_LogTag, m_sequence, NWB_TEXT("track"), placement.track);
    LogSmokeRect(__hidden_ui_list_smoke::s_LogTag, m_sequence, NWB_TEXT("thumb"), placement.thumb);
    LogSmokeRect(__hidden_ui_list_smoke::s_LogTag, m_sequence, NWB_TEXT("selected_row"), selectedRow());
    LogSmokeRect(__hidden_ui_list_smoke::s_LogTag, m_sequence, NWB_TEXT("counter"), __hidden_ui_list_smoke::s_Counter);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 IsUiLayerListSmokeEnabled(){
    return ReadSmokeEnvironmentFlag("NWB_UI_LAYER_LIST");
}

b8 IsUiLayerListSkinSmokeEnabled(){
    return ReadSmokeEnvironmentFlag("NWB_UI_LAYER_LIST_SKIN");
}

SharedUiListSmokeScene CreateUiListSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input){
    return SharedUiListSmokeScene(NewArenaObject<RefCounter<UiListSmokeScene>>(arena, input), ArenaRefDeleter<RefCounter<UiListSmokeScene>, Core::Alloc::GlobalArena>(&arena), s_AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


u64 UiListSmokeSource::key(const u64 index)const noexcept{
    const u64 count = rowCount();
    return index < count ? UiWidgetTests::ArithmeticFixtureRowKey(count, m_removed, m_reversed, index) : 0u;
}

Expected<u64> UiListSmokeSource::indexOf(const u64 keyValue)const noexcept{
    return UiWidgetTests::FindArithmeticFixtureRowIndex(rowCount(), m_removed, m_reversed, keyValue);
}

Expected<u64> UiListSmokeSource::findEnabled(const u64 start, const b8 reverse)const noexcept{
    return FindAdjacentEnabledRow(*this, start, reverse);
}

StringView UiListSmokeSource::text(const u64 index)const noexcept{
    ++m_labelReads;
    const u64 rowKey = key(index);
    if(rowKey == 0u)
        return {};
    m_label[0] = 'R';
    m_label[1] = 'o';
    m_label[2] = 'w';
    m_label[3] = ' ';
    Array<char, s_UsizeDecimalDigitCount> digits{};
    const AStringView formatted = FormatDecimal(rowKey, digits);
    NWB_MEMCPY(m_label.data() + 4u, m_label.size() - 4u, formatted.data(), formatted.size());
    return { m_label.data(), 4u + formatted.size() };
}

b8 UiListSmokeSource::enabled(const u64 index)const noexcept{
    return index < rowCount() && key(index) != 5u;
}

void UiListSmokeSource::reverse()noexcept{
    m_reversed = !m_reversed;
    ++m_revision;
}

void UiListSmokeSource::remove(const u64 keyValue)noexcept{
    if(keyValue == 0u || keyValue > 100000u || keyValue == m_removed)
        return;
    m_removed = keyValue;
    ++m_revision;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_nested_popup_smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr AStringView s_TitleKey = "title";
constexpr AStringView s_ActionKey = "action";
constexpr AStringView s_EditKey = "edit";
constexpr TStringView s_MenuAnchorLabel = NWB_TEXT("menu_anchor");
constexpr AStringView s_MenuAnchorKey = "menu_anchor";
static constexpr Impl::Ui::Rect s_Open{ 32.0f, 52.0f, 140.0f, 28.0f };
static constexpr Impl::Ui::Rect s_Counter{ 176.0f, 52.0f, 140.0f, 28.0f };
static constexpr Array<TStringView, 26u> s_RectNames{
    NWB_TEXT("open"), NWB_TEXT("counter"), NWB_TEXT("parent"), NWB_TEXT("child"),
    NWB_TEXT("before_button"), NWB_TEXT("before_edit"), NWB_TEXT("before_list"), NWB_TEXT("before_row2"),
    NWB_TEXT("child_open"), NWB_TEXT("after_button"), NWB_TEXT("after_edit"), NWB_TEXT("after_list"),
    NWB_TEXT("after_row2"), NWB_TEXT("child_action"), NWB_TEXT("child_edit"), NWB_TEXT("close_branch"),
    NWB_TEXT("combo"), NWB_TEXT("combo_popup"), NWB_TEXT("combo_row2"), NWB_TEXT("search"),
    NWB_TEXT("search_popup"), NWB_TEXT("search_query"), NWB_TEXT("search_row2"), __hidden_ui_nested_popup_smoke::s_MenuAnchorLabel,
    NWB_TEXT("menu"), NWB_TEXT("menu_row2")
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UiNestedPopupSmokeScene::UiNestedPopupSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input)
    : m_input(input)
    , m_source(arena)
    , m_search(arena)
    , m_beforeText(arena)
    , m_afterText(arena)
    , m_childText(arena)
{
    const b8 initialized = m_beforeText.setText("Before") && m_afterText.setText("After") && m_childText.setText("Child");
    NWB_FATAL_ASSERT(initialized);
    m_combo.select(1u);
    m_search.combo().select(1u);
    m_input.addHandlerToBack(*this);
}

UiNestedPopupSmokeScene::~UiNestedPopupSmokeScene()noexcept{
    m_input.removeHandler(*this);
}

b8 UiNestedPopupSmokeScene::paint(Impl::UiPaintContext& context){
    ObserveSmokeDisplay(NWB_TEXT("UiNestedPopupSmoke"), context.display, m_lastDisplay, m_displayChanged);
    m_source.beginFrame();
    m_childDeclared = false;
    auto& ui = context.ui;
    ui.style().fontSize = 12.0f;
    ui.style().gap = 4.0f;
    if(!paintRoot(ui))
        return false;
    Impl::Ui::PopupOptions options;
    options.anchor = { 72.0f, 54.0f, 120.0f, 24.0f };
    options.size = { 300.0f, 420.0f };
    if(ui.beginPopup("parent", m_parent, options)){
        if(!paintParent(ui) || !ui.endPopup())
            return false;
    }
    if(ui.failed())
        return false;
    observeState(context);
    paintMarkers(context);
    return true;
}

::NWB::Core::KeyboardInputResult UiNestedPopupSmokeScene::keyboardUpdate(const i32 key, const i32, const i32 action, const i32)noexcept{
    if(key != Core::Key::F8)
        return false;
    if(action == Core::InputAction::Press)
        m_parent.close();
    return true;
}

b8 UiNestedPopupSmokeScene::paintRoot(Impl::Ui::Builder& ui){
    using namespace Impl::Ui;
    if(!ui.beginPanel("nested_root", { 24.0f, 24.0f, 300.0f, 80.0f }))
        return false;
    const WidgetOptions caption{ {}, { LayoutSizePolicy::Fixed, 16.0f } };
    if(!ui.label(__hidden_ui_nested_popup_smoke::s_TitleKey, "Nested popup ownership / F8 closes ancestor", caption))
        return false;
    ContainerOptions row;
    row.height = { LayoutSizePolicy::Fixed, 28.0f };
    row.gap = 4.0f;
    if(!ui.beginRow("root_buttons", row))
        return false;
    const WidgetOptions button{ { LayoutSizePolicy::Fixed, 140.0f }, { LayoutSizePolicy::Fixed, 28.0f } };
    if(ui.button("open", "Open parent", button))
        m_parent.open();
    if(ui.button("counter", "Outside counter", button))
        ++m_outside;
    return ui.endContainer() && ui.endPanel();
}

b8 UiNestedPopupSmokeScene::paintParent(Impl::Ui::Builder& ui){
    using namespace Impl::Ui;
    const WidgetOptions caption{ {}, { LayoutSizePolicy::Fixed, 16.0f } };
    const WidgetOptions button{ { LayoutSizePolicy::Fixed, 100.0f }, { LayoutSizePolicy::Fixed, 32.0f } };
    const WidgetOptions wide{ { LayoutSizePolicy::Stretch, 1.0f }, { LayoutSizePolicy::Fixed, 32.0f } };
    ContainerOptions row;
    row.height = { LayoutSizePolicy::Fixed, 32.0f };
    row.gap = 4.0f;
    EditBoxOptions edit;
    edit.height = { LayoutSizePolicy::Fixed, 32.0f };
    ListOptions list;
    list.height = { LayoutSizePolicy::Fixed, 56.0f };
    if(!ui.label(__hidden_ui_nested_popup_smoke::s_TitleKey, "Parent scope resumes after nested child", caption) || !ui.beginRow("before", row))
        return false;
    if(ui.button(__hidden_ui_nested_popup_smoke::s_ActionKey, "Before", button))
        ++m_beforeClicks;
    const EditBoxResult before = ui.editBox(__hidden_ui_nested_popup_smoke::s_EditKey, m_beforeText, m_beforeEdit, edit);
    if(!before.valid || !ui.endContainer() || !ui.virtualList("before_list", m_source, m_beforeList, list).valid)
        return false;
    if(ui.button("child_open", "Open right child", wide))
        m_child.open();
    if(!paintChild(ui) || !ui.beginRow("after", row))
        return false;
    if(ui.button(__hidden_ui_nested_popup_smoke::s_ActionKey, "After", button))
        ++m_afterClicks;
    const EditBoxResult after = ui.editBox(__hidden_ui_nested_popup_smoke::s_EditKey, m_afterText, m_afterEdit, edit);
    if(!after.valid || !ui.endContainer() || !ui.virtualList("after_list", m_source, m_afterList, list).valid)
        return false;
    ComboOptions combo;
    combo.height = { LayoutSizePolicy::Fixed, 32.0f };
    combo.popupHeight = 172.0f;
    if(!ui.comboBox("combo", m_source, m_combo, combo).valid)
        return false;
    SearchComboOptions search;
    search.combo = combo;
    search.combo.popupHeight = 196.0f;
    if(!ui.searchComboBox("search", m_source, m_search, search).combo.valid)
        return false;
    if(ui.button(__hidden_ui_nested_popup_smoke::s_MenuAnchorKey, "Right-click for context commands", wide))
        m_command = 0u;
    ContextMenuOptions menu;
    menu.size = { 200.0f, 172.0f };
    const ContextMenuResult command = ui.contextMenu("commands", __hidden_ui_nested_popup_smoke::s_MenuAnchorKey, m_source, m_menu, menu);
    if(command.activated)
        m_command = command.key;
    return command.valid;
}

b8 UiNestedPopupSmokeScene::paintChild(Impl::Ui::Builder& ui){
    using namespace Impl::Ui;
    PopupOptions options;
    const Rect& parent = m_parent.placement().bounds;
    options.anchor = Impl::Ui::StoreRect(VectorSubtract(VectorAdd(VectorSet(parent.x, parent.y, 8.0f, 32.0f), VectorSet(parent.width, 124.0f, 0.0f, 0.0f)), VectorSet(8.0f, 0.0f, 0.0f, 0.0f)));
    options.side = PopupPlacementSide::Right;
    options.size = { 220.0f, 178.0f };
    if(!ui.beginPopup("child", m_child, options))
        return !ui.failed();
    m_childDeclared = true;
    const WidgetOptions caption{ {}, { LayoutSizePolicy::Fixed, 16.0f } };
    const WidgetOptions button{ { LayoutSizePolicy::Stretch, 1.0f }, { LayoutSizePolicy::Fixed, 32.0f } };
    if(!ui.label(__hidden_ui_nested_popup_smoke::s_TitleKey, "Child escapes parent clip", caption))
        return false;
    if(ui.button(__hidden_ui_nested_popup_smoke::s_ActionKey, "Child action", button))
        ++m_childClicks;
    EditBoxOptions edit;
    edit.height = { LayoutSizePolicy::Fixed, 32.0f };
    const EditBoxResult text = ui.editBox(__hidden_ui_nested_popup_smoke::s_EditKey, m_childText, m_childEdit, edit);
    if(ui.button("close_branch", "Close whole ancestor branch", button))
        m_parent.close();
    return text.valid && ui.endPopup();
}

void UiNestedPopupSmokeScene::observeState(Impl::UiPaintContext& context){
    using namespace Impl::Ui;
    m_rectangles.fill({});
    m_rectangles[0] = __hidden_ui_nested_popup_smoke::s_Open;
    m_rectangles[1] = __hidden_ui_nested_popup_smoke::s_Counter;
    if(m_parent.isOpen()){
        const Rect parent = m_parent.placement().bounds;
        m_rectangles[2] = parent;
        const SIMDVectorSingle parentFrame = VectorSet(parent.x, parent.y, parent.width, 0.0f);
        m_rectangles[4] = Impl::Ui::StoreRect(VectorAdd(VectorSet(parent.x, parent.y, 100.0f, 32.0f), VectorSet(8.0f, 28.0f, 0.0f, 0.0f)));
        m_rectangles[5] = m_beforeEdit.placement.bounds;
        m_rectangles[6] = m_beforeList.placement().bounds;
        m_rectangles[7] = rowBounds(m_beforeList, m_source, 2u);
        m_rectangles[8] = Impl::Ui::StoreRect(VectorAdd(parentFrame, VectorSet(8.0f, 124.0f, -16.0f, 32.0f)));
        m_rectangles[9] = Impl::Ui::StoreRect(VectorAdd(VectorSet(parent.x, parent.y, 100.0f, 32.0f), VectorSet(8.0f, 160.0f, 0.0f, 0.0f)));
        m_rectangles[10] = m_afterEdit.placement.bounds;
        m_rectangles[11] = m_afterList.placement().bounds;
        m_rectangles[12] = rowBounds(m_afterList, m_source, 2u);
        m_rectangles[16] = m_combo.bounds();
        m_rectangles[19] = m_search.combo().bounds();
        m_rectangles[23] = Impl::Ui::StoreRect(VectorAdd(parentFrame, VectorSet(8.0f, 328.0f, -16.0f, 32.0f)));
        if(m_childDeclared && m_child.isOpen()){
            const Rect child = m_child.placement().bounds;
            m_rectangles[3] = child;
            const SIMDVectorSingle childFrame = VectorSet(child.x, child.y, child.width, 0.0f);
            m_rectangles[13] = Impl::Ui::StoreRect(VectorAdd(childFrame, VectorSet(8.0f, 28.0f, -16.0f, 32.0f)));
            m_rectangles[14] = m_childEdit.placement.bounds;
            m_rectangles[15] = Impl::Ui::StoreRect(VectorAdd(childFrame, VectorSet(8.0f, 100.0f, -16.0f, 32.0f)));
        }
        if(m_combo.isOpen()){
            m_rectangles[17] = m_combo.placement().bounds;
            m_rectangles[18] = rowBounds(m_combo.listState(), m_source, 2u);
        }
        if(m_search.combo().isOpen()){
            m_rectangles[20] = m_search.combo().placement().bounds;
            m_rectangles[21] = m_search.editorState().placement.bounds;
            m_rectangles[22] = rowBounds(m_search.combo().listState(), m_source.filtered(), 2u);
        }
        if(m_menu.isOpen()){
            m_rectangles[24] = m_menu.placement().bounds;
            m_rectangles[25] = rowBounds(m_menu.listState(), m_source, 2u);
        }
    }
    m_focusScope = 0u;
    m_focusCode = 0u;
    m_popupTargets = 0u;
    const auto& input = context.ui.input();
    m_popupCount = static_cast<u32>(input.popupCount());
    m_childParentScope = false;
    for(const auto& target : input.targets()){
        if(target.popup.valid())
            ++m_popupTargets;
        if(target.popup.instanceGeneration == m_child.instanceGeneration()){
            const PopupScope* scope = input.popupScope(target.popup);
            m_childParentScope |= scope && scope->parent.instanceGeneration == m_parent.instanceGeneration();
        }
        if(target.id != input.focus())
            continue;
        if(target.popup.valid()){
            m_focusScope = target.popup.instanceGeneration == m_child.instanceGeneration() ? 2u : target.popup.instanceGeneration == m_parent.instanceGeneration() ? 1u : 3u;
        }
        for(usize index = 0u; index < m_rectangles.size(); ++index){
            if(SameSmokeRect(target.rectangle, m_rectangles[index])){
                m_focusCode = static_cast<u32>(index + 1u);
                break;
            }
        }
    }
    const auto current = values();
    b8 geometryChanged = false;
    for(usize index = 0u; index < m_rectangles.size(); ++index)
        geometryChanged |= !SameSmokeRect(m_rectangles[index], m_lastRectangles[index]);
    if(m_sequence != 0u && !m_displayChanged && !geometryChanged && current == m_lastValues)
        return;
    ++m_sequence;
    m_lastValues = current;
    m_lastRectangles = m_rectangles;
    m_displayChanged = false;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiNestedPopupSmoke: state sequence={} values={},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{}")
        , m_sequence, current[0], current[1], current[2], current[3], current[4], current[5], current[6]
        , current[7], current[8], current[9], current[10], current[11], current[12], current[13]
        , current[14], current[15], current[16], current[17], current[18], current[19], current[20]
        , current[21], current[22], current[23], current[24], current[25], current[26], current[27], current[28]
    );
    for(usize index = 0u; index < m_rectangles.size(); ++index)
        LogSmokeRect(NWB_TEXT("UiNestedPopupSmoke"), m_sequence, __hidden_ui_nested_popup_smoke::s_RectNames[index], m_rectangles[index]);
}

void UiNestedPopupSmokeScene::paintMarkers(Impl::UiPaintContext& context)const{
    PaintKeyedSmokeValueMarkers<12u, 8u, 8u, 18u>(values(), m_sequence, context);
}

Array<u64, 29u> UiNestedPopupSmokeScene::values()const noexcept{
    const b8 parent = m_parent.isOpen();
    return { static_cast<u64>(parent), static_cast<u64>(parent && m_childDeclared && m_child.isOpen()), m_focusScope, m_focusCode,
        m_beforeClicks, m_afterClicks, m_childClicks, m_outside, static_cast<u64>(m_beforeText.text().size()),
        static_cast<u64>(m_afterText.text().size()), static_cast<u64>(m_childText.text().size()),
        m_beforeList.selectedKey(), m_afterList.selectedKey(), static_cast<u64>(parent && m_combo.isOpen()), m_combo.selectedKey(),
        static_cast<u64>(parent && m_search.combo().isOpen()), m_search.combo().selectedKey(),
        static_cast<u64>(m_search.query().text().size()), static_cast<u64>(parent && m_menu.isOpen()), m_menu.cursorKey(), m_command,
        m_source.labelReads(), static_cast<u64>(m_child.isOpen()), static_cast<u64>(m_combo.isOpen()),
        static_cast<u64>(m_search.combo().isOpen()), static_cast<u64>(m_menu.isOpen()), m_popupTargets, m_popupCount,
        static_cast<u64>(m_childParentScope) };
}

Impl::Ui::Rect UiNestedPopupSmokeScene::rowBounds(const Impl::Ui::ListState& state, const Impl::Ui::IListDataSource& source, const u64 key)const{
    const auto& placement = state.placement();
    const auto index = source.indexOf(key);
    if(!index || *index < placement.firstRow || *index >= placement.endRow)
        return {};
    return VisibleSmokeListRow(placement, *index);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 IsUiLayerNestedPopupSmokeEnabled(){
    return ReadSmokeEnvironmentFlag("NWB_UI_LAYER_NESTED_POPUP");
}

b8 IsUiLayerNestedPopupSkinSmokeEnabled(){
    return ReadSmokeEnvironmentFlag("NWB_UI_LAYER_NESTED_POPUP_SKIN");
}

SharedUiNestedPopupSmokeScene CreateUiNestedPopupSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input){
    return SharedUiNestedPopupSmokeScene(NewArenaObject<RefCounter<UiNestedPopupSmokeScene>>(arena, arena, input), ArenaRefDeleter<RefCounter<UiNestedPopupSmokeScene>, Core::Alloc::GlobalArena>(&arena), s_AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_nested_popup_source{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr Array<StringView, 5u> s_Labels{ "Alpha", "Beta", "Gamma (disabled)", "Delta", "Echo" };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


u64 UiNestedPopupSmokeSource::View::key(const u64 index)const noexcept{
    return index < rowCount() ? m_source.key(m_source.m_indices[index]) : 0u;
}

Expected<u64> UiNestedPopupSmokeSource::View::indexOf(const u64 keyValue)const noexcept{
    u64 index = 0u;
    for(u64 candidate = 0u; candidate < rowCount(); ++candidate){
        if(key(candidate) == keyValue){
            index = candidate;
            return index;
        }
    }
    return MakeUnexpected(Failure{});
}

Expected<u64> UiNestedPopupSmokeSource::View::findEnabled(const u64 start, const b8 reverse)const noexcept{
    u64 index = 0u;
    if(start >= rowCount())
        return MakeUnexpected(Failure{});
    for(u64 candidate = start; candidate < rowCount(); reverse ? --candidate : ++candidate){
        if(enabled(candidate)){
            index = candidate;
            return index;
        }
        if(reverse && candidate == 0u)
            break;
    }
    return MakeUnexpected(Failure{});
}

StringView UiNestedPopupSmokeSource::View::text(const u64 index)const noexcept{
    return index < rowCount() ? m_source.text(m_source.m_indices[index]) : StringView{};
}

b8 UiNestedPopupSmokeSource::View::enabled(const u64 index)const noexcept{
    return index < rowCount() && m_source.enabled(m_source.m_indices[index]);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UiNestedPopupSmokeSource::UiNestedPopupSmokeSource(Core::Alloc::GlobalArena& arena)noexcept
    : m_query(arena)
    , m_view(*this)
{
    const b8 initialized = filter({});
    NWB_FATAL_ASSERT(initialized);
}

Expected<u64> UiNestedPopupSmokeSource::indexOf(const u64 keyValue)const noexcept{
    u64 index = 0u;
    if(keyValue == 0u || keyValue > rowCount())
        return MakeUnexpected(Failure{});
    index = keyValue - 1u;
    return index;
}

Expected<u64> UiNestedPopupSmokeSource::findEnabled(const u64 start, const b8 reverse)const noexcept{
    return FindAdjacentEnabledRow(*this, start, reverse);
}

StringView UiNestedPopupSmokeSource::text(const u64 index)const noexcept{
    ++m_labelReads;
    return index < rowCount() ? __hidden_ui_nested_popup_source::s_Labels[index] : StringView{};
}

b8 UiNestedPopupSmokeSource::filter(const AStringView query){
    if(m_viewRevision != 0u && query == AStringView(m_query.data(), m_query.size()))
        return true;
    if(query.empty())
        m_query.clear();
    else
        m_query.assign(query.data(), query.size());
    m_count = 0u;
    for(u64 index = 0u; index < rowCount(); ++index){
        if(!ContainsAsciiIgnoreCase(__hidden_ui_nested_popup_source::s_Labels[index], query))
            continue;
        m_indices[m_count] = index;
        ++m_count;
    }
    ++m_viewRevision;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UiNumericEditSmokeScene::UiNumericEditSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input)
    : m_arena(arena)
    , m_input(input)
    , m_integer(arena)
    , m_float(arena)
    , m_clipboard(arena)
{
    resetModels();
    m_input.addHandlerToBack(*this);
}

UiNumericEditSmokeScene::~UiNumericEditSmokeScene()noexcept{
    m_input.removeHandler(*this);
}

b8 UiNumericEditSmokeScene::paint(Impl::UiPaintContext& context){
    using namespace Impl::Ui;
    Builder& ui = context.ui;
    ui.style().fontSize = 16.0f;
    ui.style().gap = 8.0f;
    if(!ui.beginPanel("numeric_edit_fixture", { 24.0f, 24.0f, 480.0f, 340.0f }))
        return false;
    const WidgetOptions title{ {}, { LayoutSizePolicy::Fixed, 24.0f } };
    const WidgetOptions caption{ {}, { LayoutSizePolicy::Fixed, 18.0f } };
    if(!ui.label("title", "Integer and decimal editors", title) || !ui.label("integer_caption", "Signed 64-bit integer", caption))
        return false;
    EditBoxOptions edit;
    edit.width = { LayoutSizePolicy::Fixed, 400.0f };
    edit.height = { LayoutSizePolicy::Fixed, 40.0f };
    edit.enabled = m_enabled;
    edit.readOnly = m_readOnly;
    IntegerEditOptions integer;
    integer.edit = edit;
    const NumericEditBoxResult first = ui.integerEdit("integer", m_integer, m_states[0u], integer);
    if(!first.edit.valid)
        return false;
    count(first.numeric);
    if(!ui.label("float_caption", "Decimal, range -10 to 10", caption))
        return false;
    FloatEditOptions floating;
    floating.edit = edit;
    floating.bounds = floatBounds();
    const NumericEditBoxResult second = ui.floatEdit("float", m_float, m_states[1u], floating);
    if(!second.edit.valid)
        return false;
    count(second.numeric);
    if(!ui.label("clipboard_caption", "Clipboard destination", caption))
        return false;
    edit.enabled = true;
    edit.readOnly = false;
    if(!ui.editBox("clipboard", m_clipboard, m_states[2u], edit).valid)
        return false;
    if(!ui.label("policy", m_clamp ? "Range policy: clamp" : "Range policy: reject", caption) || !ui.label("hint", "Enter: commit / Escape: restore", caption) || !ui.endPanel())
        return false;
    observeState(context.display, context.text);
    paintMarkers(context);
    return true;
}

::NWB::Core::KeyboardInputResult UiNumericEditSmokeScene::keyboardUpdate(const i32 key, const i32, const i32 action, const i32){
    if(key < Core::Key::F5 || key > Core::Key::F9)
        return false;
    if(action == Core::InputAction::Press){
        if(key == Core::Key::F5)
            m_readOnly = !m_readOnly;
        else if(key == Core::Key::F6)
            m_enabled = !m_enabled;
        else if(key == Core::Key::F7){
            const b8 assigned = m_integer.setValue(9007199254740995ll) && m_float.setValue(2.5);
            NWB_FATAL_ASSERT(assigned);
        }
        else if(key == Core::Key::F8)
            m_clamp = !m_clamp;
        else
            resetModels();
    }
    return true;
}

void UiNumericEditSmokeScene::resetModels(){
    const b8 reset = m_integer.setValue(7ll) && m_float.setValue(1.25) && m_clipboard.setText("");
    NWB_FATAL_ASSERT(reset);
    m_enabled = true;
    m_readOnly = false;
    m_clamp = true;
}

void UiNumericEditSmokeScene::count(const Impl::Ui::NumericEditResult& result)noexcept{
    m_commits += result.committed ? 1u : 0u;
    m_cancels += result.cancelled ? 1u : 0u;
    m_rejects += result.rejected ? 1u : 0u;
    m_clamps += result.clamped ? 1u : 0u;
    m_restored += result.restored ? 1u : 0u;
}

Impl::Ui::FloatBounds UiNumericEditSmokeScene::floatBounds()const noexcept{
    return { -10.0, 10.0, m_clamp ? Impl::Ui::NumericBoundsPolicy::Clamp : Impl::Ui::NumericBoundsPolicy::Reject };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 IsUiLayerNumericEditSmokeEnabled(){
    return ReadSmokeEnvironmentFlag("NWB_UI_LAYER_NUMERIC_EDIT");
}

b8 IsUiLayerNumericEditSkinSmokeEnabled(){
    return ReadSmokeEnvironmentFlag("NWB_UI_LAYER_NUMERIC_EDIT_SKIN");
}

SharedUiNumericEditSmokeScene CreateUiNumericEditSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input){
    return SharedUiNumericEditSmokeScene(NewArenaObject<RefCounter<UiNumericEditSmokeScene>>(arena, arena, input), ArenaRefDeleter<RefCounter<UiNumericEditSmokeScene>, Core::Alloc::GlobalArena>(&arena), s_AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_numeric_edit_snapshot{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr Array<TStringView, 12> s_RectNames = { NWB_TEXT("integer_bounds"), NWB_TEXT("integer_content"),
    NWB_TEXT("integer_caret"), NWB_TEXT("integer_selection"), NWB_TEXT("float_bounds"), NWB_TEXT("float_content"),
    NWB_TEXT("float_caret"), NWB_TEXT("float_selection"), NWB_TEXT("clipboard_bounds"), NWB_TEXT("clipboard_content"),
    NWB_TEXT("clipboard_caret"), NWB_TEXT("clipboard_selection") };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static b8 Coherent(const Impl::Ui::EditModel& model, const Impl::Ui::EditBoxState& state)noexcept{
    return state.modelGeneration == model.instanceGeneration() && state.revision == model.revision() && state.selectionGeneration == model.selectionGeneration() && state.anchor == model.anchor() && state.caret == model.caret();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Array<u64, 30u> UiNumericEditSmokeScene::values()const noexcept{
    using namespace __hidden_ui_numeric_edit_snapshot;
    const auto& integer = m_integer.draft();
    const auto& floating = m_float.draft();
    return { BitCast<u64>(m_integer.value()), BitCast<u64>(m_float.value()), static_cast<u64>(integer.text().size()),
        ComputeFnv32TextHash(integer.text()), static_cast<u64>(integer.anchor()), static_cast<u64>(integer.caret()),
        static_cast<u64>(m_integer.status()), static_cast<u64>(m_integer.dirty()), static_cast<u64>(m_states[0u].focused),
        static_cast<u64>(floating.text().size()), ComputeFnv32TextHash(floating.text()), static_cast<u64>(floating.anchor()),
        static_cast<u64>(floating.caret()), static_cast<u64>(m_float.status(floatBounds())), static_cast<u64>(m_float.dirty()),
        static_cast<u64>(m_states[1u].focused), static_cast<u64>(m_clipboard.text().size()), ComputeFnv32TextHash(m_clipboard.text()),
        static_cast<u64>(m_clipboard.anchor()), static_cast<u64>(m_clipboard.caret()), static_cast<u64>(m_states[2u].focused),
        static_cast<u64>(m_enabled), static_cast<u64>(m_readOnly), static_cast<u64>(m_clamp), m_commits, m_cancels,
        m_rejects, m_clamps, m_restored, static_cast<u64>(Coherent(integer, m_states[0u]) && Coherent(floating, m_states[1u]) && Coherent(m_clipboard, m_states[2u])) };
}

void UiNumericEditSmokeScene::observeState(const Impl::Ui::DisplayMetrics& display, Impl::Ui::TextService& text){
    using namespace Impl::Ui;
    using namespace __hidden_ui_numeric_edit_snapshot;
    UiNumericEditSnapshot current;
    current.values = values();
    current.display = display;
    const Array<NotNull<const EditModel*>, 3> models = { MakeNotNull(&m_integer.draft()), MakeNotNull(&m_float.draft()), MakeNotNull(&m_clipboard) };
    for(usize index = 0u; index < m_states.size(); ++index){
        const auto& placement = m_states[index].placement;
        current.rectangles[index * 4u] = placement.bounds;
        current.rectangles[index * 4u + 1u] = placement.content;
        current.rectangles[index * 4u + 2u] = placement.caret;
        current.rectangles[index * 4u + 3u] = CaptureEditSelection(m_arena, text, *models[index], placement, 16.0f);
    }
    b8 changed = current.values != m_snapshot.values;
    for(usize index = 0u; index < current.rectangles.size(); ++index)
        changed |= !SameSmokeRect(current.rectangles[index], m_snapshot.rectangles[index]);
    const auto& previous = m_snapshot.display;
    const b8 displayChanged = display.logicalWidth != previous.logicalWidth || display.logicalHeight != previous.logicalHeight || display.pixelScaleX != previous.pixelScaleX || display.pixelScaleY != previous.pixelScaleY;
    if(m_snapshot.sequence != 0u && !changed && !displayChanged)
        return;
    current.sequence = m_snapshot.sequence + 1u;
    m_snapshot = current;
    if(displayChanged){
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiNumericEditSmoke: display logical={}x{} scale={}x{}"), display.logicalWidth, display.logicalHeight, display.pixelScaleX, display.pixelScaleY);
    }
    const auto& value = m_snapshot.values;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiNumericEditSmoke: state sequence={} values={},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{}")
        , m_snapshot.sequence, value[0], value[1], value[2], value[3], value[4], value[5], value[6], value[7]
        , value[8], value[9], value[10], value[11], value[12], value[13], value[14], value[15]
        , value[16], value[17], value[18], value[19], value[20], value[21], value[22], value[23]
        , value[24], value[25], value[26], value[27], value[28], value[29]
    );
    for(usize index = 0u; index < m_snapshot.rectangles.size(); ++index)
        LogSmokeRect(NWB_TEXT("UiNumericEditSmoke"), m_snapshot.sequence, s_RectNames[index], m_snapshot.rectangles[index]);
}

void UiNumericEditSmokeScene::paintMarkers(Impl::UiPaintContext& context)const{
    const f32 y = context.display.logicalHeight - 18.0f;
    constexpr usize s_ValueCount = TupleSize_V<decltype(m_snapshot.values)>;
    constexpr usize s_MarkerCount = []()noexcept{
        usize count = 0u;
        for(usize index = 0u; index <= s_ValueCount; ++index)
            count += index < 2u ? 6u : index == 3u || index == 10u || index == 17u ? 3u : 2u;
        return count;
    }();
    const auto positions = BuildSmokeMarkerPositions<s_MarkerCount, 8u, 8u>();
    usize marker = 0u;
    for(usize index = 0u; index <= m_snapshot.values.size(); ++index){
        const u64 value = index == m_snapshot.values.size() ? m_snapshot.sequence : m_snapshot.values[index];
        const u32 parts = index < 2u ? 6u : index == 3u || index == 10u || index == 17u ? 3u : 2u;
        for(u32 part = 0u; part < parts; ++part){
            context.paint.fillRect({ positions[marker], y, 6.0f, 10.0f }, EncodeSmokeColor(value >> (part * 12u)));
            ++marker;
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void PaintUiLayerSmokeScene(Impl::UiPaintContext& context){
    Impl::Ui::PaintBuilder& paint = context.paint;
    const SIMDVectorSingle extent = VectorSet(context.display.logicalWidth, context.display.logicalHeight, context.display.logicalWidth, context.display.logicalHeight);
    const auto rectangle = [extent](const f32 x, const f32 y, const f32 w, const f32 h)noexcept{
        const SIMDVectorSingle scaled = VectorMultiply(VectorSet(x, y, w, h), extent);
        return Impl::Ui::Rect{ VectorGetX(scaled), VectorGetY(scaled), VectorGetZ(scaled), VectorGetW(scaled) };
    };

    // Empty margins make the layer clear and complete fullscreen presentation observable in the final framebuffer.
    paint.fillRect(rectangle(0.04f, 0.08f, 0.28f, 0.20f), { 0.0f, 0.0f, 1.0f, 1.0f });
    paint.fillRect(rectangle(0.12f, 0.12f, 0.20f, 0.12f), { 1.0f, 0.0f, 0.0f, 0.5f });
    paint.fillRect(rectangle(0.34f, 0.08f, 0.16f, 0.20f), { 1.0f, 0.0f, 0.0f, 0.5f });
    paint.fillRect(rectangle(0.54f, 0.08f, 0.18f, 0.20f), { 0.25f, 0.25f, 0.25f, 1.0f });
    if(!paint.drawRegion(Name(s_UiSmokeNameWhite), rectangle(0.76f, 0.08f, 0.20f, 0.20f), { 0.1f, 0.8f, 0.2f, 0.5f })){
        NWB_LOGGER_ERROR(NWB_TEXT("UiLayerSmokeProject: white sprite is absent from the default atlas"));
        return;
    }

    paint.fillRect(rectangle(0.04f, 0.38f, 0.28f, 0.26f), { 0.0f, 0.2f, 0.3f, 1.0f });
    paint.pushClip(rectangle(0.10f, 0.44f, 0.16f, 0.12f));
    paint.fillRect(rectangle(0.04f, 0.38f, 0.28f, 0.26f), { 0.0f, 1.0f, 0.0f, 1.0f });
    paint.pushClip(rectangle(0.14f, 0.42f, 0.16f, 0.16f));
    paint.fillRect(rectangle(0.04f, 0.38f, 0.28f, 0.26f), { 1.0f, 0.0f, 0.0f, 1.0f });
    if(!paint.popClip() || !paint.popClip()){
        NWB_LOGGER_ERROR(NWB_TEXT("UiLayerSmokeProject: nested clip stack became unbalanced"));
        return;
    }

    if(!paint.drawRegion(Name(s_UiSmokeNamePanelNormal), rectangle(0.36f, 0.40f, 0.26f, 0.24f))
        || !paint.drawRegion(Name(s_UiSmokeNameButtonNormal), rectangle(0.66f, 0.40f, 0.13f, 0.14f))
        || !paint.drawRegion(Name(s_UiSmokeNameButtonHover), rectangle(0.82f, 0.40f, 0.14f, 0.14f))){
        NWB_LOGGER_ERROR(NWB_TEXT("UiLayerSmokeProject: named nine-slice regions are absent from the default atlas"));
        return;
    }

    paint.fillRect(rectangle(0.64f, 0.64f, 0.16f, 0.16f), { 0.0f, 0.1f, 0.2f, 1.0f });
    if(!paint.drawRegion(Name(s_UiSmokeNameComboArrow), rectangle(0.68f, 0.68f, 0.08f, 0.08f)))
        NWB_LOGGER_ERROR(NWB_TEXT("UiLayerSmokeProject: combo arrow sprite is absent from the default atlas"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 HandlePassiveSmokeKeyboard(const i32 key, const i32 action, b8& phase, Impl::Ui::PopupState& parent, Impl::Ui::PopupState& child)noexcept{
    if(key != Core::Key::F4 && key != Core::Key::F5)
        return false;
    if(action != Core::InputAction::Press)
        return true;
    if(key == Core::Key::F4)
        phase = !phase;
    else if(parent.isOpen()){
        child.close();
        parent.close();
    }
    else{
        parent.open();
        child.open();
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_popup_smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr TStringView s_ModalLabel = NWB_TEXT("modal");
constexpr AStringView s_ModalKey = "modal";
constexpr TStringView s_OutsideLabel = NWB_TEXT("outside");
constexpr AStringView s_OutsideKey = "outside";
constexpr TStringView s_PopupLabel = NWB_TEXT("popup");
constexpr AStringView s_PopupKey = "popup";
constexpr TStringView s_FirstLabel = NWB_TEXT("first");
constexpr AStringView s_FirstKey = "first";
constexpr TStringView s_SecondLabel = NWB_TEXT("second");
constexpr AStringView s_SecondKey = "second";
constexpr TStringView s_LogTag = NWB_TEXT("UiPopupSmoke");
static constexpr Impl::Ui::Rect s_Panel{ 24.0f, 24.0f, 320.0f, 216.0f };
static constexpr Impl::Ui::Rect s_Anchor{ 200.0f, 64.0f, 120.0f, 36.0f };
static constexpr Impl::Ui::Rect s_Trigger{ 32.0f, 64.0f, 120.0f, 36.0f };
static constexpr Impl::Ui::Rect s_ModalTrigger{ 160.0f, 64.0f, 120.0f, 36.0f };
static constexpr Impl::Ui::Rect s_Counter{ 32.0f, 112.0f, 130.0f, 36.0f };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UiPopupSmokeScene::UiPopupSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input)
    : m_input(input)
    , m_popupText(arena)
    , m_outsideText(arena)
{
    const b8 initialized = m_popupText.setText("Popup") && m_outsideText.setText("Outside focus sentinel");
    NWB_FATAL_ASSERT_MSG(initialized, NWB_TEXT("Popup smoke text models must contain valid UTF8"));
    m_input.addHandlerToBack(*this);
}

UiPopupSmokeScene::~UiPopupSmokeScene()noexcept{
    m_input.removeHandler(*this);
}

b8 UiPopupSmokeScene::paint(Impl::UiPaintContext& context){
    using namespace Impl::Ui;
    ObserveSmokeDisplay(__hidden_ui_popup_smoke::s_LogTag, context.display, m_lastDisplay, m_displayChanged);
    Builder& ui = context.ui;
    ui.style().fontSize = 12.0f;
    ui.style().gap = 8.0f;
    if(!ui.beginPanel("popup_fixture", __hidden_ui_popup_smoke::s_Panel))
        return false;
    const WidgetOptions title{ {}, { LayoutSizePolicy::Fixed, 24.0f } };
    if(!ui.label("title", "Popup focus and overlay fixture", title))
        return false;
    ContainerOptions row;
    row.height = { LayoutSizePolicy::Fixed, 40.0f };
    row.gap = 8.0f;
    if(!ui.beginRow("triggers", row))
        return false;
    const WidgetOptions trigger{ { LayoutSizePolicy::Fixed, 120.0f }, { LayoutSizePolicy::Fixed, 36.0f } };
    if(ui.button("open", "Open popup", trigger)){
        m_modal = false;
        m_popup.open();
    }
    if(ui.button(__hidden_ui_popup_smoke::s_ModalKey, "Open modal", trigger)){
        m_modal = true;
        m_popup.open();
    }
    if(!ui.endContainer())
        return false;
    const WidgetOptions counter{ { LayoutSizePolicy::Fixed, 130.0f }, { LayoutSizePolicy::Fixed, 36.0f } };
    if(ui.button("underlying", "Underlying", counter))
        ++m_underlying;
    EditBoxOptions edit;
    edit.width = { LayoutSizePolicy::Fixed, 260.0f };
    edit.height = { LayoutSizePolicy::Fixed, 32.0f };
    const EditBoxResult outside = ui.editBox(__hidden_ui_popup_smoke::s_OutsideKey, m_outsideText, m_outsideEdit, edit);
    m_outsideFocused = outside.focused;
    if(!outside.valid || !ui.endPanel())
        return false;

    PopupOptions options;
    options.anchor = __hidden_ui_popup_smoke::s_Anchor;
    options.size = { 220.0f, 236.0f };
    options.modal = m_modal;
    options.dismissOutside = !m_modal;
    if(m_edge)
        options.anchor = Impl::Ui::StoreRect(VectorSubtract(VectorSet(context.display.logicalWidth, context.display.logicalHeight, 48.0f, 24.0f), VectorSet(60.0f, 48.0f, 0.0f, 0.0f)));
    options.side = m_right ? PopupPlacementSide::Right : PopupPlacementSide::Below;
    m_popupFocused = false;
    if(m_visible && ui.beginPopup(__hidden_ui_popup_smoke::s_PopupKey, m_popup, options)){
        const WidgetOptions caption{ {}, { LayoutSizePolicy::Fixed, 20.0f } };
        const WidgetOptions choice{ { LayoutSizePolicy::Stretch, 1.0f }, { LayoutSizePolicy::Fixed, 32.0f } };
        if(!ui.label("caption", "Choose, edit, or dismiss", caption))
            return false;
        if(ui.button(__hidden_ui_popup_smoke::s_FirstKey, "First choice", choice)){
            m_selected = 1u;
            m_popup.close();
        }
        if(ui.button(__hidden_ui_popup_smoke::s_SecondKey, "Second choice", choice)){
            m_selected = 2u;
            m_popup.close();
        }
        if(ui.checkbox("checked", "Popup checkbox", m_checked, choice))
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiPopupSmoke: checked={}"), static_cast<u32>(m_checked));
        edit.width = {};
        const EditBoxResult popupEdit = ui.editBox("text", m_popupText, m_popupEdit, edit);
        m_popupFocused = popupEdit.focused;
        if(!popupEdit.valid || !ui.endPopup())
            return false;
    }
    observeState();
    PaintSingleSmokeValueMarkers(values(), m_sequence, context);
    return true;
}

b8 UiPopupSmokeScene::paintLaterRoot(Impl::UiPaintContext& context)const{
    using namespace Impl::Ui;
    Rect bounds = m_popup.placement().bounds;
    if(bounds.width <= 0.0f)
        bounds = { 200.0f, 104.0f, 220.0f, 236.0f };
    if(!context.ui.beginPanel("later_root", Impl::Ui::StoreRect(VectorAdd(VectorSet(bounds.x, bounds.y, bounds.width, bounds.height), VectorSet(-8.0f, -8.0f, 16.0f, 16.0f)))))
        return false;
    if(!context.ui.endPanel())
        return false;
    context.paint.fillRect(bounds, { 0.72f, 0.02f, 0.04f, 1.0f });
    context.paint.fillRect({ context.display.logicalWidth - 40.0f, 24.0f, 16.0f, 16.0f }, { 0.12f, 0.52f, 0.86f, 1.0f });
    return true;
}

::NWB::Core::KeyboardInputResult UiPopupSmokeScene::keyboardUpdate(const i32 key, const i32, const i32 action, const i32)noexcept{
    if(key != Core::Key::F5 && key != Core::Key::F6 && key != Core::Key::F7)
        return false;
    if(action == Core::InputAction::Press){
        if(key == Core::Key::F5)
            m_edge = !m_edge;
        else if(key == Core::Key::F6)
            m_right = !m_right;
        else{
            m_visible = !m_visible;
            if(m_visible)
                m_popup.close();
        }
    }
    return true;
}

Array<u32, 12u> UiPopupSmokeScene::values()const noexcept{
    return { static_cast<u32>(m_visible && m_popup.isOpen()), static_cast<u32>(m_modal), m_underlying, m_selected,
        static_cast<u32>(m_checked), static_cast<u32>(m_popupFocused), static_cast<u32>(m_outsideFocused),
        static_cast<u32>(m_edge), static_cast<u32>(m_right), static_cast<u32>(m_visible),
        static_cast<u32>(m_popupText.text().size()), static_cast<u32>(m_popup.placement().side) };
}

void UiPopupSmokeScene::observeState(){
    const auto current = values();
    const auto& placement = m_popup.placement();
    if(m_sequence != 0u && !m_displayChanged && current == m_lastValues && SameSmokeRect(placement.bounds, m_lastPlacement.bounds) && SameSmokeRect(placement.viewport, m_lastPlacement.viewport))
        return;
    ++m_sequence;
    m_lastValues = current;
    m_lastPlacement = placement;
    m_displayChanged = false;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiPopupSmoke: state sequence={} values={},{},{},{},{},{},{},{},{},{},{},{}")
        , m_sequence, current[0], current[1], current[2], current[3], current[4], current[5]
        , current[6], current[7], current[8], current[9], current[10], current[11]
    );
    LogSmokeRect(__hidden_ui_popup_smoke::s_LogTag, m_sequence, __hidden_ui_popup_smoke::s_PopupLabel, placement.bounds);
    LogSmokeRect(__hidden_ui_popup_smoke::s_LogTag, m_sequence, NWB_TEXT("viewport"), placement.viewport);
    LogSmokeRect(__hidden_ui_popup_smoke::s_LogTag, m_sequence, NWB_TEXT("trigger"), __hidden_ui_popup_smoke::s_Trigger);
    LogSmokeRect(__hidden_ui_popup_smoke::s_LogTag, m_sequence, __hidden_ui_popup_smoke::s_ModalLabel, __hidden_ui_popup_smoke::s_ModalTrigger);
    LogSmokeRect(__hidden_ui_popup_smoke::s_LogTag, m_sequence, NWB_TEXT("counter"), __hidden_ui_popup_smoke::s_Counter);
    LogSmokeRect(__hidden_ui_popup_smoke::s_LogTag, m_sequence, __hidden_ui_popup_smoke::s_OutsideLabel, m_outsideEdit.placement.bounds);
    const Impl::Ui::Rect first = Impl::Ui::StoreRect(VectorAdd(VectorSet(placement.bounds.x, placement.bounds.y, placement.bounds.width, 0.0f), VectorSet(8.0f, 36.0f, -16.0f, 32.0f)));
    LogSmokeRect(__hidden_ui_popup_smoke::s_LogTag, m_sequence, __hidden_ui_popup_smoke::s_FirstLabel, first);
    LogSmokeRect(__hidden_ui_popup_smoke::s_LogTag, m_sequence, __hidden_ui_popup_smoke::s_SecondLabel, { first.x, first.y + 40.0f, first.width, first.height });
    LogSmokeRect(__hidden_ui_popup_smoke::s_LogTag, m_sequence, NWB_TEXT("check"), { first.x, first.y + 80.0f, first.width, first.height });
    LogSmokeRect(__hidden_ui_popup_smoke::s_LogTag, m_sequence, NWB_TEXT("edit"), m_popupEdit.placement.bounds);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 IsUiLayerPopupSmokeEnabled(){
    return ReadSmokeEnvironmentFlag("NWB_UI_LAYER_POPUP");
}

b8 IsUiLayerPopupSkinSmokeEnabled(){
    return ReadSmokeEnvironmentFlag("NWB_UI_LAYER_POPUP_SKIN");
}

SharedUiPopupSmokeScene CreateUiPopupSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input){
    return SharedUiPopupSmokeScene(NewArenaObject<RefCounter<UiPopupSmokeScene>>(arena, arena, input), ArenaRefDeleter<RefCounter<UiPopupSmokeScene>, Core::Alloc::GlobalArena>(&arena), s_AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_popup_tools_smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr TStringView s_AnchorLabel = NWB_TEXT("anchor");
constexpr AStringView s_AnchorKey = "anchor";
constexpr TStringView s_SentinelLabel = NWB_TEXT("sentinel");
constexpr AStringView s_SentinelKey = "sentinel";
constexpr TStringView s_LogTag = NWB_TEXT("UiPopupToolsSmoke");
static constexpr Impl::Ui::Rect s_Panel{ 24.0f, 24.0f, 400.0f, 200.0f };
static constexpr Impl::Ui::Rect s_Anchor{ 32.0f, 64.0f, 220.0f, 36.0f };
static constexpr Impl::Ui::Rect s_Counter{ 456.0f, 32.0f, 130.0f, 36.0f };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UiPopupToolsSmokeScene::UiPopupToolsSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input)
    : m_input(input)
    , m_sentinel(arena)
{
    const b8 initialized = m_sentinel.setText("Sentinel");
    NWB_FATAL_ASSERT(initialized);
    m_input.addHandlerToBack(*this);
}

UiPopupToolsSmokeScene::~UiPopupToolsSmokeScene()noexcept{
    m_input.removeHandler(*this);
}

b8 UiPopupToolsSmokeScene::paint(Impl::UiPaintContext& context){
    using namespace Impl::Ui;
    ObserveSmokeDisplay(__hidden_ui_popup_tools_smoke::s_LogTag, context.display, m_lastDisplay, m_displayChanged);
    m_source.beginFrame();
    m_sentinelFocused = false;
    Builder& ui = context.ui;
    ui.style().fontSize = 12.0f;
    ui.style().gap = 8.0f;
    if(!ui.beginPanel("popup_tools_fixture", __hidden_ui_popup_tools_smoke::s_Panel))
        return false;
    const WidgetOptions caption{ {}, { LayoutSizePolicy::Fixed, 24.0f } };
    if(!ui.label("title", "Delayed tooltip and five context commands", caption))
        return false;
    WidgetOptions anchor{ { LayoutSizePolicy::Fixed, 220.0f }, { LayoutSizePolicy::Fixed, 36.0f } };
    anchor.enabled = m_enabled;
    if(ui.button(__hidden_ui_popup_tools_smoke::s_AnchorKey, "Right-click / Menu / Shift+F10", anchor))
        ++m_anchorClicks;
    TooltipOptions tooltip;
    constexpr f32 s_TooltipDelaySeconds = 8.0f;
    tooltip.delaySeconds = s_TooltipDelaySeconds;
    if(!ui.tooltip("hint", __hidden_ui_popup_tools_smoke::s_AnchorKey, "Open commands here; Delete is disabled.", m_tooltip, tooltip))
        return false;
    ContextMenuOptions menu;
    menu.size = { 220.0f, 180.0f };
    const ContextMenuResult result = ui.contextMenu("commands", __hidden_ui_popup_tools_smoke::s_AnchorKey, m_source, m_menu, menu);
    if(!result.valid)
        return false;
    if(result.activated){
        m_command = result.key;
        ++m_commits;
    }
    if(!ui.label("keys", "F5 reorder / F6 replace source / F9 disabled", caption))
        return false;
    EditBoxOptions edit;
    edit.width = { LayoutSizePolicy::Fixed, 260.0f };
    edit.height = { LayoutSizePolicy::Fixed, 36.0f };
    const EditBoxResult sentinel = ui.editBox(__hidden_ui_popup_tools_smoke::s_SentinelKey, m_sentinel, m_sentinelState, edit);
    m_sentinelFocused = sentinel.focused;
    if(!sentinel.valid || !ui.endPanel())
        return false;
    if(!ui.beginPanel("outside_fixture", { 448.0f, 24.0f, 146.0f, 52.0f }))
        return false;
    const WidgetOptions counter{ { LayoutSizePolicy::Fixed, 130.0f }, { LayoutSizePolicy::Fixed, 36.0f } };
    if(ui.button("underlying", "Outside counter", counter))
        ++m_underlying;
    if(!ui.endPanel())
        return false;
    observeState();
    PaintKeyedSmokeValueMarkers<16u, 12u>(values(), m_sequence, context);
    return true;
}

::NWB::Core::KeyboardInputResult UiPopupToolsSmokeScene::keyboardUpdate(const i32 key, const i32, const i32 action, const i32)noexcept{
    if(key != Core::Key::F5 && key != Core::Key::F6 && key != Core::Key::F9)
        return false;
    if(action == Core::InputAction::Press){
        if(key == Core::Key::F5)
            m_source.reverse();
        else if(key == Core::Key::F6)
            m_source.replace();
        else
            m_enabled = !m_enabled;
    }
    return true;
}

Array<u64, 15u> UiPopupToolsSmokeScene::values()const noexcept{
    return { static_cast<u64>(m_tooltip.visible()), static_cast<u64>(m_menu.isOpen()), m_menu.cursorKey(), m_command,
        m_commits, m_anchorClicks, m_underlying, static_cast<u64>(m_sentinelFocused), static_cast<u64>(m_enabled),
        static_cast<u64>(m_source.reversed()), m_source.revision(), m_source.instanceGeneration(), m_source.labelReads(),
        static_cast<u64>(m_sentinel.text().size()), m_menu.listState().selectedKey() };
}

Impl::Ui::Rect UiPopupToolsSmokeScene::rowBounds(const u64 key)const noexcept{
    const auto& placement = m_menu.listState().placement();
    if(!m_menu.isOpen())
        return {};
    const auto index = m_source.indexOf(key);
    if(!index || *index < placement.firstRow || *index >= placement.endRow)
        return {};
    return VisibleSmokeListRow(placement, *index);
}

void UiPopupToolsSmokeScene::observeState(){
    const auto current = values();
    const auto& menu = m_menu.placement();
    const auto& tooltip = m_tooltip.placement();
    const auto& sentinel = m_sentinelState.placement.bounds;
    const auto& list = m_menu.listState().placement();
    if(
        m_sequence != 0u && !m_displayChanged && current == m_lastValues
        && list.offset == m_lastList.offset && list.contentHeight == m_lastList.contentHeight
        && SameSmokeRect(list.viewport, m_lastList.viewport) && SameSmokeRect(menu.bounds, m_lastMenu.bounds)
        && SameSmokeRect(tooltip.bounds, m_lastTooltip.bounds) && SameSmokeRect(sentinel, m_lastSentinel)
    )
        return;
    ++m_sequence;
    m_lastValues = current;
    m_lastMenu = menu;
    m_lastList = list;
    m_lastTooltip = tooltip;
    m_lastSentinel = sentinel;
    m_displayChanged = false;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiPopupToolsSmoke: state sequence={} values={},{},{},{},{},{},{},{},{},{},{},{},{},{},{}")
        , m_sequence, current[0], current[1], current[2], current[3], current[4], current[5], current[6], current[7]
        , current[8], current[9], current[10], current[11], current[12], current[13], current[14]
    );
    LogSmokeRect(__hidden_ui_popup_tools_smoke::s_LogTag, m_sequence, __hidden_ui_popup_tools_smoke::s_AnchorLabel, __hidden_ui_popup_tools_smoke::s_Anchor);
    LogSmokeRect(__hidden_ui_popup_tools_smoke::s_LogTag, m_sequence, NWB_TEXT("menu"), menu.bounds);
    LogSmokeRect(__hidden_ui_popup_tools_smoke::s_LogTag, m_sequence, NWB_TEXT("tooltip"), tooltip.bounds);
    LogSmokeRect(__hidden_ui_popup_tools_smoke::s_LogTag, m_sequence, NWB_TEXT("list"), list.bounds);
    LogSmokeRect(__hidden_ui_popup_tools_smoke::s_LogTag, m_sequence, NWB_TEXT("viewport"), list.viewport);
    LogSmokeRect(__hidden_ui_popup_tools_smoke::s_LogTag, m_sequence, NWB_TEXT("first"), rowBounds(1u));
    LogSmokeRect(__hidden_ui_popup_tools_smoke::s_LogTag, m_sequence, NWB_TEXT("second"), rowBounds(2u));
    LogSmokeRect(__hidden_ui_popup_tools_smoke::s_LogTag, m_sequence, NWB_TEXT("disabled"), rowBounds(3u));
    LogSmokeRect(__hidden_ui_popup_tools_smoke::s_LogTag, m_sequence, NWB_TEXT("last"), rowBounds(5u));
    LogSmokeRect(__hidden_ui_popup_tools_smoke::s_LogTag, m_sequence, NWB_TEXT("cursor_row"), rowBounds(m_menu.cursorKey()));
    LogSmokeRect(__hidden_ui_popup_tools_smoke::s_LogTag, m_sequence, __hidden_ui_popup_tools_smoke::s_SentinelLabel, sentinel);
    LogSmokeRect(__hidden_ui_popup_tools_smoke::s_LogTag, m_sequence, NWB_TEXT("counter"), __hidden_ui_popup_tools_smoke::s_Counter);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 IsUiLayerPopupToolsSmokeEnabled(){
    return ReadSmokeEnvironmentFlag("NWB_UI_LAYER_POPUP_TOOLS");
}

b8 IsUiLayerPopupToolsSkinSmokeEnabled(){
    return ReadSmokeEnvironmentFlag("NWB_UI_LAYER_POPUP_TOOLS_SKIN");
}

SharedUiPopupToolsSmokeScene CreateUiPopupToolsSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input){
    return SharedUiPopupToolsSmokeScene(NewArenaObject<RefCounter<UiPopupToolsSmokeScene>>(arena, arena, input), ArenaRefDeleter<RefCounter<UiPopupToolsSmokeScene>, Core::Alloc::GlobalArena>(&arena), s_AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_popup_tools_source{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr Array<StringView, 5u> s_Labels{ "Inspect", "Duplicate", "Delete (disabled)", "Rename", "Reset view" };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


u64 UiPopupToolsSmokeSource::key(const u64 index)const noexcept{
    return index < rowCount() ? UiWidgetTests::ArithmeticFixtureRowKey(rowCount(), 0u, m_reversed, index) : 0u;
}

Expected<u64> UiPopupToolsSmokeSource::indexOf(const u64 keyValue)const noexcept{
    return UiWidgetTests::FindArithmeticFixtureRowIndex(rowCount(), 0u, m_reversed, keyValue);
}

Expected<u64> UiPopupToolsSmokeSource::findEnabled(const u64 start, const b8 reverse)const noexcept{
    return FindAdjacentEnabledRow(*this, start, reverse);
}

StringView UiPopupToolsSmokeSource::text(const u64 index)const noexcept{
    ++m_labelReads;
    const u64 rowKey = key(index);
    return rowKey == 0u ? StringView{} : __hidden_ui_popup_tools_source::s_Labels[rowKey - 1u];
}

void UiPopupToolsSmokeSource::reverse()noexcept{
    m_reversed = !m_reversed;
    ++m_revision;
}

void UiPopupToolsSmokeSource::replace()noexcept{
    ++m_generation;
    ++m_revision;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_progress_smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr AStringView s_TitleKey = "title";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UiProgressSmokeScene::UiProgressSmokeScene(Core::InputDispatcher& input)
    : m_input(input)
{
    m_input.addHandlerToBack(*this);
}

UiProgressSmokeScene::~UiProgressSmokeScene()noexcept{
    m_input.removeHandler(*this);
}

b8 UiProgressSmokeScene::paint(Impl::UiPaintContext& context){
    auto& ui = context.ui;
    ui.style().fontSize = 14.0f;
    ui.style().gap = 8.0f;
    ui.progressStyle().padding = { 8.0f, 8.0f, 8.0f, 8.0f };
    ui.progressStyle().trackTint = {};
    ui.progressStyle().fillTint = {};
    m_declared = {};
    const PassiveSmokeColumns columns = ResolvePassiveSmokeColumns(context.display);
    const f32 width = columns.mainWidth;
    const f32 right = columns.rightColumn;
    const f32 otherWidth = columns.rightWidth;
    if(!paintMain(ui, { 24.0f, 24.0f, width, 360.0f }) || !paintFrozen(ui, { right, 24.0f, otherWidth, 120.0f }) || !paintExternal(context, { right, 176.0f, otherWidth, 84.0f }) || !paintPopups(ui, right) || ui.failed())
        return false;
    observeState(context);
    paintMarkers(context);
    return true;
}

::NWB::Core::KeyboardInputResult UiProgressSmokeScene::keyboardUpdate(const i32 key, const i32, const i32 action, const i32)noexcept{
    return HandlePassiveSmokeKeyboard(key, action, m_phase, m_parent, m_child);
}

b8 UiProgressSmokeScene::paintMain(Impl::Ui::Builder& ui, const Impl::Ui::Rect& bounds){
    using namespace Impl::Ui;
    if(!ui.beginPanel("progress_main", bounds))
        return false;
    const WidgetOptions caption{ {}, { LayoutSizePolicy::Fixed, 16.0f } };
    const WidgetOptions button{ { LayoutSizePolicy::Stretch, 1.0f }, { LayoutSizePolicy::Fixed, 28.0f } };
    if(!ui.label(__hidden_ui_progress_smoke::s_TitleKey, "Passive progress / bounded skin fill", caption))
        return false;
    if(ui.button("before", "Before progress", button))
        ++m_beforeClicks;
    constexpr Array<StringView, 6u> s_Keys{ "zero", "quarter", "full", "below", "above", "tiny" };
    constexpr Array<f64, 6u> s_Fractions{ 0.0, 0.25, 1.0, -2.0, 3.0, 0.01 };
    const SIMDVectorSingle row = VectorAdd(VectorSet(bounds.x, bounds.y, bounds.width, 0.0f), VectorSet(8.0f, 68.0f, -16.0f, 32.0f));
    for(usize index = 0u; index < s_Keys.size(); ++index){
        if(!ui.progress(s_Keys[index], s_Fractions[index]))
            return false;
        m_declared.rectangles[index + 2u] = { VectorGetX(row), VectorGetY(row) + static_cast<f32>(index) * 40.0f, VectorGetZ(row), VectorGetW(row) };
    }
    if(ui.button("after", "After progress", button))
        ++m_afterClicks;
    return ui.endPanel();
}

b8 UiProgressSmokeScene::paintFrozen(Impl::Ui::Builder& ui, const Impl::Ui::Rect& bounds){
    using namespace Impl::Ui;
    if(!ui.beginPanel("progress_frozen", bounds))
        return false;
    const WidgetOptions caption{ {}, { LayoutSizePolicy::Fixed, 16.0f } };
    if(!ui.label(__hidden_ui_progress_smoke::s_TitleKey, "Declaration owns value, options and style", caption))
        return false;
    const ProgressStyle original = ui.progressStyle();
    f64 fraction = m_phase ? 0.75 : 0.25;
    m_declared.bits[0u] = BitCast<u64>(fraction);
    ProgressOptions options;
    if(!ui.progress("frozen", fraction, options))
        return false;
    m_declared.rectangles[8u] = Impl::Ui::StoreRect(VectorAdd(VectorSet(bounds.x, bounds.y, bounds.width, 0.0f), VectorSet(8.0f, 32.0f, -16.0f, 32.0f)));
    fraction = m_phase ? 0.25 : 0.75;
    m_declared.bits[1u] = BitCast<u64>(fraction);
    options.height = 70.0f;
    options.width = { LayoutSizePolicy::Fixed, 70.0f };
    ui.progressStyle().padding = { 20.0f, 20.0f, 20.0f, 20.0f };
    ui.progressStyle().fillTint = { 0.0f, 0.0f, 0.0f, 1.0f };
    const b8 painted = ui.endPanel();
    ui.progressStyle() = original;
    return painted;
}

b8 UiProgressSmokeScene::paintExternal(Impl::UiPaintContext& context, const Impl::Ui::Rect& bounds){
    using namespace Impl::Ui;
    auto& ui = context.ui;
    const SIMDVectorSingle content = VectorAdd(VectorSet(bounds.x, bounds.y, bounds.width, 0.0f), VectorSet(8.0f, 8.0f, -16.0f, 32.0f));
    m_declared.rectangles[9u] = Impl::Ui::StoreRect(content);
    const Rect clip = Impl::Ui::StoreRect(VectorMultiply(content, VectorSet(1.0f, 1.0f, 0.5f, 1.0f)));
    m_declared.rectangles[10u] = clip;
    context.paint.fillRect(bounds, { 0.07f, 0.13f, 0.2f, 1.0f });
    context.paint.pushClip(clip);
    const b8 painted = ui.beginPanel("progress_external", bounds) && ui.progress("external", 0.75) && ui.endPanel();
    const b8 popped = context.paint.popClip();
    return painted && popped;
}

b8 UiProgressSmokeScene::paintPopups(Impl::Ui::Builder& ui, const f32 right){
    using namespace Impl::Ui;
    PopupOptions parent;
    parent.anchor = { right, 330.0f, 8.0f, 24.0f };
    parent.size = { 280.0f, 120.0f };
    if(!ui.beginPopup("progress_parent", m_parent, parent))
        return !ui.failed();
    const WidgetOptions button{ { LayoutSizePolicy::Stretch, 1.0f }, { LayoutSizePolicy::Fixed, 28.0f } };
    if(ui.button("parent_action", "Parent action", button))
        m_parent.close();
    if(!ui.progress("parent_value", 0.25))
        return false;
    PopupOptions child;
    child.anchor = { right + 100.0f, 392.0f, 8.0f, 16.0f };
    child.size = { 240.0f, 120.0f };
    if(ui.beginPopup("progress_child", m_child, child)){
        if(!ui.progress("child_value", 0.75))
            return false;
        if(ui.button("child_action", "Child action", button))
            m_child.close();
        if(!ui.endPopup())
            return false;
    }
    if(ui.failed() || !ui.endPopup())
        return false;
    const Rect& parentBounds = m_parent.placement().bounds;
    m_declared.rectangles[13u] = parentBounds;
    m_declared.rectangles[11u] = Impl::Ui::StoreRect(VectorAdd(VectorSet(parentBounds.x, parentBounds.y, parentBounds.width, 0.0f), VectorSet(8.0f, 44.0f, -16.0f, 32.0f)));
    if(m_child.isOpen()){
        const Rect& childBounds = m_child.placement().bounds;
        m_declared.rectangles[14u] = childBounds;
        m_declared.rectangles[12u] = Impl::Ui::StoreRect(VectorAdd(VectorSet(childBounds.x, childBounds.y, childBounds.width, 0.0f), VectorSet(8.0f, 8.0f, -16.0f, 32.0f)));
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 IsUiLayerProgressSmokeEnabled(){
    return ReadSmokeEnvironmentFlag("NWB_UI_LAYER_PROGRESS");
}

b8 IsUiLayerProgressSkinSmokeEnabled(){
    return ReadSmokeEnvironmentFlag("NWB_UI_LAYER_PROGRESS_SKIN");
}

SharedUiProgressSmokeScene CreateUiProgressSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input){
    return SharedUiProgressSmokeScene(NewArenaObject<RefCounter<UiProgressSmokeScene>>(arena, input), ArenaRefDeleter<RefCounter<UiProgressSmokeScene>, Core::Alloc::GlobalArena>(&arena), s_AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_progress_snapshot{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr Array<TStringView, 15u> s_RectNames{
    NWB_TEXT("before"), NWB_TEXT("after"), NWB_TEXT("zero"), NWB_TEXT("quarter"), NWB_TEXT("full"),
    NWB_TEXT("below"), NWB_TEXT("above"), NWB_TEXT("tiny"), NWB_TEXT("frozen"), NWB_TEXT("external"),
    NWB_TEXT("external_clip"), NWB_TEXT("parent_progress"), NWB_TEXT("child_progress"), NWB_TEXT("parent"), NWB_TEXT("child")
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void UiProgressSmokeScene::observeState(Impl::UiPaintContext& context){
    using namespace Impl::Ui;
    using namespace __hidden_ui_progress_snapshot;
    UiProgressSnapshot current = m_declared;
    current.display = context.display;
    const InputRouter& input = context.ui.input();
    const auto observation = ObservePassiveSmokeTargets(input, m_parent.instanceGeneration(), m_child.instanceGeneration(), 2u, 13u, [](const usize index)noexcept{ return index != 10u; }, current.rectangles);
    const u32 focusCode = observation.focusCode;
    const u32 progressTargets = observation.passiveTargets;
    current.values = { static_cast<u64>(m_phase), focusCode, m_beforeClicks, m_afterClicks, static_cast<u64>(m_parent.isOpen()), static_cast<u64>(m_child.isOpen()), input.popupCount(), progressTargets };
    const auto update = PublishPassiveSmokeSnapshot([&]()noexcept{ return current.values != m_snapshot.values || current.bits != m_snapshot.bits; }, current, m_snapshot);
    if(!update.published)
        return;
    if(update.displayChanged){
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiProgressSmoke: display logical={}x{} scale={}x{}"), current.display.logicalWidth, current.display.logicalHeight, current.display.pixelScaleX, current.display.pixelScaleY);
    }
    const auto& value = current.values;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiProgressSmoke: state sequence={} values={},{},{},{},{},{},{},{} bits={},{}"), current.sequence, value[0], value[1], value[2], value[3], value[4], value[5], value[6], value[7], current.bits[0], current.bits[1]);
    for(usize index = 0u; index < current.rectangles.size(); ++index)
        LogSmokeRect(NWB_TEXT("UiProgressSmoke"), current.sequence, s_RectNames[index], current.rectangles[index]);
}

void UiProgressSmokeScene::paintMarkers(Impl::UiPaintContext& context)const{
    PaintSmokeValueMarkers(m_snapshot.values, m_snapshot.bits, m_snapshot.sequence, context);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u64 s_ResizePreparationPresentations = 4u;

static b8 CheckPersistentStateSelfFilter(ProjectRuntimeContext& context){
    Core::Alloc::ScratchArena scratch{Name(s_UiSmokeNameTestsUiLayerPersistentState)};
    Core::BufferDesc desc;
    desc.setDebugName(Name(s_UiSmokeNameTestsUiLayerPersistentStateBuffer)).setByteSize(16u);
    const auto first = context.graphics.createBuffer(desc);
    const auto second = context.graphics.createBuffer(desc);
    auto& device = context.graphics.getDevice();
    const auto list = device.createCommandList();
    if(!first || !second || !list)
        return false;

    Core::CommandListResourceStateHandoff state(context.objectArena);
    (*list)->open();
    (*list)->beginTrackingBufferState(first->get(), Core::ResourceStates::CopyDest);
    (*list)->beginTrackingBufferState(second->get(), Core::ResourceStates::CopyDest);
    (*list)->close(&state);
    const Core::GpuPhysicalQueueId queue = device.getPrimaryPhysicalQueue(Core::CommandQueue::Graphics);
    if(!state.valid() || !queue.valid() || (*list)->commandRecordingFailed())
        return false;

    Core::GpuPersistentResourceStateCache cache(context.objectArena);
    Core::GpuPersistentResourceStateCache::Candidate candidate(cache);
    const Array<Core::BufferHandle, 2u> buffers{ *first, *second };
    if(!cache.buildFilteredBufferSubset(state, buffers, scratch, candidate) || !candidate.source() || cache.valid())
        return false;
    if(!cache.buildFilteredBufferSubset(*candidate.source(), Span<const Core::BufferHandle>(buffers.data(), 1u), scratch, candidate))
        return false;
    const auto* filtered = candidate.source();
    if(!filtered || !filtered->coversBufferWithOwnership(first->get(), queue, {}) || filtered->coversBufferWithOwnership(second->get(), queue, {}) || cache.valid())
        return false;
    if(!cache.commit(candidate) || candidate.valid() || !cache.source())
        return false;
    return cache.source()->coversBufferWithOwnership(first->get(), queue, {}) && !cache.source()->coversBufferWithOwnership(second->get(), queue, {});
}


UiLayerSmokeProject::UiLayerSmokeProject(NotNullUniquePtr<Core::ECS::World> world, ProjectRuntimeContext& context)noexcept
    : m_world(Move(world))
    , m_context(context)
    , m_resizeRenderPass(context.graphics)
{}

UiLayerSmokeProject::~UiLayerSmokeProject()noexcept{
    destroyRuntime();
}

b8 UiLayerSmokeProject::onStartup(){
    if(ReadSmokeEnvironmentFlag("NWB_UI_LAYER_PERSISTENT_STATE_TEST")){
        if(!CheckPersistentStateSelfFilter(m_context)){
            NWB_LOGGER_ERROR(NWB_TEXT("UiLayerSmokeProject: persistent state self-filter failed"));
            return false;
        }
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiLayerSmokeProject: persistent state self-filter passed"));
    }
    auto& layer = *MakeNotNull(m_world->getSystem<Impl::UiLayerSystem>());
    m_context.graphics.addRenderPassToBack(layer);
    if(ReadSmokeEnvironmentFlag("NWB_UI_LAYER_RESIZE_RENDER")){
        m_resizePresentationBaseline = m_context.graphics.getSuccessfulPresentationCount();
        m_context.graphics.addRenderPassToBack(m_resizeRenderPass);
        m_resizeRenderPassRegistered = true;
    }
    FramebufferCaptureOptions captureOptions;
    if(ReadSmokeEnvironmentFlag("NWB_UI_LAYER_RESIZE_CAPTURE")){
        captureOptions.requiredWidth = 800u;
        captureOptions.requiredHeight = 600u;
    }
    constexpr u32 s_DefaultCaptureFrameCount = 60u;
    auto capture = NWB::Tests::Smoke::SmokeCaptureFactory(m_context).configure(NWB_TEXT("UiLayerSmokeProject"), s_DefaultCaptureFrameCount, captureOptions);
    if(!capture)
        return false;
    m_framebufferCapture = Move(*capture);

    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiLayerSmokeProject: standalone layer ready; default atlas; SDR; empty startup frames=2"));
    return true;
}

void UiLayerSmokeProject::onShutdown(){
    destroyRuntime();
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiLayerSmokeProject: shutdown"));
}

b8 UiLayerSmokeProject::onUpdate(const f32 delta){
    if(m_resizeRenderPassRegistered && !m_resizePresentationReported){
        const u64 presentations = m_context.graphics.getSuccessfulPresentationCount();
        if(presentations >= m_resizePresentationBaseline && presentations - m_resizePresentationBaseline >= s_ResizePreparationPresentations){
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiLayerSmokeProject: resize scene presented"));
            m_resizePresentationReported = true;
        }
    }
    return TickSmokeWorldWithCapture(delta, *m_world, m_framebufferCapture);
}

void UiLayerSmokeProject::destroyRuntime(){
    if(m_framebufferCapture){
        m_framebufferCapture->stop();
        m_framebufferCapture.reset();
    }
    if(m_resizeRenderPassRegistered){
        m_context.graphics.removeRenderPass(m_resizeRenderPass);
        m_resizeRenderPassRegistered = false;
    }
    ClearUiLayerSmokeWorld(m_context, *m_world);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_radio_group_smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr AStringView s_TitleKey = "title";
constexpr AStringView s_ChoicesKey = "choices";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UiRadioGroupSmokeScene::UiRadioGroupSmokeScene(Core::InputDispatcher& input)
    : m_input(input)
{
    m_state.select(10u);
    m_disabled.select(30u);
    m_popupRadio.select(10u);
    m_input.addHandlerToBack(*this);
}

UiRadioGroupSmokeScene::~UiRadioGroupSmokeScene()noexcept{
    m_input.removeHandler(*this);
}

b8 UiRadioGroupSmokeScene::paint(Impl::UiPaintContext& context){
    m_source.beginFrame();
    auto& ui = context.ui;
    ui.style().fontSize = 14.0f;
    ui.style().gap = 4.0f;
    if(!paintMain(ui) || !paintDisabled(ui) || !paintPopup(ui) || ui.failed())
        return false;
    observeState(context);
    paintMarkers(context);
    return true;
}

::NWB::Core::KeyboardInputResult UiRadioGroupSmokeScene::keyboardUpdate(const i32 key, const i32, const i32 action, const i32)noexcept{
    if(key < Core::Key::F4 || key > Core::Key::F9)
        return false;
    if(action != Core::InputAction::Press)
        return true;
    switch(key){
    case Core::Key::F4:
        m_source.reverse();
        break;
    case Core::Key::F5:
        m_source.remove(m_state.selectedKey());
        break;
    case Core::Key::F6:
        m_source.replace();
        break;
    case Core::Key::F7:
        m_state.select(30u);
        break;
    case Core::Key::F8:
        m_enabled = !m_enabled;
        break;
    case Core::Key::F9:
        m_parent.close();
        break;
    default:
        break;
    }
    return true;
}

b8 UiRadioGroupSmokeScene::paintMain(Impl::Ui::Builder& ui){
    using namespace Impl::Ui;
    if(!ui.beginPanel("radio_main", { 24.0f, 24.0f, 336.0f, 356.0f }))
        return false;
    const WidgetOptions caption{ {}, { LayoutSizePolicy::Fixed, 16.0f } };
    const WidgetOptions button{ { LayoutSizePolicy::Stretch, 1.0f }, { LayoutSizePolicy::Fixed, 28.0f } };
    if(!ui.label(__hidden_ui_radio_group_smoke::s_TitleKey, "Radio group / stable choice keys", caption))
        return false;
    if(ui.button("before", "Before group", button))
        ++m_beforeClicks;
    RadioGroupOptions options;
    options.enabled = m_enabled;
    const RadioGroupResult result = ui.radioGroup(__hidden_ui_radio_group_smoke::s_ChoicesKey, m_source, m_state, options);
    if(!result.valid)
        return false;
    m_changes += static_cast<u32>(result.selectionChanged);
    m_activations += static_cast<u32>(result.activated);
    if(ui.button("after", "After group", button))
        ++m_afterClicks;
    if(ui.button("open", "Open parent popup", button))
        m_parent.open();
    return ui.endPanel();
}

b8 UiRadioGroupSmokeScene::paintDisabled(Impl::Ui::Builder& ui){
    using namespace Impl::Ui;
    if(!ui.beginPanel("radio_disabled", { 392.0f, 24.0f, 300.0f, 240.0f }))
        return false;
    const WidgetOptions caption{ {}, { LayoutSizePolicy::Fixed, 16.0f } };
    if(!ui.label(__hidden_ui_radio_group_smoke::s_TitleKey, "Disabled group keeps checked choice", caption))
        return false;
    RadioGroupOptions options;
    options.enabled = false;
    const RadioGroupResult result = ui.radioGroup(__hidden_ui_radio_group_smoke::s_ChoicesKey, m_source, m_disabled, options);
    return result.valid && ui.endPanel();
}

b8 UiRadioGroupSmokeScene::paintPopup(Impl::Ui::Builder& ui){
    using namespace Impl::Ui;
    PopupOptions options;
    options.anchor = { 360.0f, 52.0f, 8.0f, 28.0f };
    options.side = PopupPlacementSide::Right;
    options.size = { 300.0f, 272.0f };
    if(!ui.beginPopup("radio_parent", m_parent, options))
        return !ui.failed();
    const WidgetOptions caption{ {}, { LayoutSizePolicy::Fixed, 16.0f } };
    if(!ui.label(__hidden_ui_radio_group_smoke::s_TitleKey, "Radio inside a parent popup", caption))
        return false;
    const RadioGroupResult result = ui.radioGroup(__hidden_ui_radio_group_smoke::s_ChoicesKey, m_source, m_popupRadio);
    if(!result.valid)
        return false;
    m_popupChanges += static_cast<u32>(result.selectionChanged);
    m_popupActivations += static_cast<u32>(result.activated);
    const WidgetOptions button{ { LayoutSizePolicy::Stretch, 1.0f }, { LayoutSizePolicy::Fixed, 28.0f } };
    if(ui.button("close", "Close parent", button))
        m_parent.close();
    return ui.endPopup();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 IsUiLayerRadioGroupSmokeEnabled(){
    return ReadSmokeEnvironmentFlag("NWB_UI_LAYER_RADIO_GROUP");
}

b8 IsUiLayerRadioGroupSkinSmokeEnabled(){
    return ReadSmokeEnvironmentFlag("NWB_UI_LAYER_RADIO_GROUP_SKIN");
}

SharedUiRadioGroupSmokeScene CreateUiRadioGroupSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input){
    return SharedUiRadioGroupSmokeScene(NewArenaObject<RefCounter<UiRadioGroupSmokeScene>>(arena, input), ArenaRefDeleter<RefCounter<UiRadioGroupSmokeScene>, Core::Alloc::GlobalArena>(&arena), s_AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_radio_group_snapshot{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr Array<TStringView, 41u> s_RectNames{
    NWB_TEXT("before"), NWB_TEXT("group"), NWB_TEXT("after"), NWB_TEXT("open"),
    NWB_TEXT("disabled_group"), NWB_TEXT("parent"), NWB_TEXT("popup_group"), NWB_TEXT("popup_close"),
    NWB_TEXT("row10"), NWB_TEXT("indicator10"), NWB_TEXT("mark10"),
    NWB_TEXT("row20"), NWB_TEXT("indicator20"), NWB_TEXT("mark20"),
    NWB_TEXT("row30"), NWB_TEXT("indicator30"), NWB_TEXT("mark30"),
    NWB_TEXT("row40"), NWB_TEXT("indicator40"), NWB_TEXT("mark40"),
    NWB_TEXT("row50"), NWB_TEXT("indicator50"), NWB_TEXT("mark50"),
    NWB_TEXT("disabled_row30"), NWB_TEXT("disabled_indicator30"), NWB_TEXT("disabled_mark30"),
    NWB_TEXT("popup_row10"), NWB_TEXT("popup_indicator10"), NWB_TEXT("popup_mark10"),
    NWB_TEXT("popup_row20"), NWB_TEXT("popup_indicator20"), NWB_TEXT("popup_mark20"),
    NWB_TEXT("popup_row30"), NWB_TEXT("popup_indicator30"), NWB_TEXT("popup_mark30"),
    NWB_TEXT("popup_row40"), NWB_TEXT("popup_indicator40"), NWB_TEXT("popup_mark40"),
    NWB_TEXT("popup_row50"), NWB_TEXT("popup_indicator50"), NWB_TEXT("popup_mark50")
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void CopyChoices(UiRadioGroupSnapshot& snapshot, const Impl::Ui::RadioGroupPlacement& placement, const u32 offset)noexcept{
    for(u32 index = 0u; index < placement.count; ++index){
        const Impl::Ui::RadioGroupChoicePlacement& row = placement.rows[index];
        if(row.key < 10u || row.key > 50u || row.key % 10u != 0u)
            continue;
        const usize destination = offset + static_cast<usize>(row.key / 10u - 1u) * 3u;
        snapshot.rectangles[destination] = row.rectangle;
        snapshot.rectangles[destination + 1u] = row.indicator;
        snapshot.rectangles[destination + 2u] = row.mark;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void UiRadioGroupSmokeScene::observeState(Impl::UiPaintContext& context){
    using namespace Impl::Ui;
    using namespace __hidden_ui_radio_group_snapshot;
    UiRadioGroupSnapshot current;
    current.display = context.display;
    current.rectangles[1u] = m_state.placement().bounds;
    current.rectangles[4u] = m_disabled.placement().bounds;
    CopyChoices(current, m_state.placement(), 8u);
    for(u32 index = 0u; index < m_disabled.placement().count; ++index){
        const RadioGroupChoicePlacement& row = m_disabled.placement().rows[index];
        if(row.key == 30u){
            current.rectangles[23u] = row.rectangle;
            current.rectangles[24u] = row.indicator;
            current.rectangles[25u] = row.mark;
        }
    }
    if(m_parent.isOpen()){
        current.rectangles[5u] = m_parent.placement().bounds;
        current.rectangles[6u] = m_popupRadio.placement().bounds;
        CopyChoices(current, m_popupRadio.placement(), 26u);
    }
    const InputRouter& input = context.ui.input();
    u32 focusCode = 0u;
    u32 focusScope = 0u;
    u32 rootButton = 0u;
    constexpr Array<u32, 3> s_RootSlots = { 0u, 2u, 3u };
    for(const HitTarget& target : input.targets()){
        u32 code = 0u;
        if(!target.control.valid() && target.activatable){
            if(!target.popup.valid() && rootButton < 3u){
                const u32 slot = s_RootSlots[rootButton++];
                current.rectangles[slot] = target.rectangle;
                code = slot + 1u;
            }
            else if(target.popup.instanceGeneration == m_parent.instanceGeneration()){
                if(m_parent.isOpen())
                    current.rectangles[7u] = target.rectangle;
                code = 6u;
            }
        }
        else if(target.control.instanceGeneration == m_state.inputGeneration() && !target.owner.valid())
            code = 2u;
        else if(target.control.instanceGeneration == m_popupRadio.inputGeneration() && !target.owner.valid())
            code = 5u;
        if(target.id == input.focus()){
            focusCode = code;
            focusScope = static_cast<u32>(target.popup.valid());
        }
    }
    current.values = { m_state.selectedKey(), m_state.cursorKey(), focusCode, m_changes, m_activations,
        m_beforeClicks, m_afterClicks, static_cast<u64>(m_enabled), m_source.revision(), m_source.instanceGeneration(),
        static_cast<u64>(m_source.reversed()), m_source.removedKey(), static_cast<u64>(m_parent.isOpen()),
        m_popupRadio.selectedKey(), m_popupRadio.cursorKey(), m_popupChanges, m_popupActivations,
        static_cast<u64>(input.popupCount()), focusScope, m_source.labelReads(), m_disabled.selectedKey(),
        m_source.rowCount() };
    const DisplayMetrics& previous = m_snapshot.display;
    const b8 displayChanged = current.display.logicalWidth != previous.logicalWidth || current.display.logicalHeight != previous.logicalHeight || current.display.pixelScaleX != previous.pixelScaleX || current.display.pixelScaleY != previous.pixelScaleY;
    b8 changed = current.values != m_snapshot.values;
    for(usize index = 0u; index < current.rectangles.size(); ++index)
        changed |= !SameSmokeRect(current.rectangles[index], m_snapshot.rectangles[index]);
    if(m_snapshot.sequence != 0u && !changed && !displayChanged)
        return;
    current.sequence = m_snapshot.sequence + 1u;
    m_snapshot = current;
    if(displayChanged){
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiRadioGroupSmoke: display logical={}x{} scale={}x{}"), current.display.logicalWidth, current.display.logicalHeight, current.display.pixelScaleX, current.display.pixelScaleY);
    }
    const auto& value = current.values;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiRadioGroupSmoke: state sequence={} values={},{},{},{},{},{},{},{},{},{},{}," "{},{},{},{},{},{},{},{},{},{},{}")
        , current.sequence, value[0], value[1], value[2], value[3], value[4], value[5], value[6], value[7]
        , value[8], value[9], value[10], value[11], value[12], value[13], value[14], value[15], value[16]
        , value[17], value[18], value[19], value[20], value[21]
    );
    for(usize index = 0u; index < current.rectangles.size(); ++index)
        LogSmokeRect(NWB_TEXT("UiRadioGroupSmoke"), current.sequence, s_RectNames[index], current.rectangles[index]);
}

void UiRadioGroupSmokeScene::paintMarkers(Impl::UiPaintContext& context)const{
    PaintKeyedSmokeValueMarkers<12u, 8u, 8u, 18u>(m_snapshot.values, m_snapshot.sequence, context);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


u64 UiRadioGroupSmokeSource::instanceGeneration()const noexcept{
    invalidateText();
    return m_generation;
}

u64 UiRadioGroupSmokeSource::revision()const noexcept{
    invalidateText();
    return m_revision;
}

u64 UiRadioGroupSmokeSource::rowCount()const noexcept{
    invalidateText();
    return m_removed == 0u ? 5u : 4u;
}

u64 UiRadioGroupSmokeSource::key(const u64 index)const noexcept{
    invalidateText();
    return rawKey(index);
}

Expected<u64> UiRadioGroupSmokeSource::indexOf(const u64 keyValue)const noexcept{
    u64 index = 0u;
    invalidateText();
    if(keyValue < 10u || keyValue > 50u || keyValue % 10u != 0u || keyValue == m_removed)
        return MakeUnexpected(Failure{});
    u64 forward = keyValue / 10u - 1u;
    if(m_removed != 0u && keyValue > m_removed)
        --forward;
    const u64 count = m_removed == 0u ? 5u : 4u;
    index = m_reversed ? count - 1u - forward : forward;
    return index < count ? Expected<u64>{ index } : MakeUnexpected(Failure{});
}

Expected<u64> UiRadioGroupSmokeSource::findEnabled(const u64 start, const b8 reverseValue)const noexcept{
    u64 index = 0u;
    invalidateText();
    const u64 count = m_removed == 0u ? 5u : 4u;
    if(start >= count)
        return MakeUnexpected(Failure{});
    for(u64 cursor = start; cursor < count;){
        if(rawKey(cursor) != 30u){
            index = cursor;
            return index;
        }
        if(reverseValue){
            if(cursor == 0u)
                break;
            --cursor;
        }
        else
            ++cursor;
    }
    return MakeUnexpected(Failure{});
}

StringView UiRadioGroupSmokeSource::text(const u64 index)const noexcept{
    invalidateText();
    ++m_labelReads;
    constexpr Array<StringView, 5> s_Labels = { "Alpha", "Beta", "Gamma (disabled)", "Delta", "Epsilon" };
    const u64 choice = rawKey(index);
    if(choice == 0u)
        return {};
    const StringView label = s_Labels[choice / 10u - 1u];
    NWB_MEMCPY(m_label.data(), m_label.size(), label.data(), label.size());
    return { m_label.data(), label.size() };
}

b8 UiRadioGroupSmokeSource::enabled(const u64 index)const noexcept{
    invalidateText();
    const u64 choice = rawKey(index);
    return choice != 0u && choice != 30u;
}

void UiRadioGroupSmokeSource::reverse()noexcept{
    m_reversed = !m_reversed;
    ++m_revision;
}

void UiRadioGroupSmokeSource::remove(const u64 keyValue)noexcept{
    if(keyValue < 10u || keyValue > 50u || keyValue % 10u != 0u || m_removed != 0u)
        return;
    m_removed = keyValue;
    ++m_revision;
}

void UiRadioGroupSmokeSource::replace()noexcept{
    ++m_generation;
    ++m_revision;
    m_removed = 0u;
    m_reversed = false;
}

u64 UiRadioGroupSmokeSource::rawKey(const u64 index)const noexcept{
    const u64 count = m_removed == 0u ? 5u : 4u;
    if(index >= count)
        return 0u;
    const u64 forward = m_reversed ? count - 1u - index : index;
    u64 choice = (forward + 1u) * 10u;
    if(m_removed != 0u && choice >= m_removed)
        choice += 10u;
    return choice;
}

void UiRadioGroupSmokeSource::invalidateText()const noexcept{
    m_label.fill('?');
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_search_combo_smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr Impl::Ui::Rect s_Panel{ 24.0f, 24.0f, 400.0f, 160.0f };
static constexpr Impl::Ui::Rect s_Counter{ 456.0f, 32.0f, 130.0f, 36.0f };
static constexpr TStringView s_LogTag = NWB_TEXT("UiSearchComboSmoke");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UiSearchComboSmokeScene::UiSearchComboSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input)
    : m_arena(arena)
    , m_input(input)
    , m_source(arena)
    , m_state(arena)
{
    m_input.addHandlerToBack(*this);
}

UiSearchComboSmokeScene::~UiSearchComboSmokeScene()noexcept{
    m_input.removeHandler(*this);
}

b8 UiSearchComboSmokeScene::paint(Impl::UiPaintContext& context){
    using namespace Impl::Ui;
    ObserveSmokeDisplay(__hidden_ui_search_combo_smoke::s_LogTag, context.display, m_lastDisplay, m_displayChanged);
    m_source.beginFrame();
    m_focused = false;
    Builder& ui = context.ui;
    ui.style().fontSize = 12.0f;
    ui.style().gap = 8.0f;
    ui.listStyle().padding = { 8.0f, 8.0f, 8.0f, 8.0f };
    const Rect panel = __hidden_ui_search_combo_smoke::s_Panel;
    if(!ui.beginPanel("search_combo_fixture", panel))
        return false;
    const WidgetOptions title{ {}, { LayoutSizePolicy::Fixed, 24.0f } };
    if(!ui.label("title", "Search 100000 rows by numeric prefix", title))
        return false;
    SearchComboOptions options;
    options.combo.height = { LayoutSizePolicy::Fixed, 36.0f };
    options.combo.popupHeight = 280.0f;
    options.combo.enabled = m_enabled;
    const SearchComboResult result = ui.searchComboBox("choice", m_source, m_state, options);
    if(!result.combo.valid)
        return false;
    m_focused = result.combo.focused;
    if(result.combo.committed)
        ++m_commits;
    if(!ui.label("hint", "Type 12 or 5 / Arrows preview / Enter commit", title) || !ui.endPanel())
        return false;
    if(!ui.beginPanel("outside_fixture", { 448.0f, 24.0f, 146.0f, 52.0f }))
        return false;
    const WidgetOptions counter{ { LayoutSizePolicy::Fixed, 130.0f }, { LayoutSizePolicy::Fixed, 36.0f } };
    if(ui.button("underlying", "Outside counter", counter))
        ++m_underlying;
    if(!ui.endPanel())
        return false;
    observeState(context.text);
    PaintKeyedSmokeValueMarkers<12u, 8u>(values(), m_sequence, context);
    return true;
}

::NWB::Core::KeyboardInputResult UiSearchComboSmokeScene::keyboardUpdate(const i32 key, const i32, const i32 action, const i32){
    if(key < Core::Key::F4 || key > Core::Key::F9)
        return false;
    if(action != Core::InputAction::Press)
        return true;
    switch(key){
    case Core::Key::F4:
        m_state.combo().select(100000u);
        break;
    case Core::Key::F5:
        m_source.reverse();
        break;
    case Core::Key::F6:
        m_source.remove(m_state.combo().selectedKey());
        break;
    case Core::Key::F7:{
        const b8 cleared = m_state.query().setText({});
        NWB_FATAL_ASSERT(cleared);
        break;
    }
    case Core::Key::F9:
        m_enabled = !m_enabled;
        break;
    default:
        break;
    }
    return true;
}

static constexpr u32 s_SmokeMarker24BitMask = 0xffffffu;

Array<u64, 22u> UiSearchComboSmokeScene::values()const noexcept{
    const auto& combo = m_state.combo();
    const auto& list = combo.listState();
    const auto& placement = list.placement();
    const auto& query = m_state.query();
    return { combo.selectedKey(), list.cursorKey(), m_source.rowCount(), m_source.filtered().rowCount(),
        placement.firstRow, placement.endRow, m_source.labelReads(), static_cast<u64>(combo.isOpen()),
        static_cast<u64>(query.text().size()), ComputeFnv32TextHash(query.text()) & s_SmokeMarker24BitMask, static_cast<u64>(query.anchor()),
        static_cast<u64>(query.caret()), static_cast<u64>(m_state.editorState().focused), static_cast<u64>(m_focused),
        m_commits, m_source.filtered().revision(), static_cast<u64>(m_source.reversed()), m_source.removedKey(),
        static_cast<u64>(m_enabled), m_source.revision(), m_underlying, static_cast<u64>(query.composition().active) };
}

Impl::Ui::Rect UiSearchComboSmokeScene::cursorRow()const noexcept{
    const auto& list = m_state.combo().listState();
    const auto& placement = list.placement();
    if(!m_state.combo().isOpen())
        return {};
    const auto index = m_source.filtered().indexOf(list.cursorKey());
    if(!index || *index < placement.firstRow || *index >= placement.endRow)
        return {};
    const auto rectangle = SmokeListRowBounds(placement, *index);
    if(!rectangle)
        return {};
    return *rectangle;
}

void UiSearchComboSmokeScene::observeState(Impl::Ui::TextService& text){
    const auto current = values();
    const auto& placement = m_state.combo().listState().placement();
    const auto& popup = m_state.combo().placement();
    const auto& bounds = m_state.combo().bounds();
    if(
        m_sequence != 0u && !m_displayChanged && current == m_lastValues
        && placement.offset == m_lastPlacement.offset && placement.contentHeight == m_lastPlacement.contentHeight
        && SameSmokeRect(placement.bounds, m_lastPlacement.bounds)
        && SameSmokeRect(placement.thumb, m_lastPlacement.thumb)
        && SameSmokeRect(popup.bounds, m_lastPopup.bounds)
        && SameSmokeRect(bounds, m_lastBounds)
        && SameSmokeRect(m_state.editorState().placement.bounds, m_lastQueryBounds)
    )
        return;
    ++m_sequence;
    m_lastValues = current;
    m_lastPlacement = placement;
    m_lastPopup = popup;
    m_lastBounds = bounds;
    m_lastQueryBounds = m_state.editorState().placement.bounds;
    m_displayChanged = false;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiSearchComboSmoke: state sequence={} values={},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{}")
        , m_sequence, current[0], current[1], current[2], current[3], current[4], current[5], current[6], current[7]
        , current[8], current[9], current[10], current[11], current[12], current[13], current[14], current[15]
        , current[16], current[17], current[18], current[19], current[20], current[21]
    );
    LogSmokeRect(__hidden_ui_search_combo_smoke::s_LogTag, m_sequence, NWB_TEXT("trigger"), bounds);
    LogSmokeRect(__hidden_ui_search_combo_smoke::s_LogTag, m_sequence, NWB_TEXT("popup"), popup.bounds);
    LogSmokeRect(__hidden_ui_search_combo_smoke::s_LogTag, m_sequence, NWB_TEXT("list"), placement.bounds);
    LogSmokeRect(__hidden_ui_search_combo_smoke::s_LogTag, m_sequence, NWB_TEXT("viewport"), placement.viewport);
    const auto first = SmokeListRowBounds(placement, placement.firstRow);
    LogSmokeRect(__hidden_ui_search_combo_smoke::s_LogTag, m_sequence, NWB_TEXT("first_row"), first ? *first : Impl::Ui::Rect{});
    LogSmokeRect(__hidden_ui_search_combo_smoke::s_LogTag, m_sequence, NWB_TEXT("track"), placement.track);
    LogSmokeRect(__hidden_ui_search_combo_smoke::s_LogTag, m_sequence, NWB_TEXT("thumb"), placement.thumb);
    LogSmokeRect(__hidden_ui_search_combo_smoke::s_LogTag, m_sequence, NWB_TEXT("cursor_row"), cursorRow());
    LogSmokeRect(__hidden_ui_search_combo_smoke::s_LogTag, m_sequence, NWB_TEXT("counter"), __hidden_ui_search_combo_smoke::s_Counter);
    const auto& editor = m_state.editorState().placement;
    LogSmokeRect(__hidden_ui_search_combo_smoke::s_LogTag, m_sequence, NWB_TEXT("query"), editor.bounds);
    LogSmokeRect(__hidden_ui_search_combo_smoke::s_LogTag, m_sequence, NWB_TEXT("query_content"), editor.content);
    LogSmokeRect(__hidden_ui_search_combo_smoke::s_LogTag, m_sequence, NWB_TEXT("query_caret"), editor.caret);
    LogSmokeRect(__hidden_ui_search_combo_smoke::s_LogTag, m_sequence, NWB_TEXT("query_selection"), CaptureEditSelection(m_arena, text, m_state.query(), editor, 12.0f));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 IsUiLayerSearchComboSmokeEnabled(){
    return ReadSmokeEnvironmentFlag("NWB_UI_LAYER_SEARCH_COMBO");
}

b8 IsUiLayerSearchComboSkinSmokeEnabled(){
    return ReadSmokeEnvironmentFlag("NWB_UI_LAYER_SEARCH_COMBO_SKIN");
}

SharedUiSearchComboSmokeScene CreateUiSearchComboSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input){
    return SharedUiSearchComboSmokeScene(NewArenaObject<RefCounter<UiSearchComboSmokeScene>>(arena, arena, input), ArenaRefDeleter<RefCounter<UiSearchComboSmokeScene>, Core::Alloc::GlobalArena>(&arena), s_AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


u64 UiSearchComboSmokeSource::View::key(const u64 index)const noexcept{
    if(index >= rowCount())
        return 0u;
    u64 remaining = m_source.reversed() ? rowCount() - 1u - index : index;
    const u64 removed = m_source.removedKey();
    for(usize rangeIndex = 0u; rangeIndex < m_source.m_rangeCount; ++rangeIndex){
        const Range& range = m_source.m_ranges[rangeIndex];
        const b8 excludes = removed >= range.first && removed < range.past;
        const u64 count = range.past - range.first - static_cast<u64>(excludes);
        if(remaining >= count){
            remaining -= count;
            continue;
        }
        u64 value = range.first + remaining;
        if(excludes && value >= removed)
            ++value;
        return value;
    }
    return 0u;
}

Expected<u64> UiSearchComboSmokeSource::View::indexOf(const u64 keyValue)const noexcept{
    u64 index = 0u;
    if(keyValue == 0u || keyValue > 100000u || keyValue == m_source.removedKey())
        return MakeUnexpected(Failure{});
    u64 offset = 0u;
    const u64 removed = m_source.removedKey();
    for(usize rangeIndex = 0u; rangeIndex < m_source.m_rangeCount; ++rangeIndex){
        const Range& range = m_source.m_ranges[rangeIndex];
        const b8 excludes = removed >= range.first && removed < range.past;
        if(keyValue >= range.first && keyValue < range.past){
            const u64 natural = offset + keyValue - range.first - static_cast<u64>(excludes && removed < keyValue);
            index = m_source.reversed() ? rowCount() - 1u - natural : natural;
            return index;
        }
        offset += range.past - range.first - static_cast<u64>(excludes);
    }
    return MakeUnexpected(Failure{});
}

Expected<u64> UiSearchComboSmokeSource::View::findEnabled(const u64 start, const b8 reverse)const noexcept{
    return FindAdjacentEnabledRow(*this, start, reverse);
}

StringView UiSearchComboSmokeSource::View::text(const u64 index)const noexcept{
    const auto fullIndex = m_source.m_rows.indexOf(key(index));
    if(!fullIndex)
        return {};
    return m_source.m_rows.text(*fullIndex);
}

b8 UiSearchComboSmokeSource::View::enabled(const u64 index)const noexcept{
    return index < rowCount() && key(index) != 5u;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UiSearchComboSmokeSource::UiSearchComboSmokeSource(Core::Alloc::GlobalArena& arena)noexcept
    : m_query(arena)
    , m_view(*this)
{
    const b8 initialized = filter({});
    NWB_FATAL_ASSERT(initialized);
}

Expected<u64> UiSearchComboSmokeSource::findEnabled(const u64 start, const b8 reverse)const noexcept{
    return m_rows.findEnabled(start, reverse);
}

b8 UiSearchComboSmokeSource::filter(const AStringView query){
    if(query == AStringView(m_query.data(), m_query.size()) && m_cachedSourceRevision == revision())
        return true;
    if(query.empty())
        m_query.clear();
    else
        m_query.assign(query.data(), query.size());
    m_cachedSourceRevision = revision();
    m_rangeCount = 0u;
    m_filteredCount = 0u;
    if(query.empty())
        addRange(1u, 100001u);
    else{
        u64 prefix = 0u;
        b8 valid = query.size() <= 6u && query.front() != '0';
        for(const char character : query){
            if(character < '0' || character > '9'){
                valid = false;
                break;
            }
            if(valid)
                prefix = prefix * 10u + static_cast<u64>(character - '0');
        }
        if(valid && prefix != 0u){
            u64 span = 1u;
            while(prefix <= 100000u){
                addRange(prefix, Min<u64>(100001u, prefix + span));
                prefix *= 10u;
                span *= 10u;
            }
        }
    }
    ++m_viewRevision;
    return true;
}

void UiSearchComboSmokeSource::addRange(const u64 first, const u64 past)noexcept{
    if(first >= past || m_rangeCount >= m_ranges.size())
        return;
    m_ranges[m_rangeCount] = { .first = first, .past = past };
    ++m_rangeCount;
    m_filteredCount += past - first;
    if(removedKey() >= first && removedKey() < past)
        --m_filteredCount;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_slider_smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr AStringView s_TitleKey = "title";
constexpr AStringView s_ValueKey = "value";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UiSliderSmokeScene::UiSliderSmokeScene(Core::InputDispatcher& input)
    : m_input(input)
{
    NWB_FATAL_ASSERT(m_state.setValue(0.25));
    NWB_FATAL_ASSERT(m_disabled.setValue(0.75));
    NWB_FATAL_ASSERT(m_constant.setValue(0.5));
    NWB_FATAL_ASSERT(m_popupSlider.setValue(0.5));
    m_input.addHandlerToBack(*this);
}

UiSliderSmokeScene::~UiSliderSmokeScene()noexcept{
    m_input.removeHandler(*this);
}

b8 UiSliderSmokeScene::paint(Impl::UiPaintContext& context){
    auto& ui = context.ui;
    ui.style().fontSize = 14.0f;
    ui.style().gap = 8.0f;
    const f32 leftWidth = Max(280.0f, (context.display.logicalWidth - 96.0f) * 0.5f);
    const f32 right = 24.0f + leftWidth + 32.0f;
    const f32 rightWidth = Max(240.0f, context.display.logicalWidth - right - 24.0f);
    if(!paintMain(ui, { 24.0f, 24.0f, leftWidth, 220.0f }) || !paintOther(ui, { right, 24.0f, rightWidth, 180.0f }) || !paintPopup(ui, right) || ui.failed())
        return false;
    observeState(context);
    paintMarkers(context);
    return true;
}

::NWB::Core::KeyboardInputResult UiSliderSmokeScene::keyboardUpdate(const i32 key, const i32, const i32 action, const i32)noexcept{
    if(key < Core::Key::F4 || key > Core::Key::F9)
        return false;
    if(action != Core::InputAction::Press)
        return true;
    switch(key){
    case Core::Key::F4:
        if(m_state.setValue(m_state.value()))
            ++m_externalIntents;
        break;
    case Core::Key::F5:
        m_narrowRange = !m_narrowRange;
        break;
    case Core::Key::F6:
        m_coarseStep = !m_coarseStep;
        break;
    case Core::Key::F7:
        m_enabled = !m_enabled;
        break;
    case Core::Key::F8:
        if(m_state.setValue(0.25))
            ++m_externalIntents;
        break;
    case Core::Key::F9:
        m_parent.close();
        break;
    default:
        break;
    }
    return true;
}

b8 UiSliderSmokeScene::paintMain(Impl::Ui::Builder& ui, const Impl::Ui::Rect& bounds){
    using namespace Impl::Ui;
    if(!ui.beginPanel("slider_main", bounds))
        return false;
    const WidgetOptions caption{ {}, { LayoutSizePolicy::Fixed, 16.0f } };
    const WidgetOptions button{ { LayoutSizePolicy::Stretch, 1.0f }, { LayoutSizePolicy::Fixed, 28.0f } };
    if(!ui.label(__hidden_ui_slider_smoke::s_TitleKey, "Continuous slider / exact double value", caption))
        return false;
    if(ui.button("before", "Before slider", button))
        ++m_beforeClicks;
    SliderOptions settings;
    settings.maximum = m_narrowRange ? 0.5 : 1.0;
    settings.keyStep = m_coarseStep ? 0.25 : 0.125;
    settings.enabled = m_enabled;
    if(!ui.slider(__hidden_ui_slider_smoke::s_ValueKey, m_state, settings))
        return false;
    if(ui.button("after", "After slider", button))
        ++m_afterClicks;
    if(ui.button("open", "Open parent popup", button))
        m_parent.open();
    if(!ui.endPanel())
        return false;
    m_changes += static_cast<u32>(m_state.result().valueChanged);
    return m_state.result().valid;
}

b8 UiSliderSmokeScene::paintOther(Impl::Ui::Builder& ui, const Impl::Ui::Rect& bounds){
    using namespace Impl::Ui;
    if(!ui.beginPanel("slider_other", bounds))
        return false;
    const WidgetOptions caption{ {}, { LayoutSizePolicy::Fixed, 16.0f } };
    if(!ui.label("disabled_title", "Disabled slider keeps its value", caption))
        return false;
    SliderOptions disabled;
    disabled.enabled = false;
    if(!ui.slider("disabled", m_disabled, disabled))
        return false;
    if(!ui.label("constant_title", "Constant range has no interaction", caption))
        return false;
    SliderOptions constant;
    constant.minimum = 0.5;
    constant.maximum = 0.5;
    if(!ui.slider("constant", m_constant, constant) || !ui.endPanel())
        return false;
    return m_disabled.result().valid && m_constant.result().valid;
}

b8 UiSliderSmokeScene::paintPopup(Impl::Ui::Builder& ui, const f32 right){
    using namespace Impl::Ui;
    PopupOptions popup;
    popup.anchor = { right, 228.0f, 8.0f, 24.0f };
    popup.side = PopupPlacementSide::Below;
    popup.size = { 300.0f, 144.0f };
    if(!ui.beginPopup("slider_parent", m_parent, popup))
        return !ui.failed();
    const WidgetOptions caption{ {}, { LayoutSizePolicy::Fixed, 16.0f } };
    if(!ui.label(__hidden_ui_slider_smoke::s_TitleKey, "Slider inside parent popup", caption))
        return false;
    SliderOptions settings;
    settings.keyStep = 0.125;
    if(!ui.slider(__hidden_ui_slider_smoke::s_ValueKey, m_popupSlider, settings))
        return false;
    const WidgetOptions button{ { LayoutSizePolicy::Stretch, 1.0f }, { LayoutSizePolicy::Fixed, 28.0f } };
    if(ui.button("close", "Close parent", button))
        m_parent.close();
    if(!ui.endPopup())
        return false;
    if(m_parent.isOpen())
        m_popupChanges += static_cast<u32>(m_popupSlider.result().valueChanged);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 IsUiLayerSliderSmokeEnabled(){
    return ReadSmokeEnvironmentFlag("NWB_UI_LAYER_SLIDER");
}

b8 IsUiLayerSliderSkinSmokeEnabled(){
    return ReadSmokeEnvironmentFlag("NWB_UI_LAYER_SLIDER_SKIN");
}

SharedUiSliderSmokeScene CreateUiSliderSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input){
    return SharedUiSliderSmokeScene(NewArenaObject<RefCounter<UiSliderSmokeScene>>(arena, input), ArenaRefDeleter<RefCounter<UiSliderSmokeScene>, Core::Alloc::GlobalArena>(&arena), s_AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_slider_snapshot{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr Array<TStringView, 29u> s_RectNames{
    NWB_TEXT("before"), NWB_TEXT("after"), NWB_TEXT("open"), NWB_TEXT("parent"), NWB_TEXT("popup_close"),
    NWB_TEXT("main_bounds"), NWB_TEXT("main_clip"), NWB_TEXT("main_travel"), NWB_TEXT("main_track"),
    NWB_TEXT("main_center"), NWB_TEXT("main_thumb"), NWB_TEXT("disabled_bounds"), NWB_TEXT("disabled_clip"),
    NWB_TEXT("disabled_travel"), NWB_TEXT("disabled_track"), NWB_TEXT("disabled_center"), NWB_TEXT("disabled_thumb"),
    NWB_TEXT("constant_bounds"), NWB_TEXT("constant_clip"), NWB_TEXT("constant_travel"), NWB_TEXT("constant_track"),
    NWB_TEXT("constant_center"), NWB_TEXT("constant_thumb"), NWB_TEXT("popup_bounds"), NWB_TEXT("popup_clip"),
    NWB_TEXT("popup_travel"), NWB_TEXT("popup_track"), NWB_TEXT("popup_center"), NWB_TEXT("popup_thumb")
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void CopyPlacement(UiSliderSnapshot& snapshot, const Impl::Ui::SliderPlacement& placement, const u32 offset)noexcept{
    snapshot.rectangles[offset] = placement.bounds;
    snapshot.rectangles[offset + 1u] = placement.clip;
    snapshot.rectangles[offset + 2u] = placement.travelBounds;
    snapshot.rectangles[offset + 3u] = placement.track;
    snapshot.rectangles[offset + 4u] = placement.centerTravel;
    snapshot.rectangles[offset + 5u] = placement.thumb;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void UiSliderSmokeScene::observeState(Impl::UiPaintContext& context){
    using namespace Impl::Ui;
    using namespace __hidden_ui_slider_snapshot;
    UiSliderSnapshot current;
    current.display = context.display;
    CopyPlacement(current, m_state.placement(), 5u);
    CopyPlacement(current, m_disabled.placement(), 11u);
    CopyPlacement(current, m_constant.placement(), 17u);
    if(m_parent.isOpen()){
        current.rectangles[3u] = m_parent.placement().bounds;
        CopyPlacement(current, m_popupSlider.placement(), 23u);
    }
    const InputRouter& input = context.ui.input();
    u32 focusCode = 0u;
    u32 focusScope = 0u;
    u32 rootButton = 0u;
    for(const HitTarget& target : input.targets()){
        u32 code = 0u;
        if(!target.control.valid() && target.activatable){
            if(!target.popup.valid() && rootButton < 3u){
                current.rectangles[rootButton] = target.rectangle;
                code = rootButton == 0u ? 1u : rootButton == 1u ? 3u : 4u;
                ++rootButton;
            }
            else if(target.popup.instanceGeneration == m_parent.instanceGeneration()){
                if(m_parent.isOpen())
                    current.rectangles[4u] = target.rectangle;
                code = 6u;
            }
        }
        else if(target.control.instanceGeneration == m_state.inputGeneration() && !target.owner.valid())
            code = 2u;
        else if(target.control.instanceGeneration == m_popupSlider.inputGeneration() && !target.owner.valid())
            code = 5u;
        if(target.id == input.focus()){
            focusCode = code;
            focusScope = static_cast<u32>(target.popup.valid());
        }
    }
    const b8 open = m_parent.isOpen();
    current.values = { focusCode, m_changes, m_popupChanges, m_beforeClicks, m_afterClicks,
        static_cast<u64>(m_enabled), static_cast<u64>(m_narrowRange), static_cast<u64>(m_coarseStep),
        static_cast<u64>(open), static_cast<u64>(input.popupCount()), focusScope,
        static_cast<u64>(m_state.result().valid), static_cast<u64>(m_state.result().dragging),
        static_cast<u64>(open && m_popupSlider.result().valid), static_cast<u64>(open && m_popupSlider.result().dragging),
        m_externalIntents };
    current.bits = { BitCast<u64>(m_state.value()), BitCast<u64>(m_disabled.value()), BitCast<u64>(m_constant.value()), BitCast<u64>(m_popupSlider.value()) };
    const DisplayMetrics& previous = m_snapshot.display;
    const b8 displayChanged = current.display.logicalWidth != previous.logicalWidth || current.display.logicalHeight != previous.logicalHeight || current.display.pixelScaleX != previous.pixelScaleX || current.display.pixelScaleY != previous.pixelScaleY;
    b8 changed = current.values != m_snapshot.values || current.bits != m_snapshot.bits;
    for(usize index = 0u; index < current.rectangles.size(); ++index)
        changed |= !SameSmokeRect(current.rectangles[index], m_snapshot.rectangles[index]);
    if(m_snapshot.sequence != 0u && !changed && !displayChanged)
        return;
    current.sequence = m_snapshot.sequence + 1u;
    m_snapshot = current;
    if(displayChanged){
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiSliderSmoke: display logical={}x{} scale={}x{}"), current.display.logicalWidth, current.display.logicalHeight, current.display.pixelScaleX, current.display.pixelScaleY);
    }
    const auto& value = current.values;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiSliderSmoke: state sequence={} values={},{},{},{},{},{},{},{}," "{},{},{},{},{},{},{},{} bits={},{},{},{}")
        , current.sequence, value[0], value[1], value[2], value[3], value[4], value[5], value[6], value[7]
        , value[8], value[9], value[10], value[11], value[12], value[13], value[14], value[15]
        , current.bits[0], current.bits[1], current.bits[2], current.bits[3]
    );
    for(usize index = 0u; index < current.rectangles.size(); ++index)
        LogSmokeRect(NWB_TEXT("UiSliderSmoke"), current.sequence, s_RectNames[index], current.rectangles[index]);
}

void UiSliderSmokeScene::paintMarkers(Impl::UiPaintContext& context)const{
    PaintSmokeValueMarkers(m_snapshot.values, m_snapshot.bits, m_snapshot.sequence, context);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Impl::Ui::Color EncodeSmokeColor(const u64 value)noexcept{
    const SIMDVectorSingle channels = VectorSet(static_cast<f32>(value & 15u), static_cast<f32>((value >> 4u) & 15u), static_cast<f32>((value >> 8u) & 15u), 15.0f);
    const SIMDVectorSingle color = VectorDivide(channels, VectorReplicate(15.0f));
    return { VectorGetX(color), VectorGetY(color), VectorGetZ(color), 1.0f };
}

Expected<Impl::Ui::Rect> SmokeListRowBounds(const Impl::Ui::ScrollPlacement& placement, const u64 index)noexcept{
    if(placement.rowCount == 0u)
        return MakeUnexpected(Failure{});
    const f32 height = static_cast<f32>(static_cast<f64>(placement.contentHeight) / static_cast<f64>(placement.rowCount));
    return Impl::Ui::CalculateScrollRowBounds(index, placement, height);
}

Impl::Ui::Rect VisibleSmokeListRow(const Impl::Ui::ScrollPlacement& placement, const u64 index)noexcept{
    const auto rectangle = SmokeListRowBounds(placement, index);
    if(!rectangle)
        return {};
    const auto visible = Impl::Ui::TryIntersectUiRects(*rectangle, placement.contentClip, Impl::Ui::UiRectPrecision::PreciseExtents);
    return visible ? *visible : Impl::Ui::Rect{};
}

void LogSmokeRect(const TStringView tag, const u32 sequence, const TStringView name, const Impl::Ui::Rect& rectangle){
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("{}: geometry sequence={} {}={},{},{},{}"), tag, sequence, name, rectangle.x, rectangle.y, rectangle.width, rectangle.height);
}

void ObserveSmokeDisplay(const TStringView tag, const Impl::Ui::DisplayMetrics& display, Impl::Ui::DisplayMetrics& inOutLastDisplay, b8& inOutDisplayChanged){
    if(inOutLastDisplay.logicalWidth == display.logicalWidth && inOutLastDisplay.logicalHeight == display.logicalHeight && inOutLastDisplay.pixelScaleX == display.pixelScaleX && inOutLastDisplay.pixelScaleY == display.pixelScaleY)
        return;
    inOutLastDisplay = display;
    inOutDisplayChanged = true;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("{}: display logical={}x{} scale={}x{}"), tag, display.logicalWidth, display.logicalHeight, display.pixelScaleX, display.pixelScaleY);
}

void LogSmokeEditGeometry(const TStringView tag, const u32 sequence, const TStringView field, const Impl::Ui::EditBoxPlacement& placement, const Impl::Ui::Rect& selection){
    const auto& bounds = placement.bounds;
    const auto& content = placement.content;
    const auto& caret = placement.caret;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("{}: geometry sequence={} field={} bounds={},{},{},{} content={},{},{},{}"), tag, sequence, field, bounds.x, bounds.y, bounds.width, bounds.height, content.x, content.y, content.width, content.height);
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("{}: selection sequence={} field={} caret={},{},{},{} selection={},{},{},{} scroll={}")
        , tag, sequence, field, caret.x, caret.y, caret.width, caret.height
        , selection.x, selection.y, selection.width, selection.height, placement.scroll
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UiTextAreaSmokeScene::UiTextAreaSmokeScene(Core::Alloc::GlobalArena& arena, Core::IClipboardService& clipboard)
    : m_clipboard(clipboard)
    , m_longText(arena)
    , m_model(arena, {}, Impl::Ui::EditTextMode::Multiline)
    , m_observed(arena)
    , m_completion(arena)
{
    m_longText.reserve(1044u);
    for(u32 line = 0u; line < 16u; ++line)
        m_longText.append("abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijkl\n");
    m_longText.append("tail");
    resetModel();
}

UiTextAreaSmokeScene::~UiTextAreaSmokeScene()noexcept{
    if(m_clipboardToken.valid() && !m_clipboard.cancel(m_clipboardToken))
        TerminateInvariant();
}

b8 UiTextAreaSmokeScene::paint(Impl::UiPaintContext& context){
    using namespace Impl::Ui;
    if(!drainClipboard())
        return false;
    Builder& ui = context.ui;
    ui.style().fontSize = 16.0f;
    ui.style().gap = 8.0f;
    if(!ui.beginPanel("text_area_fixture", { 24.0f, 24.0f, 480.0f, 380.0f }))
        return false;
    const WidgetOptions title{ {}, { LayoutSizePolicy::Fixed, 24.0f } };
    const WidgetOptions controller{ { LayoutSizePolicy::Fixed, 112.0f }, { LayoutSizePolicy::Fixed, 30.0f } };
    if(!ui.label("title", "Multiline plain text", title) || !ui.beginRow("document_actions"))
        return false;
    if(ui.button("reset", "Reset", controller))
        resetModel();
    if(ui.button("long", "Long document", controller))
        longDocument();
    if(ui.button("readonly", m_readOnly ? "Editable" : "Read only", controller))
        m_readOnly = !m_readOnly;
    if(!ui.endContainer() || !ui.beginRow("viewport_actions"))
        return false;
    if(ui.button("enabled", m_enabled ? "Disable" : "Enable", controller))
        m_enabled = !m_enabled;
    if(ui.button("viewport", m_compact ? "Large viewport" : "Small viewport", controller))
        m_compact = !m_compact;
    if(ui.button("clipboard", "Seed CRLF", controller) && !seedClipboard())
        return false;
    if(!ui.endContainer())
        return false;
    TextAreaOptions options;
    options.width = { LayoutSizePolicy::Fixed, m_compact ? 120.0f : 380.0f };
    options.height = { LayoutSizePolicy::Fixed, m_compact ? 72.0f : 160.0f };
    options.enabled = m_enabled;
    options.readOnly = m_readOnly;
    const EditBoxResult result = ui.textArea("document", m_model, m_state, options);
    if(!result.valid)
        return false;
    m_submits += result.submitted ? 1u : 0u;
    m_cancels += result.cancelled ? 1u : 0u;
    m_blurs += result.blurred ? 1u : 0u;
    m_abandons += result.abandoned ? 1u : 0u;
    const WidgetOptions outside{ { LayoutSizePolicy::Fixed, 160.0f }, { LayoutSizePolicy::Fixed, 30.0f } };
    if(ui.button("outside", "Outside focus", outside))
        ++m_outside;
    if(!ui.label("hint", "Enter: LF / Ctrl+Enter: submit") || !ui.endPanel())
        return false;
    if(!observeState(context))
        return false;
    paintMarkers(context);
    return true;
}

void UiTextAreaSmokeScene::resetModel(){
    const b8 initialized = m_model.setText("abcdef\nx\nabcdef\n한국어") && m_model.setSelection(5u, 5u);
    NWB_FATAL_ASSERT(initialized);
    m_state.reset();
    m_enabled = true;
    m_readOnly = false;
    m_compact = false;
    m_longDocument = false;
}

void UiTextAreaSmokeScene::longDocument(){
    const b8 replaced = m_model.setText(m_longText) && m_model.setSelection(1039u, 1039u);
    NWB_FATAL_ASSERT(replaced);
    m_longDocument = true;
}

b8 UiTextAreaSmokeScene::seedClipboard(){
    if(m_clipboardToken.valid() && !m_clipboard.cancel(m_clipboardToken))
        return false;
    m_clipboardToken = {};
    const auto request = m_clipboard.requestWriteText(Core::ClipboardChannel::Clipboard, "ab\r\ncdef\r\nxy");
    if(!request || !request->valid())
        return false;
    m_clipboardToken = *request;
    return true;
}

b8 UiTextAreaSmokeScene::drainClipboard(){
    if(!m_clipboardToken.valid())
        return true;
    const auto status = m_clipboard.poll(m_clipboardToken, m_completion);
    if(status == Core::ClipboardPollResult::Pending)
        return true;
    if(
        status != Core::ClipboardPollResult::Completed || m_completion.token != m_clipboardToken
        || m_completion.operation != Core::ClipboardOperation::WriteText
        || m_completion.channel != Core::ClipboardChannel::Clipboard || m_completion.status != Core::ClipboardStatus::Success
    )
        return false;
    m_clipboardToken = {};
    ++m_clipboardSeeds;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SharedUiTextAreaSmokeScene CreateUiTextAreaSmokeScene(Core::Alloc::GlobalArena& arena, Core::IClipboardService& clipboard){
    return SharedUiTextAreaSmokeScene(NewArenaObject<RefCounter<UiTextAreaSmokeScene>>(arena, arena, clipboard), ArenaRefDeleter<RefCounter<UiTextAreaSmokeScene>, Core::Alloc::GlobalArena>(&arena), s_AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_text_area_snapshot{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr Array<TStringView, 16> s_RectNames = { NWB_TEXT("bounds"), NWB_TEXT("content"), NWB_TEXT("clip"), NWB_TEXT("caret"),
    NWB_TEXT("reset"), NWB_TEXT("long"), NWB_TEXT("readonly"), NWB_TEXT("enabled"), NWB_TEXT("viewport"),
    NWB_TEXT("clipboard"), NWB_TEXT("outside"), NWB_TEXT("x_track"), NWB_TEXT("x_thumb"), NWB_TEXT("y_track"),
    NWB_TEXT("y_thumb"), NWB_TEXT("corner") };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 UiTextAreaSmokeScene::observeState(Impl::UiPaintContext& context){
    using namespace Impl::Ui;
    using namespace __hidden_ui_text_area_snapshot;
    if(!m_observed.snapshot(m_model) || m_observed.shape(context.text, { .fontSize = 16.0f }) != TextLayoutStatus::Success)
        return false;
    if(!m_observed.measure(context.text, context.display))
        return false;
    const EditBoxPlacement& placement = m_state.placement();
    const ScrollViewportPlacement& scrollbars = m_state.scrollbars();
    const auto& geometry = m_observed.caretGeometry();
    const auto localCaret = geometry.caretRect(m_observed.displayCaret());
    if(!localCaret || geometry.lines().size() > 32u)
        return false;
    UiTextAreaSnapshot current;
    current.display = context.display;
    current.scroll = m_state.scroll();
    // Scroll maxima use the measured visual envelope, including glyph overhang.
    const Rect& visualBounds = m_observed.visualBounds();
    current.measure = { visualBounds.width, visualBounds.height };
    current.maximum = { static_cast<f32>(scrollbars.horizontal.maximum), static_cast<f32>(scrollbars.vertical.maximum) };
    current.lineHeight = localCaret->height;
    current.rectangles[0u] = placement.bounds;
    current.rectangles[1u] = placement.content;
    current.rectangles[2u] = placement.clip;
    current.rectangles[3u] = placement.caret;
    const SIMDVectorSingle labelOrigin = VectorSet(placement.bounds.x, placement.bounds.y, 112.0f, 30.0f);
    for(u32 index = 0u; index < 6u; ++index){
        // Lanes are column step and row offset.
        const SIMDVectorSingle lifted = VectorSubtract(labelOrigin, VectorSet(0.0f, index < 3u ? 76.0f : 38.0f, 0.0f, 0.0f));
        current.rectangles[4u + index] = { VectorGetX(labelOrigin) + static_cast<f32>(index % 3u) * 120.0f, VectorGetY(lifted), VectorGetZ(lifted), VectorGetW(lifted) };
    }
    current.rectangles[10u] = { placement.bounds.x, placement.bounds.y + placement.bounds.height + 8.0f, 160.0f, 30.0f };
    current.rectangles[11u] = scrollbars.horizontal.track;
    current.rectangles[12u] = scrollbars.horizontal.thumb;
    current.rectangles[13u] = scrollbars.vertical.track;
    current.rectangles[14u] = scrollbars.vertical.thumb;
    current.rectangles[15u] = scrollbars.corner;
    for(u32 index = 0u; index < geometry.lines().size(); ++index){
        auto rectangle = geometry.rangeOnLine(m_observed.selectionRange(), index, placement.caret.width);
        if(!rectangle)
            return false;
        if(rectangle->width == 0.0f)
            continue;
        rectangle->x += placement.textOrigin.x;
        rectangle->y += placement.textOrigin.y;
        current.selections[current.selectionCount++] = *rectangle;
    }
    const f32 caretErrorX = Abs((placement.caret.x - placement.textOrigin.x) - localCaret->x);
    const f32 caretErrorY = Abs((placement.caret.y - placement.textOrigin.y) - localCaret->y);
    const b8 coherent = caretErrorX < 0.02f
        && caretErrorY < 0.02f
        && current.scroll.x == placement.scroll && current.scroll.y == placement.scrollY
        && SameSmokeRect(placement.content, scrollbars.viewport) && SameSmokeRect(placement.clip, scrollbars.contentClip)
        && current.scroll.x == scrollbars.horizontal.offset && current.scroll.y == scrollbars.vertical.offset;
    const auto& navigation = m_state.navigation();
    current.values = { static_cast<u64>(m_model.text().size()), ComputeFnv32TextHash(m_model.text()), static_cast<u64>(m_model.anchor()),
        static_cast<u64>(m_model.caret()), static_cast<u64>(m_state.focused()), static_cast<u64>(m_enabled),
        static_cast<u64>(m_readOnly), static_cast<u64>(m_compact), static_cast<u64>(m_longDocument),
        static_cast<u64>(navigation.hasPreferredX()), BitCast<u32>(navigation.preferredX()), m_submits, m_cancels, m_blurs,
        m_abandons, m_outside, m_clipboardSeeds, static_cast<u64>(m_clipboardToken.valid()), m_model.revision(),
        m_model.externalRevision(), m_model.selectionGeneration(), static_cast<u64>(coherent), geometry.lines().size(),
        static_cast<u64>(m_model.canUndo()), static_cast<u64>(m_model.canRedo()) };
    b8 changed = current.values != m_snapshot.values || current.selectionCount != m_snapshot.selectionCount
        || current.scroll.x != m_snapshot.scroll.x || current.scroll.y != m_snapshot.scroll.y
        || current.maximum.x != m_snapshot.maximum.x || current.maximum.y != m_snapshot.maximum.y
        || current.measure.x != m_snapshot.measure.x || current.measure.y != m_snapshot.measure.y || current.lineHeight != m_snapshot.lineHeight;
    for(usize index = 0u; index < current.rectangles.size(); ++index)
        changed |= !SameSmokeRect(current.rectangles[index], m_snapshot.rectangles[index]);
    for(usize index = 0u; index < current.selections.size(); ++index)
        changed |= !SameSmokeRect(current.selections[index], m_snapshot.selections[index]);
    const auto& previous = m_snapshot.display;
    const b8 displayChanged = context.display.logicalWidth != previous.logicalWidth || context.display.logicalHeight != previous.logicalHeight || context.display.pixelScaleX != previous.pixelScaleX || context.display.pixelScaleY != previous.pixelScaleY;
    if(m_snapshot.sequence != 0u && !changed && !displayChanged)
        return true;
    current.sequence = m_snapshot.sequence + 1u;
    m_snapshot = current;
    if(displayChanged){
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiTextAreaSmoke: display logical={}x{} scale={}x{}"), current.display.logicalWidth, current.display.logicalHeight, current.display.pixelScaleX, current.display.pixelScaleY);
    }
    const auto& value = current.values;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiTextAreaSmoke: state sequence={} values={},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{}")
        , current.sequence, value[0], value[1], value[2], value[3], value[4], value[5], value[6], value[7], value[8], value[9]
        , value[10], value[11], value[12], value[13], value[14], value[15], value[16], value[17], value[18], value[19]
        , value[20], value[21], value[22], value[23], value[24]
    );
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiTextAreaSmoke: metrics sequence={} scroll={},{} measure={},{} line_height={} selections={} maximum={},{}")
        , current.sequence, current.scroll.x, current.scroll.y, current.measure.x, current.measure.y
        , current.lineHeight, current.selectionCount, current.maximum.x, current.maximum.y
    );
    for(usize index = 0u; index < current.rectangles.size(); ++index)
        LogSmokeRect(NWB_TEXT("UiTextAreaSmoke"), current.sequence, s_RectNames[index], current.rectangles[index]);
    for(u32 index = 0u; index < current.selectionCount; ++index){
        const Rect& rectangle = current.selections[index];
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiTextAreaSmoke: geometry sequence={} selection_{}={},{},{},{}"), current.sequence, index, rectangle.x, rectangle.y, rectangle.width, rectangle.height);
    }
    return true;
}

void UiTextAreaSmokeScene::paintMarkers(Impl::UiPaintContext& context)const{
    const f32 y = context.display.logicalHeight - 18.0f;
    usize marker = 0u;
    for(usize index = 0u; index <= m_snapshot.values.size(); ++index){
        const u64 value = index == m_snapshot.values.size() ? m_snapshot.sequence : m_snapshot.values[index];
        const u32 parts = index == 1u || index == 10u || (index >= 18u && index <= 20u) ? 3u : 2u;
        for(u32 part = 0u; part < parts; ++part){
            context.paint.fillRect({ static_cast<f32>(marker) * 8.0f + 8.0f, y, 6.0f, 10.0f }, EncodeSmokeColor(value >> (part * 12u)));
            ++marker;
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_text_samples{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr AStringView s_LatinFontPath = "tests/smoke/ui_layer/coverage_font";
static constexpr StringView s_Korean = "\xED\x95\x9C\xEA\xB8\x80 \xEC\xA1\xB0\xED\x95\xA9";
static constexpr u64 s_CoverageAtlasIdentity = 0x5549534D4F4B45u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Expected<Impl::Ui::SharedGlyphPage> MakeCoveragePage(Core::Alloc::GlobalArena& arena){
    const Impl::Ui::GlyphPageBinding binding{
        .font = Core::Assets::AssetRef<Impl::Font>(__hidden_ui_text_samples::s_LatinFontPath),
        .fontGeneration = 1u,
        .atlasIdentity = s_CoverageAtlasIdentity,
        .generation = 1u,
        .index = static_cast<u32>(Impl::Ui::s_GlyphAtlasMaxPages - 1u),
        .width = 12u,
        .height = 4u
    };
    Impl::Ui::GlyphPage::Pixels pixels(arena);
    pixels.resize(48u);
    for(u32 y = 0u; y < binding.height; ++y){
        for(u32 x = 0u; x < binding.width; ++x)
            pixels[y * binding.width + x] = x < 4u ? 0u : (x < 8u ? 128u : 255u);
    }
    return Impl::Ui::CreateGlyphPage(arena, binding, Move(pixels));
}

[[nodiscard]] static Expected<Impl::Ui::SharedSdfAtlasPage> MakeSdfPage(Core::Alloc::GlobalArena& arena, const u32 channelCount){
    const Impl::Ui::SdfAtlasPageBinding binding{
        .font = Core::Assets::AssetRef<Impl::Font>(__hidden_ui_text_samples::s_LatinFontPath),
        .fontSha256 = {},
        .pixelsSha256 = {},
        .fontGeneration = 1u,
        .atlasIdentity = s_CoverageAtlasIdentity,
        .generation = 1u,
        .index = channelCount - 1u,
        .width = 12u,
        .height = 4u,
        .channelCount = channelCount,
        .spreadPixels = 8u
    };
    Impl::Ui::SdfAtlasPage::Pixels pixels(arena);
    pixels.resize(12u * 4u * channelCount);
    for(u32 y = 0u; y < binding.height; ++y){
        for(u32 x = 0u; x < binding.width; ++x){
            const usize offset = (y * binding.width + x) * channelCount;
            for(u32 channel = 0u; channel < channelCount; ++channel)
                pixels[offset + channel] = channel == 3u ? (x < 4u ? 0u : (x < 8u ? 128u : 255u)) : (channel == 0u ? 0u : (channel == 1u ? 128u : 255u));
        }
    }
    return Impl::Ui::CreateSdfAtlasPage(arena, binding, Move(pixels));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UiTextSmokeSamples::UiTextSmokeSamples(Core::Alloc::GlobalArena& arena)
    : m_latin(arena)
    , m_korean(arena)
    , m_clipped(arena)
{
    auto coverageResult = __hidden_ui_text_samples::MakeCoveragePage(arena);
    if(coverageResult)
        m_coverage = Move(*coverageResult);
    for(u32 channels = 1u; channels <= 4u; ++channels){
        auto pageResult = __hidden_ui_text_samples::MakeSdfPage(arena, channels);
        if(pageResult)
            m_sdf[channels - 1u] = Move(*pageResult);
    }
    const b8 configured = m_latin.setText({ .text = "office ffi e\xCC\x81 gqyp", .fontSize = 22.0f }) == Impl::Ui::TextLayoutStatus::Success && m_clipped.setText({ .text = "Clipped coverage label", .fontSize = 22.0f }) == Impl::Ui::TextLayoutStatus::Success
        && m_korean.setText({ .text = __hidden_ui_text_samples::s_Korean, .language = "ko", .fontSize = 22.0f, .scriptTag = ::PackFourCharTag('H', 'a', 'n', 'g') }) == Impl::Ui::TextLayoutStatus::Success;
    NWB_FATAL_ASSERT_MSG(configured && m_coverage, NWB_TEXT("UI smoke text and coverage fixture must be valid"));
    for(const Impl::Ui::SharedSdfAtlasPage& page : m_sdf)
        NWB_FATAL_ASSERT_MSG(page, NWB_TEXT("UI smoke compact SDF fixtures must be valid"));
}

b8 UiTextSmokeSamples::paint(Impl::UiPaintContext& context){
    Impl::Ui::PaintBuilder& paint = context.paint;
    // Lanes are width, height, width and height of the display.
    const SIMDVectorSingle extent = VectorSet(context.display.logicalWidth, context.display.logicalHeight, context.display.logicalWidth, context.display.logicalHeight);
    if(!m_latin.paint(context.text, paint, Impl::Ui::StoreRect(VectorMultiply(extent, VectorSet(0.04f, 0.80f, 0.28f, 0.075f)))) || !m_korean.paint(context.text, paint, Impl::Ui::StoreRect(VectorMultiply(extent, VectorSet(0.04f, 0.875f, 0.28f, 0.075f)))))
        return false;

    if(!m_clipped.paint(context.text, paint, Impl::Ui::StoreRect(VectorMultiply(extent, VectorSet(0.34f, 0.82f, 0.21f, 0.08f)))))
        return false;

    const Impl::Ui::Color tint{ 1.0f, 0.2f, 0.6f, 0.5f };
    for(u32 i = 0u; i < 3u; ++i){
        const SIMDVectorSingle factors = VectorMultiplyAddExpression(VectorSet(static_cast<f32>(i), 0.0f, 0.0f, 0.0f), VectorSet(0.08f, 0.0f, 0.0f, 0.0f), VectorSet(0.64f, 0.84f, 0.05f, 0.06f));
        const Impl::Ui::Rect rectangle = Impl::Ui::StoreRect(VectorMultiply(extent, factors));
        const Impl::Ui::Rect uv{ static_cast<f32>(i) / 3.0f, 0.0f, 1.0f / 3.0f, 1.0f };
        if(!paint.drawGlyph(m_coverage, rectangle, uv, tint))
            return false;
    }
    for(u32 channel = 0u; channel < 4u; ++channel){
        const SIMDVectorSingle factors = VectorMultiplyAddExpression(VectorSet(static_cast<f32>(channel), 0.0f, 0.0f, 0.0f), VectorSet(0.08f, 0.0f, 0.0f, 0.0f), VectorSet(0.64f, 0.94f, 0.05f, 0.045f));
        const Impl::Ui::Rect rectangle = Impl::Ui::StoreRect(VectorMultiply(extent, factors));
        if(!paint.drawSdfGlyph(m_sdf[channel], channel, rectangle, { 0.f, 0.f, 1.f, 1.f }, { 0.2f, 1.f, 0.4f, 0.5f }))
            return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SharedUiTextSmokeSamples CreateUiTextSmokeSamples(Core::Alloc::GlobalArena& arena){
    return SharedUiTextSmokeSamples(NewArenaObject<RefCounter<UiTextSmokeSamples>>(arena, arena), ArenaRefDeleter<RefCounter<UiTextSmokeSamples>, Core::Alloc::GlobalArena>(&arena), s_AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_texture_image_scene{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Name s_AlternateSkinNickname = NWB::Tests::Smoke::s_SmokeNameAlternateSkin;
inline constexpr TStringView s_OwnerName = NWB_TEXT("UiTextureImageSmokeScene");
static constexpr Impl::Ui::Color s_Backdrop{ 0.07f, 0.13f, 0.2f, 1.0f };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Expected<Impl::Ui::SharedImageSource> LoadSkinImageSource(
    Core::Alloc::GlobalArena& arena,
    Core::Assets::AssetManager& assets,
    const Core::Assets::AssetRef<Impl::UiSkin>& skinRef
){
    if(!skinRef.valid())
        return MakeUnexpected(Failure{});
    const auto skin = assets.loadTypedSync<Impl::UiSkin>(skinRef.name(), s_OwnerName);
    if(!skin || !*skin || !(*skin)->texture().valid())
        return MakeUnexpected(Failure{});
    return Impl::Ui::LoadImageSource(arena, assets, (*skin)->texture());
}

[[nodiscard]] static Impl::Ui::Rect TextureUv(const f32 x, const f32 y, const f32 width = 24.0f)noexcept{
    const SIMDVectorSingle uv = VectorScale(VectorSet(x, y, width, 24.0f), 1.0f / 256.0f);
    return { VectorGetX(uv), VectorGetY(uv), VectorGetZ(uv), VectorGetW(uv) };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UiTextureImageSmokeScene::UiTextureImageSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input, Core::Assets::AssetManager& assets)
    : m_arena(arena)
    , m_input(input)
{
    using namespace __hidden_ui_texture_image_scene;
    Impl::ActiveSceneSubsystems sceneSubsystems(arena);
    if(sceneSubsystems.loadProjectStartScene(assets)){
        if(const Impl::UiSubsystem* const uiSubsystem = sceneSubsystems.findSubsystem<Impl::UiSubsystem>()){
            auto defaultSource = LoadSkinImageSource(arena, assets, uiSubsystem->basicSkin);
            if(defaultSource)
                m_default = Move(*defaultSource);
        }
        auto alternateSource = LoadSkinImageSource(arena, assets, sceneSubsystems.findSceneAsset<Impl::UiSkin>(s_AlternateSkinNickname));
        if(alternateSource)
            m_alternate = Move(*alternateSource);
    }
    m_replacement = m_default;
    m_sourcesValid = m_default && m_alternate && m_default->texture().width() == 256u
        && m_default->texture().height() == 256u && m_alternate->texture().width() == 256u
        && m_alternate->texture().height() == 256u && m_default->generation() != m_alternate->generation();
    m_input.addHandlerToBack(*this);
}

UiTextureImageSmokeScene::~UiTextureImageSmokeScene()noexcept{
    m_input.removeHandler(*this);
}

b8 UiTextureImageSmokeScene::paint(Impl::UiPaintContext& context){
    if(!m_sourcesValid)
        return false;
    if(m_evictionRemaining != 0u){
        if(!replaceVersion()){
            m_sourcesValid = false;
            return false;
        }
        --m_evictionRemaining;
        ++m_evictionCompleted;
    }
    auto& ui = context.ui;
    ui.style().fontSize = 14.0f;
    ui.style().gap = 8.0f;
    m_declared = {};
    const PassiveSmokeColumns columns = ResolvePassiveSmokeColumns(context.display);
    const f32 width = columns.mainWidth;
    const f32 right = columns.rightColumn;
    const f32 otherWidth = columns.rightWidth;
    if(!paintControls(ui, { 24.0f, 24.0f, width, 112.0f }) || !paintImages(context, width, right, otherWidth) || !paintBuilderImages(ui, { right, 24.0f, otherWidth, 112.0f }) || !paintPopups(context, right) || ui.failed())
        return false;
    observeState(context);
    paintMarkers(context);
    return true;
}

::NWB::Core::KeyboardInputResult UiTextureImageSmokeScene::keyboardUpdate(const i32 key, const i32, const i32 action, const i32){
    if(key != Core::Key::F4 && key != Core::Key::F5 && key != Core::Key::F6 && key != Core::Key::F7)
        return false;
    if(action != Core::InputAction::Press)
        return true;
    if(key == Core::Key::F4)
        m_phase = !m_phase;
    else if(key == Core::Key::F6)
        m_sourcesValid = replaceVersion();
    else if(key == Core::Key::F7){
        m_evictionRemaining = 72u;
        m_evictionCompleted = 0u;
    }
    else if(m_parent.isOpen()){
        m_child.close();
        m_parent.close();
    }
    else{
        m_parent.open();
        m_child.open();
    }
    return true;
}

b8 UiTextureImageSmokeScene::replaceVersion(){
    if(!m_default || !m_alternate || !m_replacement)
        return false;
    const Impl::Texture& payload = m_replacementAlternate ? m_default->texture() : m_alternate->texture();
    Impl::Texture replacement(m_arena, m_default->identity().name());
    Impl::Texture::MipLevelVector mipLevels(m_arena);
    mipLevels.assign(payload.mipLevels().begin(), payload.mipLevels().end());
    Core::Assets::AssetBytes bytes(m_arena);
    bytes.assign(payload.payloadBytes().begin(), payload.payloadBytes().end());
    replacement.setPayload(payload.width(), payload.height(), Move(mipLevels), Move(bytes), payload.dimension(), payload.depth(), payload.payloadFormat(), payload.alphaMode(), payload.alphaConstantUnorm8());
    auto nextSource = Impl::Ui::MakeImageSource(m_arena, replacement);
    if(!nextSource)
        return false;
    Impl::Ui::SharedImageSource next = Move(*nextSource);
    if(!next || next->generation() == m_replacement->generation() || next->generation() == m_default->generation() || next->generation() == m_alternate->generation() || next->identity().name() != m_default->identity().name())
        return false;
    m_replacement = Move(next);
    ++m_replacementCount;
    m_replacementAlternate = !m_replacementAlternate;
    return true;
}

b8 UiTextureImageSmokeScene::paintControls(Impl::Ui::Builder& ui, const Impl::Ui::Rect& bounds){
    using namespace Impl::Ui;
    if(!ui.beginPanel("texture_image_controls", bounds))
        return false;
    const WidgetOptions caption{ {}, { LayoutSizePolicy::Fixed, 16.0f } };
    const WidgetOptions button{ { LayoutSizePolicy::Stretch, 1.0f }, { LayoutSizePolicy::Fixed, 28.0f } };
    if(!ui.label("title", "Owned engine textures / mixed images and skin", caption))
        return false;
    if(ui.button("before", "Before passive engine images", button))
        ++m_beforeClicks;
    if(ui.button("after", "After passive engine images", button))
        ++m_afterClicks;
    return ui.endPanel();
}

b8 UiTextureImageSmokeScene::paintImages(Impl::UiPaintContext& context, const f32 width, const f32 right, const f32 otherWidth){
    using namespace Impl::Ui;
    using namespace __hidden_ui_texture_image_scene;
    auto& paint = context.paint;
    paint.fillRect({ 24.0f, 152.0f, width, 288.0f }, s_Backdrop);
    paint.fillRect({ right, 152.0f, otherWidth, 288.0f }, s_Backdrop);
    auto& rectangles = m_declared.rectangles;
    rectangles[2u] = { 32.0f, 168.0f, 96.0f, 64.0f };
    rectangles[3u] = { 140.0f, 168.0f, 96.0f, 64.0f };
    rectangles[4u] = { 248.0f, 168.0f, 96.0f, 64.0f };
    rectangles[5u] = { 32.0f, 240.0f, 96.0f, 32.0f };
    // Lanes are column x, column width and half column width.
    const SIMDVectorSingle column = VectorMultiply(VectorAdd(VectorSet(right, otherWidth, otherWidth, 0.0f), VectorSet(8.0f, -16.0f, -16.0f, 0.0f)), VectorSet(1.0f, 1.0f, 0.5f, 1.0f));
    const f32 columnX = VectorGetX(column);
    const f32 columnWidth = VectorGetY(column);
    rectangles[6u] = { columnX, 168.0f, columnWidth, 64.0f };
    rectangles[7u] = { columnX, 240.0f, columnWidth, 32.0f };
    rectangles[8u] = { columnX, 240.0f, VectorGetZ(column), 32.0f };
    rectangles[9u] = { 32.0f, 280.0f, 256.0f, 128.0f };
    rectangles[10u] = { columnX, 280.0f, 256.0f, 128.0f };
    rectangles[11u] = { columnX, 416.0f, columnWidth, 24.0f };
    if(
        !paint.drawImage(m_default, rectangles[2u], TextureUv(4.0f, 4.0f))
        || !paint.drawImage(m_alternate, rectangles[3u], TextureUv(196.0f, 100.0f))
        || !paint.drawImage(m_default, rectangles[4u], TextureUv(4.0f, 132.0f), { 0.4f, 0.7f, 0.9f, 0.5f })
        || !paint.drawImage(m_default, rectangles[5u], TextureUv(4.0f, 4.0f), { 1.0f, 1.0f, 1.0f, 0.0f })
    )
        return false;
    SharedImageSource declaredSource = m_phase ? m_alternate : m_replacement;
    Rect rectangle = rectangles[6u];
    Rect uv = m_phase ? TextureUv(196.0f, 100.0f) : TextureUv(4.0f, 4.0f);
    Color tint = m_phase ? Color{ 0.5f, 0.75f, 1.0f, 0.5f } : Color{};
    m_declared.values[8u] = m_phase ? 2u : 1u;
    if(!paint.drawImage(declaredSource, rectangle, uv, tint))
        return false;
    declaredSource = m_phase ? m_default : m_alternate;
    declaredSource.reset();
    rectangle = { 0.0f, 0.0f, 1.0f, 1.0f };
    uv = { 0.0f, 0.0f, 1.0f, 1.0f };
    tint = { 0.0f, 0.0f, 0.0f, 1.0f };
    m_declared.values[9u] = static_cast<u64>(!declaredSource);
    paint.pushClip(rectangles[8u]);
    const b8 clipped = paint.drawImage(m_default, rectangles[7u], TextureUv(16.0f, 4.0f, 12.0f));
    const b8 popped = paint.popClip();
    if(!clipped || !popped)
        return false;
    return paint.drawImage(m_default, rectangles[9u]) && paint.drawImage(m_alternate, rectangles[10u]) && paint.drawRegion(Name(s_UiSmokeNameProgressFill), rectangles[11u]);
}

b8 UiTextureImageSmokeScene::paintBuilderImages(Impl::Ui::Builder& ui, const Impl::Ui::Rect& bounds){
    using namespace Impl::Ui;
    if(!ui.beginPanel("texture_image_builder", bounds))
        return false;
    ImageOptions options;
    options.width = { LayoutSizePolicy::Stretch, 1.0f };
    options.height = { LayoutSizePolicy::Fixed, 80.0f };
    options.tint = m_phase ? Color{ 0.5f, 0.75f, 1.0f, 0.5f } : Color{};
    SharedImageSource source = m_phase ? m_alternate : m_replacement;
    const SIMDVectorSingle content = VectorAdd(VectorSet(bounds.x, bounds.y, bounds.width, 0.0f), VectorSet(8.0f, 8.0f, -16.0f, 80.0f));
    m_declared.rectangles[16u] = { VectorGetX(content), VectorGetY(content), VectorGetZ(content), VectorGetW(content) };
    m_declared.values[14u] = m_phase ? 2u : 1u;
    if(!ui.image("owned_source", source, options))
        return false;
    source = m_phase ? m_default : m_alternate;
    source.reset();
    options.width = { LayoutSizePolicy::Fixed, 1.0f };
    options.height = { LayoutSizePolicy::Fixed, 1.0f };
    options.tint = { 0.0f, 0.0f, 0.0f, 1.0f };
    m_declared.values[15u] = static_cast<u64>(!source);
    return ui.endPanel();
}

b8 UiTextureImageSmokeScene::paintPopups(Impl::UiPaintContext& context, const f32 right){
    using namespace Impl::Ui;
    auto& ui = context.ui;
    PopupOptions parent;
    parent.anchor = { right, 300.0f, 8.0f, 24.0f };
    parent.size = { 280.0f, 128.0f };
    if(!ui.beginPopup("texture_image_parent", m_parent, parent))
        return !ui.failed();
    const WidgetOptions button{ { LayoutSizePolicy::Stretch, 1.0f }, { LayoutSizePolicy::Fixed, 28.0f } };
    if(ui.button("parent_action", "Parent action", button))
        m_parent.close();
    ImageOptions options;
    options.width = { LayoutSizePolicy::Stretch, 1.0f };
    options.height = { LayoutSizePolicy::Fixed, 64.0f };
    SharedImageSource source = m_alternate;
    if(!ui.image("parent_source", source, options))
        return false;
    source.reset();
    PopupOptions child;
    child.anchor = { right + 100.0f, 370.0f, 8.0f, 16.0f };
    child.size = { 240.0f, 128.0f };
    if(ui.beginPopup("texture_image_child", m_child, child)){
        source = m_default;
        options.tint = { 0.5f, 1.0f, 0.5f, 0.75f };
        if(!ui.image("child_source", source, options))
            return false;
        source.reset();
        if(ui.button("child_action", "Child action", button))
            m_child.close();
        if(!ui.endPopup())
            return false;
    }
    options.width = { LayoutSizePolicy::Fixed, 1.0f };
    options.height = { LayoutSizePolicy::Fixed, 1.0f };
    options.tint = { 0.0f, 0.0f, 0.0f, 1.0f };
    if(ui.failed() || !ui.endPopup())
        return false;
    m_declared.rectangles[14u] = m_parent.placement().bounds;
    const Rect& parentBounds = m_declared.rectangles[14u];
    const SIMDVectorSingle parentRow = VectorAdd(VectorSet(parentBounds.x, parentBounds.y, parentBounds.width, 0.0f), VectorSet(8.0f, 44.0f, -16.0f, 64.0f));
    m_declared.rectangles[12u] = { VectorGetX(parentRow), VectorGetY(parentRow), VectorGetZ(parentRow), VectorGetW(parentRow) };
    if(m_child.isOpen()){
        m_declared.rectangles[15u] = m_child.placement().bounds;
        const Rect& childBounds = m_declared.rectangles[15u];
        const SIMDVectorSingle childRow = VectorAdd(VectorSet(childBounds.x, childBounds.y, childBounds.width, 0.0f), VectorSet(8.0f, 8.0f, -16.0f, 64.0f));
        m_declared.rectangles[13u] = { VectorGetX(childRow), VectorGetY(childRow), VectorGetZ(childRow), VectorGetW(childRow) };
    }
    // Both late base commands must remain beneath the actual deferred Builder popup chrome and source images.
    context.paint.fillRect(m_declared.rectangles[12u], { 0.8f, 0.0f, 0.0f, 1.0f });
    if(m_child.isOpen())
        context.paint.fillRect(m_declared.rectangles[13u], { 0.0f, 0.0f, 0.8f, 1.0f });
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 IsUiLayerTextureImageSmokeEnabled(){
    return ReadSmokeEnvironmentFlag("NWB_UI_LAYER_TEXTURE_IMAGE");
}

b8 IsUiLayerTextureImageSkinSmokeEnabled(){
    return ReadSmokeEnvironmentFlag("NWB_UI_LAYER_TEXTURE_IMAGE_SKIN");
}

SharedUiTextureImageSmokeScene CreateUiTextureImageSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input, Core::Assets::AssetManager& assets){
    return SharedUiTextureImageSmokeScene(NewArenaObject<RefCounter<UiTextureImageSmokeScene>>(arena, arena, input, assets), ArenaRefDeleter<RefCounter<UiTextureImageSmokeScene>, Core::Alloc::GlobalArena>(&arena), s_AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_texture_image_snapshot{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr Array<TStringView, 17u> s_RectNames{
    NWB_TEXT("before"), NWB_TEXT("after"), NWB_TEXT("default_tile"), NWB_TEXT("alternate_tile"), NWB_TEXT("tinted"),
    NWB_TEXT("zero_alpha"), NWB_TEXT("frozen"), NWB_TEXT("external"), NWB_TEXT("external_clip"),
    NWB_TEXT("default_atlas"), NWB_TEXT("alternate_atlas"), NWB_TEXT("skin_fill"), NWB_TEXT("parent_image"),
    NWB_TEXT("child_image"), NWB_TEXT("parent"), NWB_TEXT("child"), NWB_TEXT("builder_image")
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void UiTextureImageSmokeScene::observeState(Impl::UiPaintContext& context){
    using namespace Impl::Ui;
    using namespace __hidden_ui_texture_image_snapshot;
    UiTextureImageSnapshot current = m_declared;
    current.display = context.display;
    const InputRouter& input = context.ui.input();
    const auto observation = ObservePassiveSmokeTargets(
        input, m_parent.instanceGeneration(), m_child.instanceGeneration(), 2u, current.rectangles.size(),
        [](const usize index)noexcept{ return index != 8u && index != 14u && index != 15u; }, current.rectangles
    );
    const u32 focusCode = observation.focusCode;
    const u32 imageTargets = observation.passiveTargets;
    current.values[0u] = static_cast<u64>(m_phase);
    current.values[1u] = focusCode;
    current.values[2u] = m_beforeClicks;
    current.values[3u] = m_afterClicks;
    current.values[4u] = static_cast<u64>(m_parent.isOpen());
    current.values[5u] = static_cast<u64>(m_child.isOpen());
    current.values[6u] = input.popupCount();
    current.values[7u] = imageTargets;
    current.values[10u] = m_replacementCount;
    current.values[11u] = static_cast<u64>(m_replacementAlternate);
    current.values[12u] = m_evictionRemaining;
    current.values[13u] = m_evictionCompleted;
    current.generations = { m_default->generation(), m_alternate->generation(), m_replacement->generation() };
    const auto update = PublishPassiveSmokeSnapshot([&]()noexcept{ return current.values != m_snapshot.values || current.generations != m_snapshot.generations; }, current, m_snapshot);
    if(!update.published)
        return;
    if(update.displayChanged){
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiTextureImageSmoke: display logical={}x{} scale={}x{}"), current.display.logicalWidth, current.display.logicalHeight, current.display.pixelScaleX, current.display.pixelScaleY);
    }
    const auto& value = current.values;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiTextureImageSmoke: state sequence={} values={},{},{},{},{},{},{},") NWB_TEXT("{},{},{},{},{},{},{},{},{}")
        , current.sequence, value[0], value[1], value[2], value[3], value[4], value[5]
        , value[6], value[7], value[8], value[9], value[10], value[11], value[12], value[13], value[14], value[15]
    );
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiTextureImageSmoke: sources sequence={} generations={},{},{} same_identity={}")
        , current.sequence, current.generations[0], current.generations[1], current.generations[2]
        , static_cast<u32>(m_replacement->identity().name() == m_default->identity().name())
    );
    for(usize index = 0u; index < current.rectangles.size(); ++index)
        LogSmokeRect(NWB_TEXT("UiTextureImageSmoke"), current.sequence, s_RectNames[index], current.rectangles[index]);
}

void UiTextureImageSmokeScene::paintMarkers(Impl::UiPaintContext& context)const{
    PaintPassiveSmokeValueMarkers(m_snapshot.values, m_snapshot.sequence, context);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_window_smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr TStringView s_IncreaseLabel = NWB_TEXT("increase");
constexpr AStringView s_IncreaseKey = "increase";
constexpr TStringView s_LockedLabel = NWB_TEXT("locked");
constexpr AStringView s_LockedKey = "locked";
static constexpr Impl::Ui::Color s_TitleAnchor{ 0.70f, 0.02f, 0.35f, 1.0f };
static constexpr Impl::Ui::Color s_ContentAnchor{ 0.02f, 0.30f, 0.75f, 1.0f };
static constexpr f32 s_LabelHeight = 18.0f;
static constexpr f32 s_SeparatorHeight = 1.0f;
static constexpr f32 s_ControlHeight = 26.0f;
static constexpr f32 s_ControlWidth = 80.0f;
static constexpr f32 s_Gap = 4.0f;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Impl::Ui::Rect Button(const Impl::Ui::WindowState& state, const Impl::Ui::WindowMetrics& metrics)noexcept{
    const Impl::Ui::Rect content = Impl::Ui::ResolveWindowContentBounds(state, metrics);
    const f32 originX = content.x + metrics.contentPadding.left;
    const f32 originY = content.y + metrics.contentPadding.top;
    const f32 buttonY = originY + s_LabelHeight + s_SeparatorHeight + 2.0f * s_Gap;
    return { originX, buttonY, s_ControlWidth, s_ControlHeight };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 UiWindowSmokeScene::paint(Impl::UiPaintContext& context){
    using namespace Impl::Ui;
    ObserveSmokeDisplay(NWB_TEXT("UiWindowSmoke"), context.display, m_lastDisplay, m_displayChanged);
    Builder& ui = context.ui;
    ui.style().gap = __hidden_ui_window_smoke::s_Gap;
    ui.style().fontSize = 12.0f;
    WindowOptions options;
    options.initialBounds = { 40.0f, 40.0f, 300.0f, 180.0f };
    options.minimumSize = { 180.0f, 110.0f };
    options.movable = !m_locked;
    options.resizable = !m_locked;
    const b8 contentVisible = ui.beginWindow("window", "Window", m_window, options);
    const WindowMetrics metrics = ui.windowMetrics();
    if(contentVisible){
        const WidgetOptions label{ {}, { LayoutSizePolicy::Fixed, __hidden_ui_window_smoke::s_LabelHeight } };
        if(!ui.label("caption", "Drag, resize or collapse", label))
            return false;
        SeparatorOptions separator;
        separator.thickness = __hidden_ui_window_smoke::s_SeparatorHeight;
        if(!ui.separator("separator", separator))
            return false;
        ContainerOptions row;
        row.height = { LayoutSizePolicy::Fixed, __hidden_ui_window_smoke::s_ControlHeight };
        row.gap = __hidden_ui_window_smoke::s_Gap;
        if(!ui.beginRow("actions", row))
            return false;
        const WidgetOptions control{ { LayoutSizePolicy::Fixed, __hidden_ui_window_smoke::s_ControlWidth }, { LayoutSizePolicy::Fixed, __hidden_ui_window_smoke::s_ControlHeight } };
        if(ui.button(__hidden_ui_window_smoke::s_IncreaseKey, "Increase", control)){
            ++m_count;
            logAction(__hidden_ui_window_smoke::s_IncreaseLabel);
        }
        if(ui.checkbox(__hidden_ui_window_smoke::s_LockedKey, "Lock", m_locked, control))
            logAction(__hidden_ui_window_smoke::s_LockedLabel);
        if(!ui.endContainer())
            return false;
    }
    if(!ui.endWindow())
        return false;
    observeState(metrics);
    paintMarkers(context, metrics);
    return true;
}

void UiWindowSmokeScene::observeState(const Impl::Ui::WindowMetrics& metrics){
    using namespace Impl::Ui;
    const Rect& bounds = m_window.bounds;
    const Rect& previous = m_lastWindow.bounds;
    if(
        m_sequence != 0u && !m_displayChanged && bounds.x == previous.x && bounds.y == previous.y
        && bounds.width == previous.width && bounds.height == previous.height && m_window.collapsed == m_lastWindow.collapsed
        && m_count == m_lastCount && m_locked == m_lastLocked
    )
        return;
    ++m_sequence;
    m_lastWindow = m_window;
    m_lastCount = m_count;
    m_lastLocked = m_locked;
    m_displayChanged = false;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiWindowSmoke: state sequence={} bounds={},{},{},{} collapsed={} locked={} count={}")
        , m_sequence, bounds.x, bounds.y, bounds.width, bounds.height
        , static_cast<u32>(m_window.collapsed), static_cast<u32>(m_locked), m_count
    );
    const Rect title{ bounds.x, bounds.y, bounds.width, metrics.titleHeight };
    const Rect collapse = ResolveWindowCollapseBounds(m_window, metrics);
    const Rect resize = ResolveWindowResizeBounds(m_window, metrics);
    const Rect button = __hidden_ui_window_smoke::Button(m_window, metrics);
    const Rect content = ResolveWindowContentBounds(m_window, metrics);
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiWindowSmoke: geometry sequence={} title={},{},{},{} collapse={},{},{},{} resize={},{},{},{}")
        , m_sequence, title.x, title.y, title.width, title.height
        , collapse.x, collapse.y, collapse.width, collapse.height
        , resize.x, resize.y, resize.width, resize.height
    );
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiWindowSmoke: controls sequence={} button={},{},{},{} lock={},{},{},{} content={},{},{},{} minimum={},{}")
        , m_sequence, button.x, button.y, button.width, button.height
        , button.x + button.width + __hidden_ui_window_smoke::s_Gap, button.y, button.width, button.height
        , content.x, content.y, content.width, content.height, metrics.minimumSize.x, metrics.minimumSize.y
    );
}

void UiWindowSmokeScene::logAction(const TStringView action){
    ++m_actions;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiWindowSmoke: action={} count={} locked={} actions={}"), action, m_count, static_cast<u32>(m_locked), m_actions);
}

void UiWindowSmokeScene::paintMarkers(Impl::UiPaintContext& context, const Impl::Ui::WindowMetrics& metrics)const{
    using namespace Impl::Ui;
    const Rect& bounds = m_window.bounds;
    const Array<f32, 8> values = { bounds.x, bounds.y, bounds.width, bounds.height, static_cast<f32>(m_count), static_cast<f32>(m_window.collapsed), static_cast<f32>(m_locked), static_cast<f32>(m_sequence) };
    const f32 markerY = context.display.logicalHeight - 20.0f;
    for(usize index = 0u; index < values.size(); ++index)
        context.paint.fillRect({ static_cast<f32>(index) * 20.0f + 12.0f, markerY, 14.0f, 12.0f }, EncodeSmokeColor(values[index]));
    // Lanes are anchor x, title anchor y and content anchor y.
    const SIMDVectorSingle anchors = VectorAdd(VectorAdd(VectorSet(bounds.x, bounds.y, bounds.y, 0.0f), VectorSet(bounds.width, 4.0f, metrics.titleHeight, 0.0f)), VectorSet(-14.0f, 0.0f, 4.0f, 0.0f));
    context.paint.pushClip(ResolveVisibleWindowBounds(m_window, metrics));
    context.paint.fillRect({ VectorGetX(anchors), VectorGetY(anchors), 10.0f, 10.0f }, __hidden_ui_window_smoke::s_TitleAnchor);
    if(!m_window.collapsed)
        context.paint.fillRect({ VectorGetX(anchors), VectorGetZ(anchors), 10.0f, 10.0f }, __hidden_ui_window_smoke::s_ContentAnchor);
    const b8 popped = context.paint.popClip();
    NWB_FATAL_ASSERT(popped);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 IsUiLayerWindowSmokeEnabled(){
    return ReadSmokeEnvironmentFlag("NWB_UI_LAYER_WINDOW");
}

b8 IsUiLayerWindowSkinSmokeEnabled(){
    return ReadSmokeEnvironmentFlag("NWB_UI_LAYER_WINDOW_SKIN");
}

SharedUiWindowSmokeScene CreateUiWindowSmokeScene(Core::Alloc::GlobalArena& arena){
    return SharedUiWindowSmokeScene(NewArenaObject<RefCounter<UiWindowSmokeScene>>(arena), ArenaRefDeleter<RefCounter<UiWindowSmokeScene>, Core::Alloc::GlobalArena>(&arena), s_AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_layer_smoke_world{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr Core::Assets::AssetRef<Impl::UiSkin> s_AlternateSkin{"alternate_skin"};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<NotNullUniquePtr<Core::ECS::World>> CreateUiLayerSmokeWorld(ProjectRuntimeContext& context){
    if(!context.shaderPathResolver){
        NWB_LOGGER_ERROR(NWB_TEXT("UiLayerSmokeProject: shader path resolver is unavailable"));
        return MakeUnexpected(Failure{});
    }
    auto world = MakeUnique<Core::ECS::World>(context.objectArena, context.cpuTasks);
    if(!world){
        NWB_LOGGER_ERROR(NWB_TEXT("UiLayerSmokeProject: ECS world allocation failed"));
        return MakeUnexpected(Failure{});
    }


    const b8 windowSmoke = IsUiLayerWindowSmokeEnabled();
    const b8 popupSmoke = IsUiLayerPopupSmokeEnabled();
    const b8 popupToolsSmoke = IsUiLayerPopupToolsSmokeEnabled();
    const b8 nestedPopupSmoke = IsUiLayerNestedPopupSmokeEnabled();
    const b8 radioGroupSmoke = IsUiLayerRadioGroupSmokeEnabled();
    const b8 sliderSmoke = IsUiLayerSliderSmokeEnabled();
    const b8 progressSmoke = IsUiLayerProgressSmokeEnabled();
    const b8 imageSmoke = IsUiLayerImageSmokeEnabled();
    const b8 textureImageSmoke = IsUiLayerTextureImageSmokeEnabled();
    const b8 textAreaSmoke = ReadSmokeEnvironmentFlag("NWB_UI_LAYER_TEXT_AREA");
    const b8 numericEditSmoke = IsUiLayerNumericEditSmokeEnabled();
    const b8 listSmoke = IsUiLayerListSmokeEnabled();
    const b8 comboSmoke = IsUiLayerComboSmokeEnabled();
    const b8 searchComboSmoke = IsUiLayerSearchComboSmokeEnabled();
    const b8 alternateSkin = (windowSmoke && IsUiLayerWindowSkinSmokeEnabled())
        || (popupSmoke && IsUiLayerPopupSkinSmokeEnabled()) || (listSmoke && IsUiLayerListSkinSmokeEnabled())
        || (comboSmoke && IsUiLayerComboSkinSmokeEnabled())
        || (searchComboSmoke && IsUiLayerSearchComboSkinSmokeEnabled())
        || (popupToolsSmoke && IsUiLayerPopupToolsSkinSmokeEnabled())
        || (nestedPopupSmoke && IsUiLayerNestedPopupSkinSmokeEnabled())
        || (numericEditSmoke && IsUiLayerNumericEditSkinSmokeEnabled())
        || (textAreaSmoke && ReadSmokeEnvironmentFlag("NWB_UI_LAYER_TEXT_AREA_SKIN"))
        || (radioGroupSmoke && IsUiLayerRadioGroupSkinSmokeEnabled())
        || (sliderSmoke && IsUiLayerSliderSkinSmokeEnabled())
        || (progressSmoke && IsUiLayerProgressSkinSmokeEnabled())
        || (imageSmoke && IsUiLayerImageSkinSmokeEnabled())
        || (textureImageSmoke && IsUiLayerTextureImageSkinSmokeEnabled());
    const Core::Assets::AssetRef<Impl::UiSkin> skin = alternateSkin ? __hidden_ui_layer_smoke_world::s_AlternateSkin : Core::Assets::AssetRef<Impl::UiSkin>{};
    if(windowSmoke)
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiWindowSmoke: skin={}"), alternateSkin ? NWB_TEXT("alternate") : NWB_TEXT("default"));
    if(popupSmoke)
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiPopupSmoke: skin={}"), alternateSkin ? NWB_TEXT("alternate") : NWB_TEXT("default"));
    if(listSmoke)
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiListSmoke: skin={}"), alternateSkin ? NWB_TEXT("alternate") : NWB_TEXT("default"));
    if(comboSmoke)
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiComboSmoke: skin={}"), alternateSkin ? NWB_TEXT("alternate") : NWB_TEXT("default"));
    if(searchComboSmoke)
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiSearchComboSmoke: skin={}"), alternateSkin ? NWB_TEXT("alternate") : NWB_TEXT("default"));
    if(popupToolsSmoke)
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiPopupToolsSmoke: skin={}"), alternateSkin ? NWB_TEXT("alternate") : NWB_TEXT("default"));
    if(nestedPopupSmoke)
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiNestedPopupSmoke: skin={}"), alternateSkin ? NWB_TEXT("alternate") : NWB_TEXT("default"));
    if(numericEditSmoke)
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiNumericEditSmoke: skin={}"), alternateSkin ? NWB_TEXT("alternate") : NWB_TEXT("default"));
    if(textAreaSmoke)
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiTextAreaSmoke: skin={}"), alternateSkin ? NWB_TEXT("alternate") : NWB_TEXT("default"));
    if(radioGroupSmoke)
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiRadioGroupSmoke: skin={}"), alternateSkin ? NWB_TEXT("alternate") : NWB_TEXT("default"));
    if(sliderSmoke)
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiSliderSmoke: skin={}"), alternateSkin ? NWB_TEXT("alternate") : NWB_TEXT("default"));
    if(progressSmoke)
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiProgressSmoke: skin={}"), alternateSkin ? NWB_TEXT("alternate") : NWB_TEXT("default"));
    if(imageSmoke)
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiImageSmoke: skin={}"), alternateSkin ? NWB_TEXT("alternate") : NWB_TEXT("default"));
    if(textureImageSmoke){
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiTextureImageSmoke: skin={}"), alternateSkin ? NWB_TEXT("alternate") : NWB_TEXT("default"));
    }
    auto& layer = world->addSystem<Impl::UiLayerSystem>(*world, context.graphics, context.input, context.clipboard, context.textInput, context.assetManager, context.shaderPathResolver, skin, Impl::UiLayerPresentation::Standalone);
    if(ReadSmokeEnvironmentFlag("NWB_UI_LAYER_INPUT_BINDINGS_SMOKE")){
        Impl::Ui::InputBindings defaults(context.objectArena);
        Impl::Ui::InputBindings::BindingVector bindings(context.objectArena);
        bindings.reserve(defaults.bindings().size() + 4u);
        for(const auto& binding : defaults.bindings()){
            if(binding.key != Core::Key::Tab)
                bindings.push_back(binding);
        }
        bindings.push_back({ .key = Core::Key::Q, .command = Impl::Ui::InputCommand::Activate });
        bindings.push_back({ .key = Core::Key::W, .command = Impl::Ui::InputCommand::Left });
        bindings.push_back({ .key = Core::Key::R, .command = Impl::Ui::InputCommand::FocusNext });
        bindings.push_back({ .key = Core::Key::T, .command = Impl::Ui::InputCommand::FocusPrevious });
        if(!layer.setInputBindings(bindings)){
            NWB_LOGGER_ERROR(NWB_TEXT("UiLayerSmokeProject: configured input profile was rejected"));
            return MakeUnexpected(Failure{});
        }
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiLayerSmokeProject: custom input profile Q=Activate W=Left R=FocusNext T=FocusPrevious Tab=unbound"));
    }
    if(ReadSmokeEnvironmentFlag("NWB_UI_IR_REPLAY")){
        layer.setGpuCommandRecordingMode(Impl::Ui::GpuCommandRecordingMode::CommandIrReplay);
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiLayerSmokeProject: command IR replay enabled"));
    }
    if(ReadSmokeEnvironmentFlag("NWB_UI_LAYER_MANY_ROOTS")){
        for(usize index = 0u; index < Impl::Ui::s_InputMaxTargets; ++index){
            auto root = world->createEntity();
            root.addComponent<Impl::UiPaintComponent>().paint = [](Impl::UiPaintContext&){};
        }
        auto lastRoot = world->createEntity();
        lastRoot.addComponent<Impl::UiPaintComponent>().paint = [](Impl::UiPaintContext& context){
            const Core::ECS::EntityID id = context.entity;
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiLayerSmokeProject: root beyond target limit painted"));
            context.world.destroyEntity(id);
            if(context.world.entity(id).alive())
                NWB_LOGGER_ERROR(NWB_TEXT("UiLayerSmokeProject: root removal during paint failed"));
            else
                NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiLayerSmokeProject: root removed during paint"));
        };
    }
    auto entity = world->createEntity();
    auto& paint = entity.addComponent<Impl::UiPaintComponent>();
    if(textureImageSmoke){
        const auto scene = CreateUiTextureImageSmokeScene(context.objectArena, context.input, context.assetManager);
        paint.paint = [scene](Impl::UiPaintContext& paintContext){
            if(!scene->paint(paintContext))
                NWB_LOGGER_ERROR(NWB_TEXT("UiLayerSmokeProject: texture-image UI paint failed"));
        };
    }
    else if(imageSmoke){
        paint.paint = [scene = CreateUiImageSmokeScene(context.objectArena, context.input)](Impl::UiPaintContext& paintContext){
            if(!scene->paint(paintContext))
                NWB_LOGGER_ERROR(NWB_TEXT("UiLayerSmokeProject: image UI paint failed"));
        };
    }
    else if(progressSmoke){
        paint.paint = [scene = CreateUiProgressSmokeScene(context.objectArena, context.input)](Impl::UiPaintContext& paintContext){
            if(!scene->paint(paintContext))
                NWB_LOGGER_ERROR(NWB_TEXT("UiLayerSmokeProject: progress UI paint failed"));
        };
    }
    else if(sliderSmoke){
        paint.paint = [scene = CreateUiSliderSmokeScene(context.objectArena, context.input)](Impl::UiPaintContext& paintContext){
            if(!scene->paint(paintContext))
                NWB_LOGGER_ERROR(NWB_TEXT("UiLayerSmokeProject: slider UI paint failed"));
        };
    }
    else if(radioGroupSmoke){
        paint.paint = [scene = CreateUiRadioGroupSmokeScene(context.objectArena, context.input)](Impl::UiPaintContext& paintContext){
            if(!scene->paint(paintContext))
                NWB_LOGGER_ERROR(NWB_TEXT("UiLayerSmokeProject: radio group UI paint failed"));
        };
    }
    else if(textAreaSmoke){
        paint.paint = [scene = CreateUiTextAreaSmokeScene(context.objectArena, context.clipboard)](Impl::UiPaintContext& paintContext){
            if(!scene->paint(paintContext))
                NWB_LOGGER_ERROR(NWB_TEXT("UiLayerSmokeProject: text area UI paint failed"));
        };
    }
    else if(numericEditSmoke){
        paint.paint = [scene = CreateUiNumericEditSmokeScene(context.objectArena, context.input)](Impl::UiPaintContext& paintContext){
            if(!scene->paint(paintContext))
                NWB_LOGGER_ERROR(NWB_TEXT("UiLayerSmokeProject: numeric editor UI paint failed"));
        };
    }
    else if(nestedPopupSmoke){
        paint.paint = [scene = CreateUiNestedPopupSmokeScene(context.objectArena, context.input)](Impl::UiPaintContext& paintContext){
            if(!scene->paint(paintContext))
                NWB_LOGGER_ERROR(NWB_TEXT("UiLayerSmokeProject: nested popup UI paint failed"));
        };
    }
    else if(popupToolsSmoke){
        paint.paint = [scene = CreateUiPopupToolsSmokeScene(context.objectArena, context.input)](Impl::UiPaintContext& paintContext){
            if(!scene->paint(paintContext))
                NWB_LOGGER_ERROR(NWB_TEXT("UiLayerSmokeProject: tooltip/context menu UI paint failed"));
        };
    }
    else if(searchComboSmoke){
        paint.paint = [scene = CreateUiSearchComboSmokeScene(context.objectArena, context.input)](Impl::UiPaintContext& paintContext){
            if(!scene->paint(paintContext))
                NWB_LOGGER_ERROR(NWB_TEXT("UiLayerSmokeProject: searchable combo UI paint failed"));
        };
    }
    else if(comboSmoke){
        paint.paint = [scene = CreateUiComboSmokeScene(context.objectArena, context.input)](Impl::UiPaintContext& paintContext){
            if(!scene->paint(paintContext))
                NWB_LOGGER_ERROR(NWB_TEXT("UiLayerSmokeProject: combo UI paint failed"));
        };
    }
    else if(listSmoke){
        paint.paint = [scene = CreateUiListSmokeScene(context.objectArena, context.input)](Impl::UiPaintContext& paintContext){
            if(!scene->paint(paintContext))
                NWB_LOGGER_ERROR(NWB_TEXT("UiLayerSmokeProject: virtual list paint failed"));
        };
    }
    else if(popupSmoke){
        const auto scene = CreateUiPopupSmokeScene(context.objectArena, context.input);
        paint.paint = [scene](Impl::UiPaintContext& paintContext){
            if(!scene->paint(paintContext))
                NWB_LOGGER_ERROR(NWB_TEXT("UiLayerSmokeProject: popup UI paint failed"));
        };
        auto laterEntity = world->createEntity();
        auto& laterPaint = laterEntity.addComponent<Impl::UiPaintComponent>();
        laterPaint.order = 100;
        laterPaint.paint = [scene](Impl::UiPaintContext& paintContext){
            if(!scene->paintLaterRoot(paintContext))
                NWB_LOGGER_ERROR(NWB_TEXT("UiLayerSmokeProject: later popup root paint failed"));
        };
    }
    else if(IsUiLayerEditSmokeEnabled()){
        paint.paint = [scene = CreateUiEditSmokeScene(context.objectArena, context.input)](Impl::UiPaintContext& paintContext){
            if(!scene->paint(paintContext))
                NWB_LOGGER_ERROR(NWB_TEXT("UiLayerSmokeProject: edit UI paint failed"));
        };
    }
    else if(windowSmoke){
        paint.paint = [scene = CreateUiWindowSmokeScene(context.objectArena)](Impl::UiPaintContext& paintContext){
            if(!scene->paint(paintContext))
                NWB_LOGGER_ERROR(NWB_TEXT("UiLayerSmokeProject: window UI paint failed"));
        };
    }
    else if(IsUiLayerInteractionSmokeEnabled()){
        paint.paint = [scene = CreateUiInteractiveSmokeScene(context.objectArena)](Impl::UiPaintContext& paintContext){
            if(!scene->paint(paintContext))
                NWB_LOGGER_ERROR(NWB_TEXT("UiLayerSmokeProject: interactive UI paint failed"));
        };
    }
    else{
        paint.paint = [textSamples = CreateUiTextSmokeSamples(context.objectArena), paintFrame = 0u](Impl::UiPaintContext& paintContext)mutable{
            if(paintFrame < 3u){
                ++paintFrame;
                if(paintFrame <= 2u)
                    return;
                NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiLayerSmokeProject: deterministic solid, skin, alpha and nested clip geometry submitted"));
            }
            PaintUiLayerSmokeScene(paintContext);
            if(!textSamples->paint(paintContext))
                NWB_LOGGER_ERROR(NWB_TEXT("UiLayerSmokeProject: text or glyph coverage painting failed"));
        };
    }

    return MakeNotNullUnique(Move(world));
}

void ClearUiLayerSmokeWorld(ProjectRuntimeContext& context, Core::ECS::World& world){
    world.taskScope().wait();
    auto* const layer = world.getSystem<Impl::UiLayerSystem>();
    if(!layer)
        return;

    context.graphics.removeRenderPass(*layer);
    const b8 deviceIdle = context.graphics.waitForIdle();
    NWB_FATAL_ASSERT_MSG(deviceIdle || context.graphics.isDeviceLost(), NWB_TEXT("UI smoke teardown requires device idle or terminal loss"));
    world.clear();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB::ProjectFrameClientSize NWB::QueryProjectFrameClientSize(){
    return { 960u, 540u };
}

TStringView NWB::QueryProjectWindowTitle()noexcept{
    return NWB_TEXT("NWB UI Layer Smoke");
}

b8 NWB::ConfigureProjectRuntime(ProjectStartupContext& context)noexcept{
    return context.graphics.setBindlessHeapAbi(Impl::AssetsGraphicsBindless::MakeGpuDescriptorHeapAbi()) && context.graphics.setHDR10OutputEnabled(false) && context.graphics.setSwapChainReadbackEnabled(true);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UniquePtr<NWB::IProjectEntryCallbacks> NWB::CreateProjectEntryCallbacks(ProjectRuntimeContext& context){
    auto world = Tests::Smoke::CreateUiLayerSmokeWorld(context);
    if(!world)
        return nullptr;
    return MakeUnique<Tests::Smoke::UiLayerSmokeProject>(Move(*world), context);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
