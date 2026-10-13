// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "layer_system.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool UiLayerSystem::pointerInput(const Ui::PointerInput& input){
    synchronizeNativeInput();
    bool consumed = false;
    routePointerInput(input, &consumed);
    return consumed;
}

void UiLayerSystem::cancelDeviceInput(const u64 device){
    synchronizeNativeInput();
    const Ui::InputSource source = m_context.input().activePointerSource();
    if(source.valid() && source.device == device){
        Ui::PointerInput input;
        input.source = source;
        input.phase = Ui::PointerPhase::Cancel;
        routePointerInput(input);
    }
    m_context.input().cancelDeviceInput(device);
    if(device == 0u){
        m_pressedButtons = 0u;
        m_pointerOwner = 0u;
        m_nativeKeyOwners.fill(0u);
        m_blockNativeChars = false;
        m_blockCommandChars = false;
    }
}

void UiLayerSystem::routePointerInput(const Ui::PointerInput& input, bool* consumed){
    if(consumed)
        *consumed = false;
    m_editHost.collectNative();
    const Ui::WidgetId previousCapture = m_context.input().capture();
    const auto normalized = m_context.input().queuePointer(input);
    if(!normalized){
        NWB_LOGGER_ERROR(NWB_TEXT("UiLayerSystem: invalid or overflowing pointer input"));
        m_context.resetInput();
        m_editHost.reset();
        m_blockCommandChars = false;
        return;
    }
    const Ui::InputRoutingResult result = m_context.input().process();
    if(result.pointerRouted)
        m_editHost.input(*normalized, previousCapture, false);
    if(result.activationOverflow)
        NWB_LOGGER_WARNING(NWB_TEXT("UiLayerSystem: bounded activation queue is full"));
    if(result.gestureOverflow)
        NWB_LOGGER_WARNING(NWB_TEXT("UiLayerSystem: bounded pointer gesture queue is full"));
    if(consumed)
        *consumed = result.pointerConsumed;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

