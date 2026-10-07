// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "namespace.h"
#include "ui_combo_gallery.h"
#include "ui_edit_gallery.h"
#include "ui_list_gallery.h"
#include "ui_nested_popup_gallery.h"
#include "ui_numeric_edit_gallery.h"
#include "ui_popup_gallery.h"
#include "ui_popup_tools_gallery.h"
#include "ui_radio_group_gallery.h"
#include "ui_search_combo_gallery.h"
#include "ui_slider_gallery.h"
#include "ui_progress_gallery.h"
#include "ui_image_gallery.h"
#include "ui_text_area_gallery.h"

#include <impl/ecs_ui/components.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TESTBED_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiWidgetGallery final : NoCopy{
public:
    [[nodiscard]] static NWB::Impl::Ui::Rect LayoutBounds(const NWB::Impl::Ui::DisplayMetrics& display);


public:
    UiWidgetGallery(NWB::Core::Alloc::GlobalArena& arena, const NWB::Core::Assets::AssetManager& assets);


public:
    void paint(NWB::Impl::UiPaintContext& context);


private:
    void paintControls(NWB::Impl::UiPaintContext& context, f32 x, f32 y);


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
    bool m_enabled = true;
};


TESTBED_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

