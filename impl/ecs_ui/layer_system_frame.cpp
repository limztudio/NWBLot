// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "layer_system.h"

#include <core/common/log.h>
#include <core/ecs/world.h>

#include <global/simplemath.h>
#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void UiLayerSystem::update(Core::ECS::World& world, const f32 delta){
    static_cast<void>(world);
    if(!m_resourcesReady || m_renderer.hasPendingFrame())
        return;
    if(m_frameGeneration == Limit<u64>::s_Max)
        TerminateInvariant();
    ++m_frameGeneration;
    const UiSkin* skin = Core::Assets::CastAsset<UiSkin>(m_skinAsset.get());
    NWB_FATAL_ASSERT(skin);
    m_paint.begin(m_display, m_frameGeneration, m_skinGeneration, m_skinRef, *skin);
    m_paint.reserve(256u);
    const f32 safeDelta = IsFinite(delta) && delta >= 0.0f ? delta : 0.0f;
    UiPaintContext context{ m_world, m_clipboard, m_paint, m_text, m_display, Core::ECS::ENTITY_ID_INVALID, safeDelta };
    m_world.view<UiPaintComponent>().each([&context](const Core::ECS::EntityID entity, UiPaintComponent& component){
        if(component.visible && component.paint){
            context.entity = entity;
            component.paint(context);
        }
    });
    if(!m_renderer.submit(m_paint.freeze()))
        NWB_LOGGER_ERROR(NWB_TEXT("UiLayerSystem: GPU renderer rejected a new paint snapshot"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

