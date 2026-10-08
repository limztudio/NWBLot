// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "radio_group_fixture.h"

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UiRadioGroupTests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


u64 RadioSource::instanceGeneration()const{
    callback(RadioCallbackSite::Instance);
    return m_generation;
}

u64 RadioSource::revision()const{
    callback(RadioCallbackSite::Revision);
    return m_contentRevision;
}

u64 RadioSource::rowCount()const{
    callback(RadioCallbackSite::Count);
    return rawCount();
}

u64 RadioSource::key(const u64 index)const{
    callback(RadioCallbackSite::Key);
    return rawKey(index);
}

Expected<u64> RadioSource::indexOf(const u64 value)const{
    u64 index = 0u;
    callback(RadioCallbackSite::None);
    const u64 count = rawCount();
    for(u64 cursor = 0u; cursor < count; ++cursor){
        if(value != 0u && rawKey(cursor) == value){
            index = cursor;
            return index;
        }
    }
    return MakeUnexpected(Failure{});
}

Expected<u64> RadioSource::findEnabled(const u64 start, const bool reverse)const{
    u64 index = 0u;
    callback(RadioCallbackSite::None);
    const u64 count = rawCount();
    if(start >= count || m_allDisabled)
        return MakeUnexpected(Failure{});
    for(u64 cursor = start; cursor < count;){
        if(rawKey(cursor) != m_disabled){
            index = cursor;
            return index;
        }
        if(reverse){
            if(cursor == 0u)
                break;
            --cursor;
        }
        else
            ++cursor;
    }
    return MakeUnexpected(Failure{});
}

StringView RadioSource::text(const u64 index)const{
    callback(RadioCallbackSite::Text);
    ++m_textCalls;
    m_label = { static_cast<char>('A' + index % 26u), 'b', 'c', 'd' };
    return { m_label.data(), m_label.size() };
}

bool RadioSource::enabled(const u64 index)const{
    callback(RadioCallbackSite::Enabled);
    return index < rawCount() && !m_allDisabled && rawKey(index) != m_disabled;
}

void RadioSource::resetCounters()const{
    m_calls = 0u;
    m_textCalls = 0u;
    m_mutations = 0u;
    m_callbacksAfterMutation = 0u;
    m_beginAccepted = false;
    m_endAccepted = false;
}

void RadioSource::armSelection(RadioGroupState& state, const RadioCallbackSite::Enum site,
    const u64 keyValue, const bool awayAndBack
){
    m_state = &state;
    m_selected = keyValue;
    m_site = site;
    m_mutation = awayAndBack ? RadioMutation::SelectAwayBack : RadioMutation::Select;
}

void RadioSource::armSource(const RadioCallbackSite::Enum site, const bool replacement){
    m_site = site;
    m_mutation = replacement ? RadioMutation::Replacement : RadioMutation::Revision;
}

void RadioSource::armReentry(Builder& builder, const RadioCallbackSite::Enum site, const bool panel){
    m_builder = &builder;
    m_site = site;
    m_mutation = panel ? RadioMutation::PanelBuilder : RadioMutation::ResetBuilder;
}

void RadioSource::armClose(PopupState& popup, const RadioCallbackSite::Enum site){
    m_popup = &popup;
    m_site = site;
    m_mutation = RadioMutation::ClosePopup;
}

u64 RadioSource::rawCount()const{
    return m_count - static_cast<u64>(m_removed != 0u);
}

u64 RadioSource::rawKey(const u64 index)const{
    const u64 count = rawCount();
    if(index >= count)
        return 0u;
    if(m_duplicate)
        return 10u;
    const u64 forward = m_reverse ? count - 1u - index : index;
    const u64 value = (forward + 1u) * 10u;
    return value + (m_removed != 0u && value >= m_removed ? 10u : 0u);
}

