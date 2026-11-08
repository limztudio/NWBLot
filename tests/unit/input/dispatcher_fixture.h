// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/input/module.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace InputDispatcherTestsImpl{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::Core;

class FocusHandler final : public IInputEventHandler{
public:
    explicit FocusHandler(InputDispatcher& dispatcher)noexcept
        : m_dispatcher(dispatcher)
    {}


public:
    virtual void windowFocusUpdate(const b8 focused)override{
        ASSERT_LT(m_focusCount, m_focusValues.size());
        m_focusValues[m_focusCount] = focused;
        ++m_focusCount;
        if(m_removeTarget){
            m_dispatcher.removeHandler(*m_removeTarget);
            m_removeTarget = nullptr;
        }
        if(m_addTarget){
            m_dispatcher.addHandlerToBack(*m_addTarget);
            m_addTarget = nullptr;
        }
        if(m_removeSelf)
            m_dispatcher.removeHandler(*this);
    }

    virtual ::NWB::Core::KeyboardInputResult keyboardUpdate(i32, i32, const i32 action, i32)override{
        if(action == InputAction::Release)
            ++m_keyReleases;
        else
            ++m_keyPresses;
        if(m_loseFocusOnPress && action == InputAction::Press){
            m_dispatcher.windowFocusUpdate(false);
            if(m_regainFocusOnPress)
                m_dispatcher.windowFocusUpdate(true);
        }
        if(m_removeSelfOnPress && action != InputAction::Release)
            m_dispatcher.removeHandler(*this);
        if(m_consumeKeys && action != InputAction::Release)
            ++m_textPolicyQueries;
        return { m_consumeKeys, m_blockText };
    }

    virtual b8 mouseButtonUpdate(i32, const i32 action, i32)noexcept override{
        if(action == InputAction::Release)
            ++m_buttonReleases;
        else
            ++m_buttonPresses;
        return m_consumeButtons;
    }


public:
    InputDispatcher& m_dispatcher;
    Array<b8, 8u> m_focusValues{};
    usize m_focusCount = 0u;
    usize m_keyPresses = 0u;
    usize m_keyReleases = 0u;
    usize m_buttonPresses = 0u;
    usize m_buttonReleases = 0u;
    mutable usize m_textPolicyQueries = 0u;
    IInputEventHandler* m_removeTarget = nullptr;
    IInputEventHandler* m_addTarget = nullptr;
    b8 m_removeSelf = false;
    b8 m_loseFocusOnPress = false;
    b8 m_regainFocusOnPress = false;
    b8 m_removeSelfOnPress = false;
    b8 m_blockText = false;
    b8 m_consumeKeys = false;
    b8 m_consumeButtons = false;
};



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


