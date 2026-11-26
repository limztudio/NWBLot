// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "accessibility.h"

#include <global/sync.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class TrackedAccessibilityService : public IAccessibilityService{
public:
    explicit TrackedAccessibilityService(Alloc::GlobalArena& arena);
    virtual ~TrackedAccessibilityService()noexcept override;


public:
    [[nodiscard]] virtual b8 isOwnerThread()const noexcept override final{ return QueryCurrentThreadId() == m_ownerThread; }
    [[nodiscard]] virtual b8 publishTree(
        Span<const AccessibilityNodeData> nodes,
        AStringView textStorage,
        u64 focusedId,
        u64 version,
        f32 scaleX,
        f32 scaleY
    )override final;
    [[nodiscard]] virtual b8 updateFocus(u64 focusedId)override final;
    [[nodiscard]] virtual b8 announce(AStringView text, AccessibilityLiveMode::Enum politeness)override final;
    [[nodiscard]] virtual b8 pollAction(AccessibilityActionEvent& outEvent)noexcept override final;
    [[nodiscard]] virtual u64 publishedVersion()const noexcept override final{ return m_publishedVersion; }
    [[nodiscard]] virtual usize publishedNodeCount()const noexcept override final{ return m_publishedNodeCount; }
    [[nodiscard]] virtual u64 publishedFocusedId()const noexcept override final{ return m_publishedFocusedId; }
    [[nodiscard]] virtual usize announcementCount()const noexcept override final{ return m_announcementCount; }


protected:
    [[nodiscard]] b8 enqueueAction(const AccessibilityActionEvent& event)noexcept;
    virtual void onTreePublished(
        Span<const AccessibilityNodeData> nodes,
        AStringView textStorage,
        u64 focusedId,
        u64 version,
        f32 scaleX,
        f32 scaleY
    );
    virtual void onFocusUpdated(u64 focusedId);
    virtual void onAnnouncement(AStringView text, AccessibilityLiveMode::Enum politeness);


private:
    Array<AccessibilityActionEvent, s_AccessibilityMaxActions> m_actions{};
    Futex m_actionMutex;
    Atomic<usize> m_pendingActionCount{ 0u };
    usize m_actionHead = 0u;
    usize m_actionCount = 0u;
    const ThreadId m_ownerThread;
    u64 m_publishedVersion = 0u;
    u64 m_publishedFocusedId = 0u;
    usize m_publishedNodeCount = 0u;
    usize m_announcementCount = 0u;
    f32 m_scaleX = 1.0f;
    f32 m_scaleY = 1.0f;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
