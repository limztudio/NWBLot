// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

#include <impl/ecs_ui/toolkit/id.adhd.h>

#include <global/expected.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline Expected<WidgetId> MakeRootId(const WidgetRoot& root)noexcept{
    WidgetId outId;
    if(!TryMakeRootId(root, outId))
        return MakeUnexpected(Failure{});
    return outId;
}

[[nodiscard]] inline Expected<WidgetId> MakeWidgetId(const WidgetId parent, const AStringView stableKey)noexcept{
    WidgetId outId;
    if(!TryMakeWidgetId(parent, stableKey, outId))
        return MakeUnexpected(Failure{});
    return outId;
}

// Numeric parts occupy a separate identity domain from nonempty textual declarations.
[[nodiscard]] inline Expected<WidgetId> MakeWidgetPartId(const WidgetId parent, const u64 stableKey)noexcept{
    WidgetId outId;
    if(!TryMakeWidgetPartId(parent, stableKey, outId))
        return MakeUnexpected(Failure{});
    return outId;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
