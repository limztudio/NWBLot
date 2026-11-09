// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "namespace.h"
#include "CoolStuff/Testbed/ui_gallery_constants.adhd.h"

#include <impl/assets_scene/asset.h>
#include <impl/ecs_ui/paint_context.h>
#include <impl/ecs_ui/toolkit/edit/float_model.h>
#include <impl/ecs_ui/toolkit/edit/integer_model.h>
#include <impl/ecs_ui/toolkit/widgets/combo.h>
#include <impl/ecs_ui/toolkit/widgets/context_menu.h>
#include <impl/ecs_ui/toolkit/widgets/edit_box_state.h>
#include <impl/ecs_ui/toolkit/widgets/label.h>
#include <impl/ecs_ui/toolkit/widgets/list.h>
#include <impl/ecs_ui/toolkit/widgets/popup.h>
#include <impl/ecs_ui/toolkit/widgets/radio_group.h>
#include <impl/ecs_ui/toolkit/widgets/search_combo.h>
#include <impl/ecs_ui/toolkit/widgets/text_area.h>
#include <impl/ecs_ui/toolkit/widgets/tooltip.h>

#include <core/alloc/scratch.h>
#include <core/assets/manager.h>

#include <global/range_algorithms.h>
#include <global/scope_exit.h>
#include <global/text_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TESTBED_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename CallbackF>
decltype(auto) WithGalleryFontSize(NWB::Impl::Ui::Builder& ui, const f32 fontSize, CallbackF&& callback){
    const f32 previousFontSize = ui.style().fontSize;
    ui.style().fontSize = fontSize;
    ScopeExit restoreFontSize([&ui, previousFontSize]()noexcept{ ui.style().fontSize = previousFontSize; });
    return Forward<CallbackF>(callback)();
}

template<typename CallbackF>
decltype(auto) WithGalleryFontSize(NWB::Impl::Ui::Builder& ui, CallbackF&& callback){
    return WithGalleryFontSize(ui, s_GalleryFontSize, Forward<CallbackF>(callback));
}

[[nodiscard]] inline b8 DeclareSelectedKeyLabel(NWB::Impl::Ui::Builder& ui, NWB::Core::Alloc::ScratchArena& scratchArena, const u64 selectedKey){
    const auto text = StringFormat(scratchArena, s_SelectedTextFormat, selectedKey);
    return ui.label(s_SelectedKey, { text.data(), text.size() });
}

template<typename Source>
[[nodiscard]] inline Expected<u64> FindEnabledListSourceRow(const Source& source, const u64 start, const b8 reverse)noexcept{
    return FindDirectionalIndex(start, reverse, [&source]()noexcept{ return source.rowCount(); }, [&source](const u64 candidate)noexcept{ return source.enabled(candidate); });
}

// Immutable demo rows share key navigation without adding per-source row or label storage.
template<typename Source, usize LabelCount, u64 DisabledKey = 0u, typename Interface = NWB::Impl::Ui::IListDataSource>
class UiImmutableListSource : public Interface, NoCopy{
public:
    virtual u64 instanceGeneration()const noexcept override final{ return 1u; }

    virtual u64 revision()const noexcept override final{ return 1u; }

    virtual u64 rowCount()const noexcept override final{ return LabelCount; }

    virtual u64 key(const u64 index)const noexcept override final{ return ResolveImmutableListSourceKey(index, LabelCount); }

    virtual Expected<u64> indexOf(const u64 keyValue)const noexcept override final{
        return TryResolveImmutableListSourceIndex(keyValue, LabelCount);
    }

    virtual Expected<u64> findEnabled(const u64 start, const b8 reverse)const noexcept override final{
        return FindEnabledListSourceRow(*this, start, reverse);
    }

    virtual StringView text(const u64 index)const noexcept override{
        if constexpr(requires{ Source::ResolveLabel(index); })
            return Source::ResolveLabel(index);
        else if constexpr(requires{ Source::s_Labels; })
            return index < LabelCount ? Source::s_Labels[index] : StringView{};
        else
            return {};
    }

    virtual b8 enabled(const u64 index)const noexcept override final{ return IsImmutableListSourceRowEnabled(index, LabelCount, DisabledKey); }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename Source, usize LabelCount, u64 DisabledKey>
class UiFilteredListSource : public UiImmutableListSource<Source, LabelCount, DisabledKey, NWB::Impl::Ui::ISearchableListDataSource>{
private:
    class View final : public NWB::Impl::Ui::IListDataSource, NoCopy{
    public:
        explicit View(const UiFilteredListSource& source)noexcept
            : m_source(source)
        {}


    public:
        virtual u64 instanceGeneration()const noexcept override{ return s_FilteredInstanceGeneration; }

