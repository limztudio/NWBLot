// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "ui_widget_gallery.h"

#include <impl/ecs_ui/components.h>
#include <impl/ui/widgets/label.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class TestbedUiSkinPreview final : NoCopy{
public:
    explicit TestbedUiSkinPreview(NWB::Core::Alloc::GlobalArena& arena);


public:
    void paint(NWB::Impl::UiPaintContext& context);


private:
    NWB::Impl::Ui::Label m_normal;
    NWB::Impl::Ui::Label m_hover;
    NWB::Impl::Ui::Label m_pressed;
    NWB::Impl::Ui::Label m_disabled;
    NWB::Impl::Ui::Label m_edit;
    NWB::Impl::Ui::Label m_caption;
    NWB::Impl::Ui::Label m_korean;
    TestbedUiWidgetGallery m_widgets;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