void RadioSource::callback(const RadioCallbackSite::Enum site)const{
    ++m_calls;
    m_label.fill('?');
    if(m_mutations != 0u)
        ++m_callbacksAfterMutation;
    if(m_site == RadioCallbackSite::None || m_site != site)
        return;
    m_site = RadioCallbackSite::None;
    ++m_mutations;
    switch(m_mutation){
    case RadioMutation::Select:
        m_state->select(m_selected);
        break;
    case RadioMutation::SelectAwayBack:
        m_state->select(m_selected + 10u);
        m_state->select(m_selected);
        break;
    case RadioMutation::Revision:
        ++m_contentRevision;
        break;
    case RadioMutation::Replacement:
        ++m_generation;
        break;
    case RadioMutation::ResetBuilder:
        m_builder->reset();
        break;
    case RadioMutation::PanelBuilder:
        m_beginAccepted = m_builder->beginPanel("reentered", { 0.0f, 0.0f, 100.0f, 80.0f });
        m_endAccepted = m_builder->endPanel();
        break;
    case RadioMutation::ClosePopup:
        m_popup->close();
        break;
    default:
        break;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Point RadioCenter(const Rect& rectangle){
    return { rectangle.x + rectangle.width * 0.5f, rectangle.y + rectangle.height * 0.5f };
}

void ExpectRadioRect(const Rect& actual, const Rect& expected){
    EXPECT_FLOAT_EQ(actual.x, expected.x);
    EXPECT_FLOAT_EQ(actual.y, expected.y);
    EXPECT_FLOAT_EQ(actual.width, expected.width);
    EXPECT_FLOAT_EQ(actual.height, expected.height);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RadioGroupFixture::SetUp(){
    WidgetFixture::SetUp();
    configureRadioSkin();
    ASSERT_TRUE(m_skin.validatePayload());
    m_builder.setSkin(m_skin);
    m_state.select(10u);
}

void RadioGroupFixture::configureRadioSkin(const bool fallbackOnly){
    configureSkin();
    UiSkin::RegionVector regions(m_arena);
    regions.assign(m_skin.regions().begin(), m_skin.regions().end());
    constexpr StringView s_Names[]{ "radio.normal", "radio.checked", "radio.mark",
        "checkbox.normal", "checkbox.checked", "checkbox.mark" };
    for(u32 index = 0u; index < 6u; ++index){
        if(fallbackOnly && index < 3u)
            continue;
        UiSkinRegion region;
        region.name = Name(s_Names[index]);
        region.rectangle = { 88u + index * 8u, 0u, 8u, 8u };
        if(index % 3u != 2u){
            region.minimumWidth = 24.0f;
            region.minimumHeight = 24.0f;
        }
        regions.push_back(Move(region));
    }
    m_skin.setAtlas(Core::Assets::AssetRef<Texture>("tests/ui/texture"), 144u, 16u, 1.0f, Move(regions));
}

bool RadioGroupFixture::declare(const u64 generation, const RadioGroupOptions& options, const Rect& bounds){
    return declareSource(generation, m_source, m_state, options, bounds);
}

bool RadioGroupFixture::declareSource(const u64 generation, const IListDataSource& source, RadioGroupState& state,
    const RadioGroupOptions& options, const Rect& bounds
){
    if(!begin(generation) || !m_builder.beginPanel("panel", bounds))
        return false;
    m_result = m_builder.radioGroup("radio", source, state, options);
    return m_result.valid;
}

bool RadioGroupFixture::prepare(const u64 generation, const RadioGroupOptions& options, const Rect& bounds){
    return declare(generation, options, bounds) && finishPanel();
}

bool RadioGroupFixture::accept(const u64 generation, const RadioGroupOptions& options, const Rect& bounds){
    return prepare(generation, options, bounds) && m_context.commitFrame(generation);
}

WidgetId RadioGroupFixture::host()const{
    return id("radio", "panel");
}

const RadioGroupChoicePlacement* RadioGroupFixture::choice(const u64 keyValue, const RadioGroupState& state)const{
    const RadioGroupPlacement& placement = state.placement();
    for(u32 index = 0u; index < placement.count; ++index){
        if(placement.rows[index].key == keyValue)
            return &placement.rows[index];
    }
    return nullptr;
}

Point RadioGroupFixture::choicePoint(const u64 keyValue)const{
    const RadioGroupChoicePlacement* item = choice(keyValue, m_state);
    return item ? RadioCenter(item->rectangle) : Point{};
}

void RadioGroupFixture::press(const Core::Key::Enum keyValue, const bool repeat){
    InputEvent event;
    event.type = InputEventType::KeyDown;
    event.key = keyValue;
    event.repeat = repeat;
    EXPECT_TRUE(send(event).keyboardConsumed);
    if(!repeat){
        event.type = InputEventType::KeyUp;
        EXPECT_TRUE(send(event).keyboardConsumed);
    }
}

RadioAcceptedFrame RadioGroupFixture::accepted()const{
    RadioAcceptedFrame saved;
    saved.count = m_context.input().targets().size();
    saved.popupCount = m_context.input().popupCount();
    saved.generation = m_context.input().layoutGeneration();
    saved.focus = m_context.input().focus();
    saved.capture = m_context.input().capture();
    for(usize index = 0u; index < Min(saved.count, saved.targets.size()); ++index)
        saved.targets[index] = m_context.input().targets()[index];
    return saved;
}

void RadioGroupFixture::expectAccepted(const RadioAcceptedFrame& saved, const bool focus, const bool popupScopes)const{
    EXPECT_EQ(m_context.input().layoutGeneration(), saved.generation);
    if(popupScopes)
        EXPECT_EQ(m_context.input().popupCount(), saved.popupCount);
    if(focus)
        EXPECT_EQ(m_context.input().focus(), saved.focus);
    EXPECT_EQ(m_context.input().capture(), saved.capture);
    ASSERT_EQ(m_context.input().targets().size(), saved.count);
    ASSERT_LE(saved.count, saved.targets.size());
    for(usize index = 0u; index < saved.count; ++index){
        const HitTarget& before = saved.targets[index];
        const HitTarget& after = m_context.input().targets()[index];
        EXPECT_EQ(after.id, before.id);
        EXPECT_EQ(after.declarationGeneration, before.declarationGeneration);
        EXPECT_EQ(after.control, before.control);
        EXPECT_EQ(after.owner, before.owner);
        EXPECT_EQ(after.enabled, before.enabled);
        ExpectRadioRect(after.rectangle, before.rectangle);
        ExpectRadioRect(after.clip, before.clip);
    }
}

usize RadioGroupFixture::regionQuads(const DrawSnapshot& snapshot, const Name& name)const{
    const UiSkinRegion* region = m_skin.findRegion(name);
    if(!region)
        return 0u;
    constexpr f32 s_UvTolerance = 0.00000024f;
    const f32 left = static_cast<f32>(region->rectangle.x) / static_cast<f32>(m_skin.atlasWidth());
    const f32 right = static_cast<f32>(region->rectangle.x + region->rectangle.width)
        / static_cast<f32>(m_skin.atlasWidth());
    usize count = 0u;
    for(const DrawCommand& command : snapshot.commands()){
        if(command.material != PaintMaterial::Skin)
            continue;
        for(u32 index = command.firstIndex; index + 5u < command.firstIndex + command.indexCount; index += 6u){
            const Vertex& first = snapshot.vertices()[snapshot.indices()[index]];
            const Vertex& opposite = snapshot.vertices()[snapshot.indices()[index + 2u]];
            if(
                Abs(first.texCoord.x - left) <= s_UvTolerance
                && Abs(opposite.texCoord.x - right) <= s_UvTolerance
            )
                ++count;
        }
    }
    return count;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

