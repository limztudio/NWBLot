// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/skin_selection.h>
#include <impl/assets_ui_skin/asset.h>
#include <impl/assets_ui_skin/toolkit_contract.h>

#include <tests/common/capturing_logger.h>
#include <tests/common/test_context.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_skin_selection_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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

TEST(UiSkinSelectionTests, ChangeWaitsForResourcesGpuFrameAndAcceptedLayout){
    UiSkinSelection selection(s_DefaultUiSkinRef);
    const Core::Assets::AssetRef<UiSkin> alternate{ "project/ui/skins/alternate/atlas" };
    selection.requestChange(alternate);
    u32 binds = 0u;
    const auto bind = [&](const Core::Assets::AssetRef<UiSkin>&, const u64){ ++binds; return true; };
    EXPECT_EQ(selection.applyChangeIfReady(false, false, false, bind), UiSkinChangeResult::Deferred);
    EXPECT_TRUE(selection.changePending());
    EXPECT_EQ(selection.ensure([&](const Core::Assets::AssetRef<UiSkin>&){ return true; }), UiSkinSelectionResult::Selected);
    EXPECT_EQ(selection.applyChangeIfReady(true, true, false, bind), UiSkinChangeResult::Deferred);
    EXPECT_EQ(selection.applyChangeIfReady(true, false, true, bind), UiSkinChangeResult::Deferred);
    EXPECT_EQ(binds, 0u);
    EXPECT_EQ(selection.selected(), s_DefaultUiSkinRef);
    EXPECT_EQ(selection.generation(), 1u);
    const auto bindAlternate = [&](const Core::Assets::AssetRef<UiSkin>& ref, const u64 generation){
        ++binds;
        EXPECT_EQ(ref, alternate);
        EXPECT_EQ(generation, 2u);
        EXPECT_EQ(selection.selected(), s_DefaultUiSkinRef);
        EXPECT_EQ(selection.generation(), 1u);
        return true;
    };
    EXPECT_EQ(selection.applyChangeIfReady(true, false, false, bindAlternate), UiSkinChangeResult::Applied);
    EXPECT_EQ(binds, 1u);
    EXPECT_EQ(selection.selected(), alternate);
    EXPECT_EQ(selection.generation(), 2u);
}

TEST(UiSkinSelectionTests, SameReferenceReloadAdvancesGenerationAndLatestRequestWins){
    UiSkinSelection selection(s_DefaultUiSkinRef);
    const Core::Assets::AssetRef<UiSkin> obsolete{ "project/ui/skins/obsolete/atlas" };
    EXPECT_EQ(selection.ensure([&](const Core::Assets::AssetRef<UiSkin>&){ return true; }), UiSkinSelectionResult::Selected);
    selection.requestChange(obsolete);
    selection.requestChange(s_DefaultUiSkinRef);
    EXPECT_EQ(selection.applyChangeIfReady(true, false, false,
        [&](const Core::Assets::AssetRef<UiSkin>& ref, const u64 generation){
            EXPECT_EQ(ref, s_DefaultUiSkinRef);
            EXPECT_EQ(generation, 2u);
            return true;
        }
    ), UiSkinChangeResult::Applied);
    EXPECT_EQ(selection.selected(), s_DefaultUiSkinRef);
    EXPECT_EQ(selection.generation(), 2u);
    EXPECT_FALSE(selection.changePending());
}

