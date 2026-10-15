// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "slider_fixture.h"

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UiSliderTests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SliderCallbackSource::SliderCallbackSource(){
    count = 1u;
    generation = 2501u;
}

StringView SliderCallbackSource::text(const u64 index)const{
    static_cast<void>(index);
    ++m_calls;
    if(m_mutation == SliderCallbackMutation::None)
        return "Later callback";
    const SliderCallbackMutation::Enum mutation = m_mutation;
    m_mutation = SliderCallbackMutation::None;
    ++m_mutations;
    switch(mutation){
    case SliderCallbackMutation::SameValue:
        EXPECT_TRUE(m_state->setValue(m_state->value()));
        break;
    case SliderCallbackMutation::AwayAndBack:{
        const f64 value = m_state->value();
        EXPECT_TRUE(m_state->setValue(0.9));
        EXPECT_TRUE(m_state->setValue(value));
        break;
    }
    case SliderCallbackMutation::SetValue:
        EXPECT_TRUE(m_state->setValue(0.9));
        break;
    case SliderCallbackMutation::ResetBuilder:
        m_builder->reset();
        break;
    case SliderCallbackMutation::PanelBuilder:
        m_beginAccepted = m_builder->beginPanel("reentered", { 0.0f, 0.0f, 100.0f, 80.0f });
        m_endAccepted = m_builder->endPanel();
        break;
    case SliderCallbackMutation::ClosePopup:
        m_popup->close();
        break;
    default:
        break;
    }
    return "Later callback";
}

void SliderCallbackSource::arm(const SliderCallbackMutation::Enum mutation, SliderState* state, Builder* builder, PopupState* popup){
    m_mutation = mutation;
    m_state = state;
    m_builder = builder;
    m_popup = popup;
    clearCounters();
}

void SliderCallbackSource::clearCounters()const{
    m_calls = 0u;
    m_mutations = 0u;
    m_beginAccepted = false;
    m_endAccepted = false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SliderOptions SliderFixture::Options(){
    SliderOptions settings;
    settings.width = { LayoutSizePolicy::Fixed, 280.0f };
    settings.keyStep = 0.125;
    return settings;
}

ListOptions SliderFixture::SiblingOptions(){
    ListOptions settings;
    settings.width = { LayoutSizePolicy::Fixed, 280.0f };
    settings.height = { LayoutSizePolicy::Fixed, 48.0f };
    return settings;
}

void SliderFixture::SetUp(){
    WidgetFixture::SetUp();
    configureSliderSkin();
    ASSERT_TRUE(m_skin.validatePayload());
    m_builder.setSkin(m_skin);
    ASSERT_TRUE(m_state.setValue(0.25));
}

void SliderFixture::configureSliderSkin(){
    configureSkin();
    UiSkin::RegionVector regions(m_arena);
    regions.assign(m_skin.regions().begin(), m_skin.regions().end());
    constexpr StringView s_Names[]{ "slider.track", "slider.thumb.normal", "slider.thumb.hover" };
    for(u32 index = 0u; index < 3u; ++index){
        UiSkinRegion region;
        region.name = Name(s_Names[index]);
        region.rectangle = { 96u + index * 32u, 0u, 24u, 24u };
        region.drawMode = UiSkinDrawMode::NineSlice;
        region.sliceInsets = { 6u, 6u, 6u, 6u };
        region.minimumWidth = 12.0f;
        region.minimumHeight = 12.0f;
        regions.push_back(Move(region));
    }
    m_skin.setAtlas(Core::Assets::AssetRef<Texture>("tests/ui/texture"), 192u, 32u, 1.0f, Move(regions));
}

bool SliderFixture::declare(const u64 generation, const SliderOptions& settings, const Rect& bounds){
    return begin(generation) && m_builder.beginPanel("panel", bounds) && m_builder.slider("slider", m_state, settings);
}

bool SliderFixture::prepare(const u64 generation, const SliderOptions& settings, const Rect& bounds){
    return declare(generation, settings, bounds) && finishPanel();
}

bool SliderFixture::accept(const u64 generation, const SliderOptions& settings, const Rect& bounds){
    return prepare(generation, settings, bounds) && m_context.commitFrame(generation);
}

bool SliderFixture::sibling(const AStringView key){
    return m_builder.virtualList(key, m_laterSource, m_laterState, SiblingOptions()).valid;
}

WidgetId SliderFixture::host()const{
    return id("slider", "panel");
}

WidgetId SliderFixture::track()const{
    return MakeWidgetId(host(), "track");
}

WidgetId SliderFixture::thumb()const{
    return MakeWidgetId(host(), "thumb");
}

Point SliderFixture::trackPoint(const f64 normalized, const SliderState& state)const{
    const SliderPlacement& placement = state.placement();
    return { placement.centerTravel.x + static_cast<f32>(normalized) * placement.centerTravel.width, placement.track.y + placement.track.height * 0.5f };
}

Point SliderFixture::thumbPoint()const{
    return UiWidgetTests::Center(m_state.placement().thumb);
}

SliderAcceptedFrame SliderFixture::accepted()const{
    SliderAcceptedFrame saved;
    saved.count = m_context.input().targets().size();
    saved.popupCount = m_context.input().popupCount();
    saved.generation = m_context.input().layoutGeneration();
    saved.focus = m_context.input().focus();
    saved.capture = m_context.input().capture();
    for(usize index = 0u; index < Min(saved.count, saved.targets.size()); ++index)
        saved.targets[index] = m_context.input().targets()[index];
    return saved;
}

void SliderFixture::expectAccepted(const SliderAcceptedFrame& saved, const bool popupFocus)const{
    EXPECT_EQ(m_context.input().layoutGeneration(), saved.generation);
    if(popupFocus){
        EXPECT_EQ(m_context.input().popupCount(), saved.popupCount);
        EXPECT_EQ(m_context.input().focus(), saved.focus);
    }
    EXPECT_EQ(m_context.input().capture(), saved.capture);
    ExpectAcceptedTargets(m_context.input(), saved.targets, saved.count);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

