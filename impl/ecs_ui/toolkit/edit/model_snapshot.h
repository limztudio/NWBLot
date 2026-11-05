// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "model.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Copies the exact model fence for asynchronous clipboard and native text-input work without retaining a model pointer.
class EditModelSnapshot final{
public:
    explicit EditModelSnapshot(Core::Alloc::GlobalArena& arena)noexcept
        : m_expectedText(arena)
        , m_expectedPreedit(arena)
    {}


public:
    [[nodiscard]] b8 matchesSurrounding(const EditModel& model)const noexcept{
        return MatchesEditModelSurrounding(model.stamp(), model.text(), m_expectedStamp, AStringView(m_expectedText));
    }
    [[nodiscard]] b8 matches(const EditModel& model)const noexcept{
        return MatchesEditModelSnapshot(model.stamp(), model.text(), model.composition(), m_expectedStamp, AStringView(m_expectedText), m_expectedComposition, AStringView(m_expectedPreedit));
    }
    void captureSurrounding(const EditModel& model){
        CaptureEditModelSurrounding(
            model.text(),
            model.stamp(),
            m_expectedText,
            m_expectedStamp,
            m_expectedRevision,
            m_expectedExternalRevision
        );
    }
    void capture(const EditModel& model){
        CaptureEditModelSnapshot(
            model.text(),
            model.stamp(),
            model.composition(),
            m_expectedText,
            m_expectedPreedit,
            m_expectedComposition,
            m_expectedStamp,
            m_expectedRevision,
            m_expectedExternalRevision
        );
    }
    void clear()noexcept{
        ClearEditModelSnapshot(
            m_expectedText,
            m_expectedPreedit,
            m_expectedComposition,
            m_expectedStamp,
            m_expectedRevision,
            m_expectedExternalRevision
        );
    }


public:
    AString<Core::Alloc::GlobalArena> m_expectedText;
    AString<Core::Alloc::GlobalArena> m_expectedPreedit;
    EditCompositionView m_expectedComposition;
    TextAreaModelStamp m_expectedStamp;
    u64 m_expectedRevision = 0u;
    u64 m_expectedExternalRevision = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

