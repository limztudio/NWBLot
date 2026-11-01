// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "widget_id_assertion.h"

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UiTestSupport{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB::Impl::Ui::WidgetId RequireRootId(const NWB::Impl::Ui::WidgetRoot& root){
    NWB::Impl::Ui::WidgetId id{};
    const b8 valid = NWB::Impl::Ui::TryMakeRootId(root, id);
    EXPECT_TRUE(valid);
    return valid ? id : NWB::Impl::Ui::WidgetId{};
}

NWB::Impl::Ui::WidgetId RequireWidgetId(const NWB::Impl::Ui::WidgetId parent, const AStringView stableKey){
    NWB::Impl::Ui::WidgetId id{};
    const b8 valid = NWB::Impl::Ui::TryMakeWidgetId(parent, stableKey, id);
    EXPECT_TRUE(valid);
    return valid ? id : NWB::Impl::Ui::WidgetId{};
}

NWB::Impl::Ui::WidgetId RequireWidgetPartId(const NWB::Impl::Ui::WidgetId parent, const u64 partKey){
    NWB::Impl::Ui::WidgetId id{};
    const b8 valid = NWB::Impl::Ui::TryMakeWidgetPartId(parent, partKey, id);
    EXPECT_TRUE(valid);
    return valid ? id : NWB::Impl::Ui::WidgetId{};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

