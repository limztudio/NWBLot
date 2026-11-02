// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "namespace.h"
#include "CoolStuff/Testbed/ui_gallery_constants.adhd.h"

#include <impl/ecs_ui/paint_context.h>

#include <core/alloc/scratch.h>
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TESTBED_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

