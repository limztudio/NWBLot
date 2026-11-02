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
    [[nodiscard]] b8 matchesSurrounding(const EditModel& model)const noexcept;
    [[nodiscard]] b8 matches(const EditModel& model)const noexcept;
    void captureSurrounding(const EditModel& model);
    void capture(const EditModel& model);
    void clear()noexcept;


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