TEST(UiSkinSelectionTests, FailedCandidateRetainsSkinUntilExplicitOrResourceRetry){
    UiSkinSelection selection(s_DefaultUiSkinRef);
    const Core::Assets::AssetRef<UiSkin> alternate{ "project/ui/skins/alternate/atlas" };
    EXPECT_EQ(selection.ensure([&](const Core::Assets::AssetRef<UiSkin>&){ return true; }), UiSkinSelectionResult::Selected);
    selection.requestChange(alternate);
    u32 binds = 0u;
    const auto reject = [&](const Core::Assets::AssetRef<UiSkin>& ref, const u64 generation){
        ++binds;
        EXPECT_EQ(ref, alternate);
        EXPECT_EQ(generation, 2u);
        return false;
    };
    EXPECT_EQ(selection.applyChangeIfReady(true, false, false, reject), UiSkinChangeResult::Failed);
    EXPECT_EQ(selection.selected(), s_DefaultUiSkinRef);
    EXPECT_EQ(selection.generation(), 1u);
    EXPECT_TRUE(selection.changeFailed());
    EXPECT_FALSE(selection.changePending());
    EXPECT_EQ(selection.requestedChange(), alternate);
    EXPECT_EQ(selection.applyChangeIfReady(true, false, false, reject), UiSkinChangeResult::Deferred);
    EXPECT_EQ(binds, 1u);
    selection.resourcesValidated();
    EXPECT_TRUE(selection.changePending());
    EXPECT_EQ(selection.applyChangeIfReady(true, false, false,
        [&](const Core::Assets::AssetRef<UiSkin>& ref, const u64 generation){
            EXPECT_EQ(ref, alternate);
            EXPECT_EQ(generation, 2u);
            return true;
        }
    ), UiSkinChangeResult::Applied);
    EXPECT_EQ(selection.selected(), alternate);
    EXPECT_EQ(selection.generation(), 2u);
    EXPECT_FALSE(selection.changeFailed());
}

TEST(UiSkinSelectionTests, EmptyLiveRequestSelectsAndRebindsEngineDefault){
    const Core::Assets::AssetRef<UiSkin> custom{ "project/ui/skins/custom/atlas" };
    UiSkinSelection selection(custom);
    EXPECT_EQ(selection.ensure([&](const Core::Assets::AssetRef<UiSkin>&){ return true; }), UiSkinSelectionResult::Selected);
    selection.requestChange({});
    EXPECT_EQ(selection.applyChangeIfReady(true, false, false,
        [&](const Core::Assets::AssetRef<UiSkin>& ref, const u64 generation){
            EXPECT_EQ(ref, s_DefaultUiSkinRef);
            EXPECT_EQ(generation, 2u);
            return true;
        }
    ), UiSkinChangeResult::Applied);
    EXPECT_EQ(selection.selected(), s_DefaultUiSkinRef);
}

TEST(UiSkinSelectionTests, IncompleteCookedCandidateCannotReachGpuBindOrReplaceSelection){
    NWB::Tests::CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    struct SkinAdmissionArenaTag{};
    NWB::Tests::TestArena<SkinAdmissionArenaTag> testArena;
    const Core::Assets::AssetRef<UiSkin> custom{ "project/ui/skins/incomplete/atlas" };
    UiSkin incomplete(testArena.arena, custom.name());
    UiSkin::RegionVector regions(testArena.arena);
    UiSkinRegion panel;
    panel.name = Name("panel.normal");
    panel.rectangle = { 0u, 0u, 16u, 16u };
    regions.push_back(panel);
    incomplete.setAtlas(Core::Assets::AssetRef<Texture>{ "project/ui/texture" }, 32u, 32u, 1.0f, Move(regions));
    ASSERT_TRUE(incomplete.validatePayload());

    u32 gpuBinds = 0u;
    const auto tryBind = [&](const Core::Assets::AssetRef<UiSkin>& ref){
        if(ref == custom){
            if(!ValidateUiSkinToolkitContract(incomplete))
                return false;
            ++gpuBinds;
            return true;
        }
        EXPECT_EQ(ref, s_DefaultUiSkinRef);
        return true;
    };
    UiSkinSelection startup(custom);
    EXPECT_EQ(startup.ensure(tryBind), UiSkinSelectionResult::DefaultFallback);
    EXPECT_EQ(startup.selected(), s_DefaultUiSkinRef);
    EXPECT_EQ(gpuBinds, 0u);

    UiSkinSelection live(s_DefaultUiSkinRef);
    ASSERT_EQ(live.ensure(tryBind), UiSkinSelectionResult::Selected);
    live.requestChange(custom);
    EXPECT_EQ(live.applyChangeIfReady(true, false, false,
        [&](const Core::Assets::AssetRef<UiSkin>& ref, const u64){ return tryBind(ref); }
    ), UiSkinChangeResult::Failed);
    EXPECT_EQ(live.selected(), s_DefaultUiSkinRef);
    EXPECT_EQ(live.generation(), 1u);
    EXPECT_EQ(gpuBinds, 0u);
    EXPECT_TRUE(live.changeFailed());
    EXPECT_TRUE(logger.sawErrorContaining(GLOBAL_TEXT("missing required region 'window.normal'")));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

