// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "layer_system.h"

#include <core/common/log.h>
#include <core/ecs/world.h>

#include <global/simplemath.h>
#include <global/scope_exit.h>
#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<void, Core::CpuTaskAdmissionError::Enum> UiLayerSystem::update([[maybe_unused]] Core::ECS::World& world, const f32 delta){
    m_frameDelta.add(delta);
    synchronizeInput();
    if(!m_resourcesReady || m_renderer.hasPendingFrame() || m_context.ready())
        return {};
    if(m_frameGeneration == Limit<u64>::s_Max)
        TerminateInvariant();
    ++m_frameGeneration;
    const UiSkin& skin = *MakeNotNull(Core::Assets::CastAsset<UiSkin>(m_skinAsset.get()));
    if(!m_context.beginFrame(m_frameGeneration))
        TerminateInvariant();
    ScopeExit abandon([this]()noexcept{ abandonPendingFrame(true); });
    const f32 frameDelta = m_frameDelta.consume();
    m_editHost.beginFrame(m_frameGeneration, m_display);
    if(m_editHost.takeClipboardFailure())
        NWB_LOGGER_WARNING(NWB_TEXT("UiLayerSystem: clipboard publication failed"));
    m_ui.reset();
    m_ui.setSkin(skin);
    m_paint.begin(m_display, m_frameGeneration, m_skinSelection.generation, Core::Assets::AssetRef<UiSkin>(skin.virtualPath()), skin);
    m_paint.reserve(s_UiLayerPaintReserveQuads);
    m_ui.setDeltaSeconds(frameDelta);
    m_ui.setPointerBusy(m_pressedButtons != 0u || m_context.input().primaryDown() || m_context.input().secondaryDown());
    UiPaintContext context{ m_world, m_clipboard, m_textInput, m_paint, m_text, m_display, m_ui, Core::ECS::s_InvalidEntityId, frameDelta };
    Sort(m_liveRoots.begin(), m_liveRoots.end(), [](const UiLiveRoot& lhs, const UiLiveRoot& rhs)noexcept{
        return lhs.order != rhs.order ? lhs.order < rhs.order : lhs.entity.id < rhs.entity.id;
    });
    for(const auto& root : m_liveRoots){
        UiPaintComponent* component = m_world.tryGetComponent<UiPaintComponent>(root.entity);
        if(!component || !component->visible || !component->paint)
            continue;
        if(!m_context.beginRoot({ root.entity.id, 1u }))
            break;
        context.entity = root.entity;
        component->paint.invoke(context);
        if(!m_ui.balanced())
            m_context.fail();
        if(!m_context.endRoot())
            break;
    }
    if(!m_context.finishFrame()){
        NWB_LOGGER_ERROR(NWB_TEXT("UiLayerSystem: rejected unbalanced or invalid UI declarations"));
        return {};
    }
    m_editHost.finishFrame();
    if(!m_renderer.submit(m_paint.freeze())){
        NWB_LOGGER_ERROR(NWB_TEXT("UiLayerSystem: GPU renderer rejected a new paint snapshot"));
        abandonPendingFrame();
        abandon.release();
        return {};
    }
    abandon.release();
    return {};
}

void UiLayerSystem::collectRoots(){
    m_liveRoots.clear();
    m_rootIdentities.clear();
    m_world.view<UiPaintComponent>().each([this](const Core::ECS::EntityID entity, UiPaintComponent& component){
        if(!component.visible || !component.paint.present())
            return;
        m_liveRoots.push_back({ entity, component.order });
        m_rootIdentities.push_back({ entity.id, 1u });
    });
}

void UiLayerSystem::synchronizeInput(){
    collectRoots();
    m_context.retainRoots(m_rootIdentities);
    if(m_context.ready() && m_renderer.lastAcceptedGeneration() == m_context.readyGeneration()){
        const auto status = m_renderer.lastAcceptedPresentationStatus();
        if(status == Core::PresentationReceiptStatus::Accepted){
            const b8 committed = m_context.commitFrame(m_renderer.lastAcceptedGeneration());
            NWB_FATAL_ASSERT(committed);
            m_editHost.commitFrame(m_renderer.lastAcceptedGeneration());
        }
        else if(status == Core::PresentationReceiptStatus::Rejected)
            abandonPendingFrame();
    }
    m_editHost.synchronizeFocus();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

