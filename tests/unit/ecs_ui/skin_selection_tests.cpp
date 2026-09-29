// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/skin_selection.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_skin_selection_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(UiSkinSelectionTests, OmittedSkinSelectsEngineDefaultOnce){
    const Core::Assets::AssetRef<UiSkin> omitted;
    UiSkinSelection selection(omitted);
    u32 calls = 0u;
    const auto result = selection.ensure([&](const Core::Assets::AssetRef<UiSkin>& ref){
        ++calls;
        return ref == s_DefaultUiSkinRef;
    });
    EXPECT_EQ(result, UiSkinSelectionResult::Selected);
    EXPECT_EQ(calls, 1u);
    EXPECT_EQ(selection.selected(), s_DefaultUiSkinRef);
}

TEST(UiSkinSelectionTests, FailedCustomFallsBackToDefaultAndRevalidatesOnlyDefault){
    const Core::Assets::AssetRef<UiSkin> custom{ "project/ui/skins/missing/atlas" };
    UiSkinSelection selection(custom);
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
    EXPECT_EQ(selection.ensure(bind), UiSkinSelectionResult::DefaultFallback);
    EXPECT_EQ(selection.selected(), s_DefaultUiSkinRef);
    EXPECT_EQ(selection.ensure(bind), UiSkinSelectionResult::Selected);
    EXPECT_EQ(customCalls, 1u);
    EXPECT_EQ(defaultCalls, 2u);
}

TEST(UiSkinSelectionTests, FailedDefaultLeavesSelectionUncommittedForRetry){
    const Core::Assets::AssetRef<UiSkin> custom{ "project/ui/skins/missing/atlas" };
    UiSkinSelection selection(custom);
    u32 calls = 0u;
    const auto reject = [&](const Core::Assets::AssetRef<UiSkin>&){ ++calls; return false; };
    EXPECT_EQ(selection.ensure(reject), UiSkinSelectionResult::Failed);
    EXPECT_FALSE(selection.selected().valid());
    EXPECT_EQ(calls, 2u);
    const auto acceptCustom = [&](const Core::Assets::AssetRef<UiSkin>& ref){ return ref == custom; };
    EXPECT_EQ(selection.ensure(acceptCustom), UiSkinSelectionResult::Selected);
    EXPECT_EQ(selection.selected(), custom);
}

TEST(UiSkinSelectionTests, FailedRevalidationPreservesPreviouslySelectedCustom){
    const Core::Assets::AssetRef<UiSkin> custom{ "project/ui/skins/custom/atlas" };
    UiSkinSelection selection(custom);
    const auto acceptCustom = [&](const Core::Assets::AssetRef<UiSkin>& ref){ return ref == custom; };
    EXPECT_EQ(selection.ensure(acceptCustom), UiSkinSelectionResult::Selected);
    u32 calls = 0u;
    EXPECT_EQ(selection.ensure([&](const Core::Assets::AssetRef<UiSkin>& ref){
        ++calls;
        EXPECT_EQ(ref, custom);
        return false;
    }), UiSkinSelectionResult::Failed);
    EXPECT_EQ(calls, 1u);
    EXPECT_EQ(selection.selected(), custom);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

