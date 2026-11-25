// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/os/accessibility_service.h>

#include <global/optional.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct IRawElementProviderSimple;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_win32_accessibility{
    struct SharedUiaState;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Win32AccessibilityService final : public TrackedAccessibilityService{
public:
    Win32AccessibilityService(Alloc::GlobalArena& arena, NotNull<void*> nativeWindowHandle);
    virtual ~Win32AccessibilityService()noexcept override;


public:
    [[nodiscard]] virtual AccessibilityCapabilities capabilities()const noexcept override;
    [[nodiscard]] Optional<isize> handleObjectMessage(usize wParam, isize lParam)noexcept;
    [[nodiscard]] IRawElementProviderSimple* rootProvider()noexcept;
    [[nodiscard]] b8 enqueueActionFromProvider(const AccessibilityActionEvent& event)noexcept{ return enqueueAction(event); }


protected:
    virtual void onTreePublished(
        Span<const AccessibilityNodeData> nodes,
        AStringView textStorage,
        u64 focusedId,
        u64 version,
        f32 scaleX,
        f32 scaleY
    )override;
    virtual void onAnnouncement(AStringView text, AccessibilityLiveMode::Enum politeness)override;


private:
    NotNull<void*> m_nativeWindowHandle;
    __hidden_win32_accessibility::SharedUiaState* m_shared = nullptr;
    IRawElementProviderSimple* m_rootProvider = nullptr;
    void* m_uiaLibrary = nullptr;
    void* m_returnRawElementProviderProc = nullptr;
    void* m_hostProviderFromHwndProc = nullptr;
    void* m_clientsAreListeningProc = nullptr;
    void* m_raiseAutomationEventProc = nullptr;
    void* m_raiseStructureChangedEventProc = nullptr;
    void* m_raiseNotificationEventProc = nullptr;
    void* m_disconnectProviderProc = nullptr;
    u64 m_lastFocusId = 0u;
    usize m_lastNodeCount = 0u;
    b8 m_uiaAvailable = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
