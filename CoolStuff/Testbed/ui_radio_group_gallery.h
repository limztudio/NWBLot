// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "namespace.h"
#include "CoolStuff/Testbed/ui_gallery_constants.adhd.h"
#include "ui_immutable_list_source.h"

#include <impl/ecs_ui/paint_context.h>
#include <impl/ecs_ui/toolkit/widgets/radio_group.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TESTBED_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TESTBED_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

