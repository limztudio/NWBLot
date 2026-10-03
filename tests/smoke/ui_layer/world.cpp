// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "world.h"
#include "combo_scene.h"
#include "edit_scene.h"
#include "interactive_scene.h"
#include "list_scene.h"
#include "nested_popup_scene.h"
#include "numeric_edit_scene.h"
#include "paint_scene.h"
#include "popup_scene.h"
#include "popup_tools_scene.h"
#include "radio_group_scene.h"
#include "slider_scene.h"
#include "progress_scene.h"
#include "image_scene.h"
#include "texture_image_scene.h"
#include "search_combo_scene.h"
#include "text_samples.h"
#include "text_area_scene.h"
#include "window_scene.h"
#include "../smoke_environment.h"

#include <impl/ecs_ui/layer_system.h>

#include <core/common/log.h>
#include <core/ecs/entity.h>
#include <core/ecs/world.h>
#include <core/graphics/runtime/runtime.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_layer_smoke_world{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr Core::Assets::AssetRef<Impl::UiSkin> s_DefaultSkin{"engine/ui/skins/default/atlas"};
static constexpr Core::Assets::AssetRef<Impl::UiSkin> s_AlternateSkin{"project/ui/skins/alternate/atlas"};
static constexpr Core::Assets::AssetRef<Impl::Font> s_DefaultLatin{"engine/ui/fonts/default/latin/face"};
static constexpr Core::Assets::AssetRef<Impl::Font> s_DefaultKorean{"engine/ui/fonts/default/korean/face"};
static constexpr Core::Assets::AssetRef<Impl::FontAtlas> s_DefaultLatinAtlas{"engine/ui/fonts/default/latin/atlas"};
static constexpr Core::Assets::AssetRef<Impl::FontAtlas> s_DefaultKoreanAtlas{"engine/ui/fonts/default/korean/atlas"};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool CreateUiLayerSmokeWorld(ProjectRuntimeContext& context, UniquePtr<Core::ECS::World>& outWorld){
    outWorld.reset();
    if(!context.shaderPathResolver){
        NWB_LOGGER_ERROR(NWB_TEXT("UiLayerSmokeProject: shader path resolver is unavailable"));
        return false;
    }
    auto world = MakeUnique<Core::ECS::World>(context.objectArena, context.cpuTasks);
    if(!world){
        NWB_LOGGER_ERROR(NWB_TEXT("UiLayerSmokeProject: ECS world allocation failed"));
        return false;
    }

    const Impl::UiLayerSystem::FontReferences fonts{
        { { __hidden_ui_layer_smoke_world::s_DefaultLatin, __hidden_ui_layer_smoke_world::s_DefaultLatinAtlas },
            { __hidden_ui_layer_smoke_world::s_DefaultKorean, __hidden_ui_layer_smoke_world::s_DefaultKoreanAtlas } }, context.objectArena
    };
    const bool windowSmoke = IsUiLayerWindowSmokeEnabled();
    const bool popupSmoke = IsUiLayerPopupSmokeEnabled();
    const bool popupToolsSmoke = IsUiLayerPopupToolsSmokeEnabled();
    const bool nestedPopupSmoke = IsUiLayerNestedPopupSmokeEnabled();
    const bool radioGroupSmoke = IsUiLayerRadioGroupSmokeEnabled();
    const bool sliderSmoke = IsUiLayerSliderSmokeEnabled();
    const bool progressSmoke = IsUiLayerProgressSmokeEnabled();
    const bool imageSmoke = IsUiLayerImageSmokeEnabled();
    const bool textureImageSmoke = IsUiLayerTextureImageSmokeEnabled();
    const bool textAreaSmoke = IsUiLayerTextAreaSmokeEnabled();
    const bool numericEditSmoke = IsUiLayerNumericEditSmokeEnabled();
    const bool listSmoke = IsUiLayerListSmokeEnabled();
    const bool comboSmoke = IsUiLayerComboSmokeEnabled();
    const bool searchComboSmoke = IsUiLayerSearchComboSmokeEnabled();
    const bool alternateSkin = (windowSmoke && IsUiLayerWindowSkinSmokeEnabled())
        || (popupSmoke && IsUiLayerPopupSkinSmokeEnabled()) || (listSmoke && IsUiLayerListSkinSmokeEnabled())
        || (comboSmoke && IsUiLayerComboSkinSmokeEnabled())
        || (searchComboSmoke && IsUiLayerSearchComboSkinSmokeEnabled())
        || (popupToolsSmoke && IsUiLayerPopupToolsSkinSmokeEnabled())
        || (nestedPopupSmoke && IsUiLayerNestedPopupSkinSmokeEnabled())
        || (numericEditSmoke && IsUiLayerNumericEditSkinSmokeEnabled())
        || (textAreaSmoke && IsUiLayerTextAreaSkinSmokeEnabled())
        || (radioGroupSmoke && IsUiLayerRadioGroupSkinSmokeEnabled())
        || (sliderSmoke && IsUiLayerSliderSkinSmokeEnabled())
        || (progressSmoke && IsUiLayerProgressSkinSmokeEnabled())
        || (imageSmoke && IsUiLayerImageSkinSmokeEnabled())
        || (textureImageSmoke && IsUiLayerTextureImageSkinSmokeEnabled());
    const auto& skin = alternateSkin ? __hidden_ui_layer_smoke_world::s_AlternateSkin : __hidden_ui_layer_smoke_world::s_DefaultSkin;
    if(windowSmoke)
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiWindowSmoke: skin={}"), alternateSkin ? NWB_TEXT("alternate") : NWB_TEXT("default"));
    if(popupSmoke)
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiPopupSmoke: skin={}"), alternateSkin ? NWB_TEXT("alternate") : NWB_TEXT("default"));
    if(listSmoke)
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiListSmoke: skin={}"), alternateSkin ? NWB_TEXT("alternate") : NWB_TEXT("default"));
    if(comboSmoke)
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiComboSmoke: skin={}"), alternateSkin ? NWB_TEXT("alternate") : NWB_TEXT("default"));
    if(searchComboSmoke)
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiSearchComboSmoke: skin={}"), alternateSkin ? NWB_TEXT("alternate") : NWB_TEXT("default"));
    if(popupToolsSmoke)
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiPopupToolsSmoke: skin={}"), alternateSkin ? NWB_TEXT("alternate") : NWB_TEXT("default"));
    if(nestedPopupSmoke)
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiNestedPopupSmoke: skin={}"), alternateSkin ? NWB_TEXT("alternate") : NWB_TEXT("default"));
    if(numericEditSmoke)
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiNumericEditSmoke: skin={}"), alternateSkin ? NWB_TEXT("alternate") : NWB_TEXT("default"));
    if(textAreaSmoke)
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiTextAreaSmoke: skin={}"), alternateSkin ? NWB_TEXT("alternate") : NWB_TEXT("default"));
    if(radioGroupSmoke)
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiRadioGroupSmoke: skin={}"), alternateSkin ? NWB_TEXT("alternate") : NWB_TEXT("default"));
    if(sliderSmoke)
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiSliderSmoke: skin={}"), alternateSkin ? NWB_TEXT("alternate") : NWB_TEXT("default"));
    if(progressSmoke)
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiProgressSmoke: skin={}"), alternateSkin ? NWB_TEXT("alternate") : NWB_TEXT("default"));
    if(imageSmoke)
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiImageSmoke: skin={}"), alternateSkin ? NWB_TEXT("alternate") : NWB_TEXT("default"));
    if(textureImageSmoke){
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiTextureImageSmoke: skin={}")
            , alternateSkin ? NWB_TEXT("alternate") : NWB_TEXT("default")
        );
    }
    auto& layer = world->addSystem<Impl::UiLayerSystem>(
        *world,
        context.graphics,
        context.input,
        context.clipboard,
        context.textInput,
        context.assetManager,
        context.shaderPathResolver,
        skin,
        fonts,
        Impl::UiLayerPresentation::Standalone
    );
    if(ReadSmokeEnvironmentFlag("NWB_UI_LAYER_INPUT_BINDINGS_SMOKE")){
        Impl::Ui::InputBindings defaults(context.objectArena);
        Impl::Ui::InputBindings::BindingVector bindings(context.objectArena);
        bindings.reserve(defaults.bindings().size() + 4u);
        for(const auto& binding : defaults.bindings()){
            if(binding.key != Core::Key::Tab)
                bindings.push_back(binding);
        }
        bindings.push_back({ .key = Core::Key::Q, .command = Impl::Ui::InputCommand::Activate });
        bindings.push_back({ .key = Core::Key::W, .command = Impl::Ui::InputCommand::Left });
        bindings.push_back({ .key = Core::Key::R, .command = Impl::Ui::InputCommand::FocusNext });
        bindings.push_back({ .key = Core::Key::T, .command = Impl::Ui::InputCommand::FocusPrevious });
        if(!layer.setInputBindings(bindings.data(), bindings.size())){
            NWB_LOGGER_ERROR(NWB_TEXT("UiLayerSmokeProject: configured input profile was rejected"));
            return false;
        }
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiLayerSmokeProject: custom input profile Q=Activate W=Left R=FocusNext T=FocusPrevious Tab=unbound"));
    }
    if(ReadSmokeEnvironmentFlag("NWB_UI_IR_REPLAY")){
        layer.setGpuCommandRecordingMode(Impl::Ui::GpuCommandRecordingMode::CommandIrReplay);
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiLayerSmokeProject: command IR replay enabled"));
    }
    auto entity = world->createEntity();
    auto& paint = entity.addComponent<Impl::UiPaintComponent>();
    if(textureImageSmoke){
        const auto scene = CreateUiTextureImageSmokeScene(
            context.objectArena, context.input, context.assetManager, context.graphics
        );
        paint.paint = [scene](Impl::UiPaintContext& paintContext){
            if(!scene->paint(paintContext))
                NWB_LOGGER_ERROR(NWB_TEXT("UiLayerSmokeProject: texture-image UI paint failed"));
        };
    }
    else if(imageSmoke){
        paint.paint = [scene = CreateUiImageSmokeScene(context.objectArena, context.input)](Impl::UiPaintContext& paintContext){
            if(!scene->paint(paintContext))
                NWB_LOGGER_ERROR(NWB_TEXT("UiLayerSmokeProject: image UI paint failed"));
        };
    }
    else if(progressSmoke){
        paint.paint = [scene = CreateUiProgressSmokeScene(context.objectArena, context.input)](Impl::UiPaintContext& paintContext){
            if(!scene->paint(paintContext))
                NWB_LOGGER_ERROR(NWB_TEXT("UiLayerSmokeProject: progress UI paint failed"));
        };
    }
    else if(sliderSmoke){
        paint.paint = [scene = CreateUiSliderSmokeScene(context.objectArena, context.input)](Impl::UiPaintContext& paintContext){
            if(!scene->paint(paintContext))
                NWB_LOGGER_ERROR(NWB_TEXT("UiLayerSmokeProject: slider UI paint failed"));
        };
    }
    else if(radioGroupSmoke){
        paint.paint = [scene = CreateUiRadioGroupSmokeScene(context.objectArena, context.input)](Impl::UiPaintContext& paintContext){
            if(!scene->paint(paintContext))
                NWB_LOGGER_ERROR(NWB_TEXT("UiLayerSmokeProject: radio group UI paint failed"));
        };
    }
    else if(textAreaSmoke){
        paint.paint = [scene = CreateUiTextAreaSmokeScene(context.objectArena, context.clipboard)](Impl::UiPaintContext& paintContext){
            if(!scene->paint(paintContext))
                NWB_LOGGER_ERROR(NWB_TEXT("UiLayerSmokeProject: text area UI paint failed"));
        };
    }
    else if(numericEditSmoke){
        paint.paint = [scene = CreateUiNumericEditSmokeScene(context.objectArena, context.input)](Impl::UiPaintContext& paintContext){
            if(!scene->paint(paintContext))
                NWB_LOGGER_ERROR(NWB_TEXT("UiLayerSmokeProject: numeric editor UI paint failed"));
        };
    }
    else if(nestedPopupSmoke){
        paint.paint = [scene = CreateUiNestedPopupSmokeScene(context.objectArena, context.input)](Impl::UiPaintContext& paintContext){
            if(!scene->paint(paintContext))
                NWB_LOGGER_ERROR(NWB_TEXT("UiLayerSmokeProject: nested popup UI paint failed"));
        };
    }
    else if(popupToolsSmoke){
        paint.paint = [scene = CreateUiPopupToolsSmokeScene(context.objectArena, context.input)](Impl::UiPaintContext& paintContext){
            if(!scene->paint(paintContext))
                NWB_LOGGER_ERROR(NWB_TEXT("UiLayerSmokeProject: tooltip/context menu UI paint failed"));
        };
    }
    else if(searchComboSmoke){
        paint.paint = [scene = CreateUiSearchComboSmokeScene(context.objectArena, context.input)](Impl::UiPaintContext& paintContext){
            if(!scene->paint(paintContext))
                NWB_LOGGER_ERROR(NWB_TEXT("UiLayerSmokeProject: searchable combo UI paint failed"));
        };
    }
    else if(comboSmoke){
        paint.paint = [scene = CreateUiComboSmokeScene(context.objectArena, context.input)](Impl::UiPaintContext& paintContext){
            if(!scene->paint(paintContext))
                NWB_LOGGER_ERROR(NWB_TEXT("UiLayerSmokeProject: combo UI paint failed"));
        };
    }
    else if(listSmoke){
        paint.paint = [scene = CreateUiListSmokeScene(context.objectArena, context.input)](Impl::UiPaintContext& paintContext){
            if(!scene->paint(paintContext))
                NWB_LOGGER_ERROR(NWB_TEXT("UiLayerSmokeProject: virtual list paint failed"));
        };
    }
    else if(popupSmoke){
        const auto scene = CreateUiPopupSmokeScene(context.objectArena, context.input);
        paint.paint = [scene](Impl::UiPaintContext& paintContext){
            if(!scene->paint(paintContext))
                NWB_LOGGER_ERROR(NWB_TEXT("UiLayerSmokeProject: popup UI paint failed"));
        };
        auto laterEntity = world->createEntity();
        auto& laterPaint = laterEntity.addComponent<Impl::UiPaintComponent>();
        laterPaint.order = 100;
        laterPaint.paint = [scene](Impl::UiPaintContext& paintContext){
            if(!scene->paintLaterRoot(paintContext))
                NWB_LOGGER_ERROR(NWB_TEXT("UiLayerSmokeProject: later popup root paint failed"));
        };
    }
    else if(IsUiLayerEditSmokeEnabled()){
        paint.paint = [scene = CreateUiEditSmokeScene(context.objectArena, context.input)](Impl::UiPaintContext& paintContext){
            if(!scene->paint(paintContext))
                NWB_LOGGER_ERROR(NWB_TEXT("UiLayerSmokeProject: edit UI paint failed"));
        };
    }
    else if(windowSmoke){
        paint.paint = [scene = CreateUiWindowSmokeScene(context.objectArena)](Impl::UiPaintContext& paintContext){
            if(!scene->paint(paintContext))
                NWB_LOGGER_ERROR(NWB_TEXT("UiLayerSmokeProject: window UI paint failed"));
        };
    }
    else if(IsUiLayerInteractionSmokeEnabled()){
        paint.paint = [scene = CreateUiInteractiveSmokeScene(context.objectArena)](Impl::UiPaintContext& paintContext){
            if(!scene->paint(paintContext))
                NWB_LOGGER_ERROR(NWB_TEXT("UiLayerSmokeProject: interactive UI paint failed"));
        };
    }
    else{
        paint.paint = [textSamples = CreateUiTextSmokeSamples(context.objectArena), paintFrame = 0u](Impl::UiPaintContext& paintContext)mutable{
            if(paintFrame < 3u){
                ++paintFrame;
                if(paintFrame <= 2u)
                    return;
                NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiLayerSmokeProject: deterministic solid, skin, alpha and nested clip geometry submitted"));
            }
            PaintUiLayerSmokeScene(paintContext);
            if(!textSamples->paint(paintContext))
                NWB_LOGGER_ERROR(NWB_TEXT("UiLayerSmokeProject: text or glyph coverage painting failed"));
        };
    }

    context.graphics.addRenderPassToBack(layer);
    outWorld = Move(world);
    return true;
}

void DestroyUiLayerSmokeWorld(ProjectRuntimeContext& context, UniquePtr<Core::ECS::World>& world){
    if(!world)
        return;

    world->taskScope().wait();
    auto* const layer = world->getSystem<Impl::UiLayerSystem>();
    NWB_FATAL_ASSERT(layer);
    context.graphics.removeRenderPass(*layer);
    const bool deviceIdle = context.graphics.waitForIdle();
    NWB_FATAL_ASSERT_MSG(deviceIdle || context.graphics.isDeviceLost(), NWB_TEXT("UI smoke teardown requires device idle or terminal loss"));
    world->clear();
    world.reset();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

