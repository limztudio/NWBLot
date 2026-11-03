// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "namespace.h"
#include "CoolStuff/Testbed/ui_gallery_constants.adhd.h"
#include "ui_immutable_list_source.h"

#include <impl/ecs_ui/paint_context.h>
#include <impl/ecs_ui/toolkit/widgets/context_menu.h>
#include <impl/ecs_ui/toolkit/widgets/tooltip.h>

#include <core/alloc/scratch.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TESTBED_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TESTBED_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

