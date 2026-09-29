// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "layer_system.h"

#include <impl/assets_ui_skin/toolkit_contract.h>

#include <core/common/log.h>
#include <core/ecs/world.h>

#include <global/simplemath.h>
#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void UiLayerSystem::update(Core::ECS::World& world, const f32 delta){
    static_cast<void>(world);
    m_frameDelta.add(delta);
    synchronizeInput();
    const UiSkinChangeResult::Enum skinChange = m_skinSelection.applyChangeIfReady(
        m_resourcesReady,
        m_renderer.hasPendingFrame(),
        m_context.ready(),
        [this](const Core::Assets::AssetRef<UiSkin>& ref, const u64 generation){
            UniquePtr<Core::Assets::IAsset> candidateAsset;
            const UiSkin* skin = m_assetManager.loadTypedSync<UiSkin>(
                ref.name(), candidateAsset, MakeNotNull(NWB_TEXT("UiLayerSystem")), MakeNotNull("UI skin")
            );
            if(!skin || !ValidateUiSkinToolkitContract(*skin) || !m_renderer.setSkin(ref, *skin, generation))
                return false;
            m_skinAsset = Move(candidateAsset);
            return true;
        }
    );
    if(skinChange == UiSkinChangeResult::Failed)
        NWB_LOGGER_WARNING(NWB_TEXT("UiLayerSystem: UI skin request failed; retaining the selected skin"));
    if(!m_resourcesReady || m_renderer.hasPendingFrame() || m_context.ready())
        return;
    if(m_frameGeneration == Limit<u64>::s_Max)
        TerminateInvariant();
    ++m_frameGeneration;
    const UiSkin* skin = Core::Assets::CastAsset<UiSkin>(m_skinAsset.get());
    NWB_FATAL_ASSERT(skin);
    if(!m_context.beginFrame(m_frameGeneration))
        TerminateInvariant();
    const f32 frameDelta = m_frameDelta.consume();
    m_editHost.beginFrame(m_frameGeneration, m_display);
    if(m_editHost.takeClipboardFailure())
        NWB_LOGGER_WARNING(NWB_TEXT("UiLayerSystem: clipboard publication failed"));
    m_ui.reset();
    m_ui.setSkin(*skin);
    m_paint.begin(
        m_display,
        m_frameGeneration,
        m_skinSelection.generation(),
        m_skinSelection.selected(),
        *skin
    );
    m_paint.reserve(256u);
    m_ui.setDeltaSeconds(frameDelta);
    m_ui.setPointerBusy(m_pressedButtons != 0u);
    UiPaintContext context{ m_world, m_clipboard, m_textInput, m_paint, m_text, m_display, m_ui, Core::ECS::ENTITY_ID_INVALID, frameDelta };
    for(const auto& root : m_liveRoots){
        UiPaintComponent* component = m_world.tryGetComponent<UiPaintComponent>(root.entity);
        if(!component || !component->visible || !component->paint)
            continue;
        if(!m_context.beginRoot({ root.entity.id, 1u }))
            break;
        context.entity = root.entity;
        // Keep mutable callback state and its captures alive if it removes/replaces its own component.
        UiPaintCallback callback = Move(component->paint);
        callback(context);
        UiPaintComponent* updated = m_world.tryGetComponent<UiPaintComponent>(root.entity);
        if(updated && !updated->paint)
            updated->paint = Move(callback);
        if(!m_ui.balanced())
            m_context.fail();
        if(!m_context.endRoot())
            break;
    }
    if(!m_context.finishFrame()){
        NWB_LOGGER_ERROR(NWB_TEXT("UiLayerSystem: rejected unbalanced or invalid UI declarations"));
        m_context.abandonFrame();
        m_editHost.reset();
        m_ui.reset();
        return;
    }
    m_editHost.finishFrame();
    if(!m_renderer.submit(m_paint.freeze())){
        NWB_LOGGER_ERROR(NWB_TEXT("UiLayerSystem: GPU renderer rejected a new paint snapshot"));
        m_context.abandonFrame();
        m_editHost.reset();
    }
}

bool UiLayerSystem::collectRoots(){
    m_liveRoots.clear();
    m_rootIdentities.clear();
    bool overflow = false;
    m_world.view<UiPaintComponent>().each([this, &overflow](const Core::ECS::EntityID entity, UiPaintComponent& component){
        if(!component.visible || !component.paint)
            return;
        if(m_liveRoots.size() == Ui::s_InputMaxTargets){
            overflow = true;
            return;
        }
        m_liveRoots.push_back({ entity, component.order });
        m_rootIdentities.push_back({ entity.id, 1u });
    });
    m_context.retainRoots(m_rootIdentities.data(), m_rootIdentities.size());
    Sort(m_liveRoots.begin(), m_liveRoots.end(), [](const LiveRoot& lhs, const LiveRoot& rhs){
        return lhs.order != rhs.order ? lhs.order < rhs.order : lhs.entity.id < rhs.entity.id;
    });
    return !overflow;
}

void UiLayerSystem::synchronizeInput(){
    if(!collectRoots()){
        m_context.resetInput();
        m_editHost.reset();
        return;
    }
    if(m_context.ready() && m_renderer.lastAcceptedGeneration() == m_context.readyGeneration()){
        const auto status = m_renderer.lastAcceptedPresentationStatus();
        if(status == Core::PresentationReceiptStatus::Accepted){
            const bool committed = m_context.commitFrame(m_renderer.lastAcceptedGeneration());
            NWB_FATAL_ASSERT(committed);
            m_editHost.commitFrame(m_renderer.lastAcceptedGeneration());
        }
        else if(status == Core::PresentationReceiptStatus::Rejected){
            m_context.abandonFrame();
            m_editHost.reset();
        }
    }
    m_editHost.synchronizeFocus();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