        virtual u64 revision()const noexcept override{ return m_source.m_viewRevision; }

        virtual u64 rowCount()const noexcept override{ return m_source.m_count; }

        virtual u64 key(const u64 index)const noexcept override{ return index < rowCount() ? m_source.key(m_source.m_indices[index]) : 0u; }

        virtual Expected<u64> indexOf(const u64 keyValue)const noexcept override{
            return TryFindFilteredListSourceIndex(m_source.m_indices, rowCount(), LabelCount, keyValue);
        }

        virtual Expected<u64> findEnabled(const u64 start, const b8 reverse)const noexcept override{
            return FindEnabledListSourceRow(*this, start, reverse);
        }

        virtual StringView text(const u64 index)const noexcept override{
            return index < rowCount() ? m_source.text(m_source.m_indices[index]) : StringView{};
        }

        virtual b8 enabled(const u64 index)const noexcept override{ return index < rowCount() && m_source.enabled(m_source.m_indices[index]); }


    private:
        const UiFilteredListSource& m_source;
    };


public:
    explicit UiFilteredListSource(NWB::Core::Alloc::GlobalArena& arena)noexcept
        : m_query(arena)
        , m_view(*this)
        , m_count(LabelCount)
        , m_viewRevision(1u)
    {
        for(u64 index = 0u; index < LabelCount; ++index)
            m_indices[index] = index;
    }


public:
    [[nodiscard]] virtual b8 filter(const AStringView query)override final{
        if(m_viewRevision != 0u && query == AStringView(m_query.data(), m_query.size()))
            return true;
        if(query.empty())
            m_query.clear();
        else
            m_query.assign(query.data(), query.size());
        m_count = 0u;
        for(u64 index = 0u; index < LabelCount; ++index){
            if(!ContainsAsciiIgnoreCase<char>(this->text(index), query))
                continue;
            m_indices[m_count] = index;
            ++m_count;
        }
        ++m_viewRevision;
        return true;
    }

