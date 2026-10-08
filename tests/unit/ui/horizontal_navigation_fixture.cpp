// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "horizontal_navigation_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UiHorizontalNavigationTests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


HitTarget Part(const HitTarget& host){
    HitTarget part;
    part.id = { 4u };
    part.declarationGeneration = host.declarationGeneration;
    part.rectangle = { 20.0f, 20.0f, 60.0f, 20.0f };
    part.clip = host.clip;
    part.paintOrder = 1u;
    part.activatable = true;
    part.control = host.control;
    part.owner = host.id;
    part.ownerDeclarationGeneration = host.declarationGeneration;
    part.value = 91u;
    part.popup = host.popup;
    part.layer = host.layer;
    return part;
}

PopupScope Popup(const HitTarget& owner, const u64 openGeneration){
    PopupScope popup;
    popup.token = { owner.id, owner.declarationGeneration, 6u, openGeneration };
    popup.bounds = { 0.0f, 0.0f, 300.0f, 200.0f };
    popup.viewport = popup.bounds;
    popup.layer = 1u;
    return popup;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


HorizontalNavigationFixture::HorizontalNavigationFixture()
    : m_arena(Name("tests/ui/horizontal_navigation"))
    , m_router(m_arena)
{
    for(usize index = 0u; index < 2u; ++index){
        HitTarget& host = m_targets[index];
        host.id = { index + 1u };
        host.declarationGeneration = 7u;
        host.rectangle = { static_cast<f32>(index) * 140.0f, 0.0f, 100.0f, 100.0f };
        host.clip = { 0.0f, 0.0f, 300.0f, 200.0f };
        host.focusable = true;
        host.control = { 11u + index, 21u, 31u };
        host.navigable = true;
        host.horizontalNavigation = true;
        host.pageRows = 4u;
    }
    HitTarget& editor = m_targets[2u];
    editor.id = { 3u };
    editor.declarationGeneration = 9u;
    editor.rectangle = { 0.0f, 120.0f, 120.0f, 24.0f };
    editor.clip = m_targets[0u].clip;
    editor.focusable = true;
    editor.textEditable = true;
    editor.control = { 101u, 102u, 103u };
}

InputRoutingResult HorizontalNavigationFixture::send(const InputEvent& event){
    EXPECT_TRUE(m_router.queue(event));
    return m_router.process();
}

InputRoutingResult HorizontalNavigationFixture::keyDown(const Core::Key::Enum key, const bool repeat){
    return send({ .type = InputEventType::KeyDown, .key = key, .repeat = repeat });
}

InputRoutingResult HorizontalNavigationFixture::keyUp(const Core::Key::Enum key){
    return send({ .type = InputEventType::KeyUp, .key = key });
}

void HorizontalNavigationFixture::press(const Core::Key::Enum key){
    EXPECT_TRUE(keyDown(key).keyboardConsumed);
    EXPECT_TRUE(keyUp(key).keyboardConsumed);
}

void HorizontalNavigationFixture::focusTarget(const usize index){
    const Rect& rectangle = m_targets[index].rectangle;
    const Point point{ rectangle.x + 10.0f, rectangle.y + 10.0f };
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryDown, .position = point }).pointerConsumed);
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryUp, .position = point }).pointerConsumed);
    EXPECT_EQ(m_router.focus(), m_targets[index].id);
}

bool HorizontalNavigationFixture::publish(const usize count, const PopupScope* popups, const usize popupCount){
    const u64 generation = m_generation + 1u;
    if(!m_router.commitTargets(m_targets.data(), count, generation, popups, popupCount))
        return false;
    m_generation = generation;
    return true;
}

Expected<ControlAction> HorizontalNavigationFixture::take(const usize index){
    const HitTarget& host = m_targets[index];
    return m_router.consumeControlAction(host.id, host.declarationGeneration, host.control);
}

void HorizontalNavigationFixture::bindEditor(const usize hostIndex){
    const HitTarget& host = m_targets[hostIndex];
    HitTarget& editor = m_targets[2u];
    editor.keyboardOwner = host.id;
    editor.keyboardOwnerDeclarationGeneration = host.declarationGeneration;
    editor.keyboardControl = host.control;
    editor.popup = host.popup;
    editor.layer = host.layer;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

