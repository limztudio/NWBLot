// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "accessibility_service.h"

#include <global/simd/vector.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_accessibility_service{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static b8 ValidTextRange(const u32 offset, const u32 length, const usize textBytes)noexcept{
    const u64 begin = static_cast<u64>(offset);
    const u64 size = static_cast<u64>(length);
    return begin <= static_cast<u64>(textBytes) && size <= static_cast<u64>(textBytes) - begin;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TrackedAccessibilityService::TrackedAccessibilityService([[maybe_unused]] Alloc::GlobalArena& arena)
    : m_ownerThread(QueryCurrentThreadId())
{}

TrackedAccessibilityService::~TrackedAccessibilityService()noexcept{
    FOUNDATION_ASSERT(isOwnerThread());
}

b8 TrackedAccessibilityService::publishTree(
    const Span<const AccessibilityNodeData> nodes,
    const AStringView textStorage,
    const u64 focusedId,
    const u64 version,
    const f32 scaleX,
    const f32 scaleY
){
    using namespace __hidden_accessibility_service;

    if(!isOwnerThread())
        return false;
    if(nodes.size() > s_AccessibilityMaxNodes || textStorage.size() > s_AccessibilityMaxTextBytes)
        return false;
    const SIMDVectorSingle scales = VectorSet(scaleX, scaleY, 0.0f, 0.0f);
    if(!VectorIsFinite(scales, VectorComponentMask::s_XY) || scaleX <= 0.0f || scaleY <= 0.0f)
        return false;

    const usize textBytes = textStorage.size();
    for(const AccessibilityNodeData& node : nodes){
        if(node.id == 0u)
            return false;
        if(static_cast<u8>(node.role) >= static_cast<u8>(AccessibilityRole::kCount))
            return false;
        if(static_cast<u8>(node.liveMode) > static_cast<u8>(AccessibilityLiveMode::Assertive))
            return false;
        if(!VectorIsFinite(VectorSet(node.x, node.y, node.width, node.height), VectorComponentMask::s_XYZW))
            return false;
        if(!ValidTextRange(node.nameOffset, node.nameLength, textBytes)
            || !ValidTextRange(node.descriptionOffset, node.descriptionLength, textBytes)
            || !ValidTextRange(node.valueOffset, node.valueLength, textBytes)
        )
            return false;
    }

    m_publishedVersion = version;
    m_publishedFocusedId = focusedId;
    m_publishedNodeCount = nodes.size();
    m_scaleX = scaleX;
    m_scaleY = scaleY;

    onTreePublished(nodes, textStorage, focusedId, version, scaleX, scaleY);
    return true;
}

b8 TrackedAccessibilityService::updateFocus(const u64 focusedId){
    if(!isOwnerThread())
        return false;
    m_publishedFocusedId = focusedId;
    onFocusUpdated(focusedId);
    return true;
}

b8 TrackedAccessibilityService::announce(const AStringView text, const AccessibilityLiveMode::Enum politeness){
    if(!isOwnerThread())
        return false;
    if(text.empty() || text.size() > s_AccessibilityMaxTextBytes)
        return false;
    if(politeness == AccessibilityLiveMode::Off || static_cast<u8>(politeness) > static_cast<u8>(AccessibilityLiveMode::Assertive))
        return false;

    ++m_announcementCount;
    onAnnouncement(text, politeness);
    return true;
}

b8 TrackedAccessibilityService::pollAction(AccessibilityActionEvent& outEvent)noexcept{
    if(!isOwnerThread())
        return false;
    if(m_pendingActionCount.load(MemoryOrder::acquire) == 0u)
        return false;
    const FutexGuard lock = AcquireFutex(m_actionMutex);
    if(m_actionCount == 0u){
        m_pendingActionCount.store(0u, MemoryOrder::release);
        return false;
    }
    outEvent = m_actions[m_actionHead];
    m_actionHead = (m_actionHead + 1u) % s_AccessibilityMaxActions;
    --m_actionCount;
    m_pendingActionCount.store(m_actionCount, MemoryOrder::release);
    return true;
}

b8 TrackedAccessibilityService::enqueueAction(const AccessibilityActionEvent& event)noexcept{
    if(event.targetId == 0u || static_cast<u8>(event.kind) > static_cast<u8>(AccessibilityActionKind::ContextMenu))
        return false;
    const FutexGuard lock = AcquireFutex(m_actionMutex);
    if(m_actionCount >= s_AccessibilityMaxActions)
        return false;
    const usize tailIdx = (m_actionHead + m_actionCount) % s_AccessibilityMaxActions;
    m_actions[tailIdx] = event;
    ++m_actionCount;
    m_pendingActionCount.store(m_actionCount, MemoryOrder::release);
    return true;
}

void TrackedAccessibilityService::onTreePublished(
    [[maybe_unused]] Span<const AccessibilityNodeData> nodes,
    [[maybe_unused]] AStringView textStorage,
    [[maybe_unused]] u64 focusedId,
    [[maybe_unused]] u64 version,
    [[maybe_unused]] f32 scaleX,
    [[maybe_unused]] f32 scaleY
){}

void TrackedAccessibilityService::onFocusUpdated([[maybe_unused]] u64 focusedId){}

void TrackedAccessibilityService::onAnnouncement(
    [[maybe_unused]] AStringView text,
    [[maybe_unused]] AccessibilityLiveMode::Enum politeness
){}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