    [[nodiscard]] virtual const NWB::Impl::Ui::IListDataSource& filtered()const noexcept override final{ return m_view; }


private:
    AString<NWB::Core::Alloc::GlobalArena> m_query;
    View m_view;
    Array<u64, LabelCount> m_indices{};
    u64 m_count = 0u;
    u64 m_viewRevision = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiListSource final : public UiImmutableListSource<UiListSource, s_VirtualListRowCount>{
public:
    virtual StringView text(u64 index)const noexcept override;


private:
    mutable Array<char, s_VirtualListLabelCapacity> m_label{};
};

class UiListGallery final : NoCopy{
public:
    void paint(NWB::Impl::UiPaintContext& context, NWB::Core::Alloc::ScratchArena& scratchArena, f32 x, f32 y);


private:
    UiListSource m_source;
    NWB::Impl::Ui::ListState m_state;
};

class UiComboGallery final : NoCopy{
public:
    void paint(NWB::Impl::UiPaintContext& context, NWB::Core::Alloc::ScratchArena& scratchArena, f32 x, f32 y);


private:
    UiListSource m_source;
    NWB::Impl::Ui::ComboState m_state;
};

class UiEditGallery final : NoCopy{
public:
    explicit UiEditGallery(NWB::Core::Alloc::GlobalArena& arena);


public:
    void paint(NWB::Impl::UiPaintContext& context, f32 x, f32 y);


private:
    NWB::Impl::Ui::EditModel m_text;
    NWB::Impl::Ui::EditModel m_readOnly;
    NWB::Impl::Ui::EditBoxState m_textState;
    NWB::Impl::Ui::EditBoxState m_readOnlyState;
};

class UiImageGallery final : NoCopy{
public:
    UiImageGallery()noexcept = default;


public:
    [[nodiscard]] b8 loadAssets(NWB::Core::Alloc::GlobalArena& arena, const NWB::Core::Assets::AssetManager& assets, const NWB::Impl::ActiveSceneSubsystems& sceneSubsystems);
    void paint(NWB::Impl::UiPaintContext& context, f32 x, f32 y);


private:
    NWB::Impl::Ui::SharedImageSource m_source;
};

class UiNestedPopupSource final : public UiFilteredListSource<UiNestedPopupSource, s_NestedPopupLabelCount, s_NestedPopupDisabledKey>{
    friend class UiImmutableListSource<UiNestedPopupSource, s_NestedPopupLabelCount, s_NestedPopupDisabledKey, NWB::Impl::Ui::ISearchableListDataSource>;
    friend class UiFilteredListSource<UiNestedPopupSource, s_NestedPopupLabelCount, s_NestedPopupDisabledKey>;


public:
    using UiFilteredListSource<UiNestedPopupSource, s_NestedPopupLabelCount, s_NestedPopupDisabledKey>::UiFilteredListSource;


private:
    [[nodiscard]] static StringView ResolveLabel(const u64 index)noexcept{ return ResolveNestedPopupLabel(index); }
};

class UiNestedPopupGallery final : NoCopy{
public:
    explicit UiNestedPopupGallery(NWB::Core::Alloc::GlobalArena& arena);


public:
    void paint(NWB::Impl::UiPaintContext& context, f32 x, f32 y);


private:
    [[nodiscard]] b8 paintParent(NWB::Impl::Ui::Builder& ui);
    [[nodiscard]] b8 paintChild(NWB::Impl::Ui::Builder& ui);


private:
    UiNestedPopupSource m_source;
    NWB::Impl::Ui::PopupState m_parent;
    NWB::Impl::Ui::PopupState m_child;
    NWB::Impl::Ui::ListState m_beforeList;
    NWB::Impl::Ui::ListState m_afterList;
    NWB::Impl::Ui::ComboState m_combo;
    NWB::Impl::Ui::SearchComboState m_search;
    NWB::Impl::Ui::ContextMenuState m_menu;
    NWB::Impl::Ui::EditModel m_beforeText;
    NWB::Impl::Ui::EditModel m_afterText;
    NWB::Impl::Ui::EditModel m_childText;
    NWB::Impl::Ui::EditBoxState m_beforeEdit;
    NWB::Impl::Ui::EditBoxState m_afterEdit;
    NWB::Impl::Ui::EditBoxState m_childEdit;
};

class UiNumericEditGallery final : NoCopy{
public:
    explicit UiNumericEditGallery(NWB::Core::Alloc::GlobalArena& arena);


public:
    void paint(NWB::Impl::UiPaintContext& context, NWB::Core::Alloc::ScratchArena& scratchArena, f32 x, f32 y);


private:
    NWB::Impl::Ui::IntegerEditModel m_integer;
    NWB::Impl::Ui::FloatEditModel m_float;
    NWB::Impl::Ui::EditBoxState m_integerState;
    NWB::Impl::Ui::EditBoxState m_floatState;
};

class UiPopupGallery final : NoCopy{
public:
    void trigger(NWB::Impl::Ui::Builder& ui);
    void paint(NWB::Impl::UiPaintContext& context, f32 x, f32 y);


private:
    NWB::Impl::Ui::PopupState m_popup;
    u32 m_choice = 0u;
    b8 m_keepOpen = false;
};

class UiPopupToolsSource final : public UiImmutableListSource<UiPopupToolsSource, s_PopupToolsLabelCount, s_PopupToolsDisabledKey>{
    friend class UiImmutableListSource<UiPopupToolsSource, s_PopupToolsLabelCount, s_PopupToolsDisabledKey>;


public:
    using UiImmutableListSource<UiPopupToolsSource, s_PopupToolsLabelCount, s_PopupToolsDisabledKey>::UiImmutableListSource;


private:
    [[nodiscard]] static StringView ResolveLabel(const u64 index)noexcept{ return ResolvePopupToolsLabel(index); }
};

class UiPopupToolsGallery final : NoCopy{
public:
    void paint(NWB::Impl::UiPaintContext& context, NWB::Core::Alloc::ScratchArena& scratchArena, f32 x, f32 y);


private:
    UiPopupToolsSource m_source;
    NWB::Impl::Ui::ContextMenuState m_menu;
    NWB::Impl::Ui::TooltipState m_tooltip;
    u64 m_command = 0u;
    u32 m_anchorClicks = 0u;
};

class UiProgressGallery final : NoCopy{
public:
    void paint(NWB::Impl::UiPaintContext& context, NWB::Core::Alloc::ScratchArena& scratchArena, f32 x, f32 y);


private:
    f64 m_fraction = s_DefaultProgressFraction;
};

class UiRadioGroupSource final : public UiImmutableListSource<UiRadioGroupSource, s_RadioGroupLabelCount, s_RadioGroupDisabledKey>{
    friend class UiImmutableListSource<UiRadioGroupSource, s_RadioGroupLabelCount, s_RadioGroupDisabledKey>;


public:
    using UiImmutableListSource<UiRadioGroupSource, s_RadioGroupLabelCount, s_RadioGroupDisabledKey>::UiImmutableListSource;


private:
    [[nodiscard]] static StringView ResolveLabel(const u64 index)noexcept{ return ResolveRadioGroupLabel(index); }
};

class UiRadioGroupGallery final : NoCopy{
public:
    void paint(NWB::Impl::UiPaintContext& context, f32 x, f32 y);


private:
    UiRadioGroupSource m_source;
    NWB::Impl::Ui::RadioGroupState m_state;
};

class UiSearchComboSource final : public UiFilteredListSource<UiSearchComboSource, s_SearchComboLabelCount, s_SearchComboDisabledKey>{
    friend class UiImmutableListSource<UiSearchComboSource, s_SearchComboLabelCount, s_SearchComboDisabledKey, NWB::Impl::Ui::ISearchableListDataSource>;
    friend class UiFilteredListSource<UiSearchComboSource, s_SearchComboLabelCount, s_SearchComboDisabledKey>;


public:
    using UiFilteredListSource<UiSearchComboSource, s_SearchComboLabelCount, s_SearchComboDisabledKey>::UiFilteredListSource;


private:
    [[nodiscard]] static StringView ResolveLabel(const u64 index)noexcept{ return ResolveSearchComboLabel(index); }
};

class UiSearchComboGallery final : NoCopy{
public:
    explicit UiSearchComboGallery(NWB::Core::Alloc::GlobalArena& arena)
        : m_source(arena)
        , m_state(arena)
    {}


public:
    void paint(NWB::Impl::UiPaintContext& context, NWB::Core::Alloc::ScratchArena& scratchArena, f32 x, f32 y);


private:
    UiSearchComboSource m_source;
    NWB::Impl::Ui::SearchComboState m_state;
};

class UiSliderGallery final : NoCopy{
public:
    void paint(NWB::Impl::UiPaintContext& context, f32 x, f32 y);


private:
    NWB::Impl::Ui::SliderState m_state;
    b8 m_enabled = true;
};

class UiTextAreaGallery final : NoCopy{
public:
    explicit UiTextAreaGallery(NWB::Core::Alloc::GlobalArena& arena);


public:
    void paint(NWB::Impl::UiPaintContext& context, f32 x, f32 y);


private:
    NWB::Impl::Ui::EditModel m_document;
    NWB::Impl::Ui::TextAreaState m_state;
    b8 m_readOnly = false;
};

class UiWidgetGallery final : NoCopy{
public:
    explicit UiWidgetGallery(NWB::Core::Alloc::GlobalArena& arena)
        : m_edits(arena)
        , m_searchCombos(arena)
        , m_nestedPopups(arena)
        , m_numericEdits(arena)
        , m_textAreas(arena)
    {}


public:
    [[nodiscard]] b8 loadAssets(NWB::Core::Alloc::GlobalArena& arena, const NWB::Core::Assets::AssetManager& assets, const NWB::Impl::ActiveSceneSubsystems& sceneSubsystems){
        return m_images.loadAssets(arena, assets, sceneSubsystems);
    }
    void paint(NWB::Impl::UiPaintContext& context, const NWB::Impl::Ui::Rect& bounds);


private:
    void paintControls(NWB::Impl::UiPaintContext& context, NWB::Core::Alloc::ScratchArena& scratchArena, f32 x, f32 y);


private:
    UiEditGallery m_edits;
    UiListGallery m_lists;
    UiComboGallery m_combos;
    UiSearchComboGallery m_searchCombos;
    UiPopupGallery m_popups;
    UiPopupToolsGallery m_popupTools;
    UiNestedPopupGallery m_nestedPopups;
    UiNumericEditGallery m_numericEdits;
    UiTextAreaGallery m_textAreas;
    UiRadioGroupGallery m_radioGroups;
    UiSliderGallery m_sliders;
    UiProgressGallery m_progress;
    UiImageGallery m_images;
    u32 m_count = 0u;
    u32 m_selectedGallery = 0u;
    b8 m_enabled = true;
};

class UiSkinPreview final : NoCopy{
public:
    explicit UiSkinPreview(NWB::Core::Alloc::GlobalArena& arena);


public:
    [[nodiscard]] b8 loadAssets(NWB::Core::Alloc::GlobalArena& arena, const NWB::Core::Assets::AssetManager& assets, const NWB::Impl::ActiveSceneSubsystems& sceneSubsystems){
        return m_widgets.loadAssets(arena, assets, sceneSubsystems);
    }
    [[nodiscard]] Expected<NWB::Impl::Ui::Rect> galleryBounds(NWB::Impl::UiPaintContext& context);
    void paint(NWB::Impl::UiPaintContext& context);


private:
    NWB::Impl::Ui::Label m_normal;
    NWB::Impl::Ui::Label m_hover;
    NWB::Impl::Ui::Label m_pressed;
    NWB::Impl::Ui::Label m_disabled;
    NWB::Impl::Ui::Label m_edit;
    NWB::Impl::Ui::Label m_caption;
    NWB::Impl::Ui::Label m_korean;
    UiWidgetGallery m_widgets;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TESTBED_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
