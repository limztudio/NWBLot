// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "namespace.h"
#include "ui_gallery_constants.h"
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
    static constexpr Array<StringView, s_RadioGroupLabelCount> s_Labels{ "Balanced", "High quality", "Unavailable" };
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

