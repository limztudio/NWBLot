// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "namespace.h"

#include <impl/ecs_ui/components.h>

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TESTBED_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiGalleryMetrics final{
public:
    [[nodiscard]] static Expected<f32> RowHeight(NWB::Impl::UiPaintContext& context, const StringView text, const f32 fontSize){
        auto layout = context.text.layout({ .text = text, .fontSize = fontSize });
        if(!layout)
            return MakeUnexpected(Failure{});
        const auto bounds = context.text.measure(*layout, context.display);
        if(!bounds)
            return MakeUnexpected(bounds.error());
        const NWB::Impl::Ui::Insets& padding = context.ui.listStyle().row.padding;
        return bounds->height + padding.top + padding.bottom;
    }

    [[nodiscard]] static Expected<f32> RowsHeight(NWB::Impl::UiPaintContext& context, const NWB::Impl::Ui::IListDataSource& source, const f32 fontSize){
        f32 height = 0.0f;
        for(u64 index = 0u; index < source.rowCount(); ++index){
            const auto measured = RowHeight(context, source.text(index), fontSize);
            if(!measured)
                return MakeUnexpected(measured.error());
            height = Max(height, *measured);
        }
        return height;
    }
};


TESTBED_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

