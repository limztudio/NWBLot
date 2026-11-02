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
        m_expectedText.assign(model.text().data(), model.text().size());
        m_expectedStamp = model.stamp();
        m_expectedRevision = m_expectedStamp.revision;
        m_expectedExternalRevision = m_expectedStamp.externalRevision;
    }
    void capture(const EditModel& model){
        captureSurrounding(model);
        m_expectedComposition = model.composition();
        m_expectedPreedit.assign(m_expectedComposition.text.data(), m_expectedComposition.text.size());
        m_expectedComposition.text = {};
    }
    void clear()noexcept{
        m_expectedText.clear();
        m_expectedPreedit.clear();
        m_expectedComposition = {};
        m_expectedStamp = {};
        m_expectedRevision = 0u;
        m_expectedExternalRevision = 0u;
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

