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
        const TextAreaModelStamp current = model.stamp();
        return current.revision == m_expectedStamp.revision && current.externalRevision == m_expectedStamp.externalRevision && current.selectionGeneration == m_expectedStamp.selectionGeneration
            && model.text() == AStringView(m_expectedText) && current.anchor == m_expectedStamp.anchor && current.caret == m_expectedStamp.caret;
    }
    [[nodiscard]] b8 matches(const EditModel& model)const noexcept{
        const EditCompositionView current = model.composition();
        return model.stamp() == m_expectedStamp && model.text() == AStringView(m_expectedText) && current.active == m_expectedComposition.active && current.text == AStringView(m_expectedPreedit)
            && current.anchor == m_expectedComposition.anchor && current.caret == m_expectedComposition.caret && current.replacementStart == m_expectedComposition.replacementStart && current.replacementEnd == m_expectedComposition.replacementEnd;
    }
    void captureSurrounding(const EditModel& model);
    void capture(const EditModel& model);
    void clear()noexcept;


private:
    void captureSurroundingValues(AStringView text, const TextAreaModelStamp& stamp)noexcept;
    void captureValues(AStringView text, const TextAreaModelStamp& stamp, const EditCompositionView& composition)noexcept;


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

