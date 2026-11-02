// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "namespace.h"
#include "CoolStuff/Testbed/ui_gallery_constants.adhd.h"
#include "ui_immutable_list_source.h"

#include <impl/ecs_ui/paint_context.h>

#include <core/alloc/scratch.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TESTBED_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiSearchComboSource final : public UiFilteredListSource<UiSearchComboSource, s_SearchComboLabelCount, s_SearchComboDisabledKey>{
    friend class UiImmutableListSource<UiSearchComboSource, s_SearchComboLabelCount, s_SearchComboDisabledKey, NWB::Impl::Ui::ISearchableListDataSource>;
    friend class UiFilteredListSource<UiSearchComboSource, s_SearchComboLabelCount, s_SearchComboDisabledKey>;


public:
    using UiFilteredListSource<UiSearchComboSource, s_SearchComboLabelCount, s_SearchComboDisabledKey>::UiFilteredListSource;


private:
    static constexpr Array<StringView, s_SearchComboLabelCount> s_Labels{ "Apple", "Banana", "Blueberry", "Cherry", "Fig (disabled)", "Grape", "Kiwi", "Lemon", "Mango", "Orange", "Pear", "Plum" };
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TESTBED_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

