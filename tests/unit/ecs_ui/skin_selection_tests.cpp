// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/skin_selection.adhd.h>
#include <impl/assets_ui_skin/asset.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_skin_selection_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr AStringView s_DefaultSkinPath = "engine/ui/skins/default/atlas";
constexpr AStringView s_MissingSkinPath = "project/ui/skins/missing/atlas";
constexpr AStringView s_CustomSkinPath = "project/ui/skins/custom/atlas";
using namespace NWB;
using namespace NWB::Impl;
inline constexpr Core::Assets::AssetRef<UiSkin> s_DefaultUiSkinRef{ s_DefaultSkinPath };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(UiSkinSelectionTests, FailedCustomFallsBackToDefaultAndRevalidatesOnlyDefault){
    const Core::Assets::AssetRef<UiSkin> custom{ __hidden_ui_skin_selection_tests::s_MissingSkinPath };
    UiSkinSelectionState selection = MakeUiSkinSelectionState(custom);
    u32 customCalls = 0u;
    u32 defaultCalls = 0u;
    const auto bind = [&](const Core::Assets::AssetRef<UiSkin>& ref){
        if(ref == custom){
            ++customCalls;
            return false;
        }
        EXPECT_EQ(ref, s_DefaultUiSkinRef);
        ++defaultCalls;
        return true;
    };
    EXPECT_EQ(EnsureUiSkinSelection(selection, s_DefaultUiSkinRef, bind), UiSkinSelectionResult::DefaultFallback);
    EXPECT_EQ(selection.selected, s_DefaultUiSkinRef);
    EXPECT_EQ(EnsureUiSkinSelection(selection, s_DefaultUiSkinRef, bind), UiSkinSelectionResult::Selected);
    EXPECT_EQ(customCalls, 1u);
    EXPECT_EQ(defaultCalls, 2u);
}

TEST(UiSkinSelectionTests, FailedDefaultLeavesSelectionUncommittedForRetry){
    const Core::Assets::AssetRef<UiSkin> custom{ __hidden_ui_skin_selection_tests::s_MissingSkinPath };
    UiSkinSelectionState selection = MakeUiSkinSelectionState(custom);
    u32 calls = 0u;
    const auto reject = [&](const Core::Assets::AssetRef<UiSkin>&)noexcept{ ++calls; return false; };
    EXPECT_EQ(EnsureUiSkinSelection(selection, s_DefaultUiSkinRef, reject), UiSkinSelectionResult::Failed);
    EXPECT_FALSE(selection.selected.valid());
    EXPECT_EQ(calls, 2u);
    const auto acceptCustom = [&](const Core::Assets::AssetRef<UiSkin>& ref)noexcept{ return ref == custom; };
    EXPECT_EQ(EnsureUiSkinSelection(selection, s_DefaultUiSkinRef, acceptCustom), UiSkinSelectionResult::Selected);
    EXPECT_EQ(selection.selected, custom);
}

TEST(UiSkinSelectionTests, FailedRevalidationPreservesPreviouslySelectedCustom){
    const Core::Assets::AssetRef<UiSkin> custom{ __hidden_ui_skin_selection_tests::s_CustomSkinPath };
    UiSkinSelectionState selection = MakeUiSkinSelectionState(custom);
    const auto acceptCustom = [&](const Core::Assets::AssetRef<UiSkin>& ref)noexcept{ return ref == custom; };
    EXPECT_EQ(EnsureUiSkinSelection(selection, s_DefaultUiSkinRef, acceptCustom), UiSkinSelectionResult::Selected);
    u32 calls = 0u;
    EXPECT_EQ(
        EnsureUiSkinSelection(selection, s_DefaultUiSkinRef, [&](const Core::Assets::AssetRef<UiSkin>& ref){
            ++calls;
            EXPECT_EQ(ref, custom);
            return false;
        }),
        UiSkinSelectionResult::Failed
    );
    EXPECT_EQ(calls, 1u);
    EXPECT_EQ(selection.selected, custom);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

