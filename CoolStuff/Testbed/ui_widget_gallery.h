// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


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


class TestbedUiWidgetGallery final : NoCopy{
public:
    [[nodiscard]] static NWB::Impl::Ui::Rect layoutBounds(const NWB::Impl::Ui::DisplayMetrics& display);


public:
    TestbedUiWidgetGallery(NWB::Core::Alloc::GlobalArena& arena, const NWB::Core::Assets::AssetManager& assets);


public:
    void paint(NWB::Impl::UiPaintContext& context);


private:
    void paintControls(NWB::Impl::UiPaintContext& context, f32 x, f32 y);


private:
    TestbedUiEditGallery m_edits;
    TestbedUiListGallery m_lists;
    TestbedUiComboGallery m_combos;
    TestbedUiSearchComboGallery m_searchCombos;
    TestbedUiPopupGallery m_popups;
    TestbedUiPopupToolsGallery m_popupTools;
    TestbedUiNestedPopupGallery m_nestedPopups;
    TestbedUiNumericEditGallery m_numericEdits;
    TestbedUiTextAreaGallery m_textAreas;
    TestbedUiRadioGroupGallery m_radioGroups;
    TestbedUiSliderGallery m_sliders;
    TestbedUiProgressGallery m_progress;
    TestbedUiImageGallery m_images;
    u32 m_count = 0u;
    u32 m_selectedGallery = 0u;
    bool m_enabled = true;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

