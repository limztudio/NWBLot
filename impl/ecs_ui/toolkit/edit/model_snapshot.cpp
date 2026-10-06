// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "model_snapshot.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


EditModelSnapshot::EditModelSnapshot(Core::Alloc::GlobalArena& arena)noexcept
    : m_expectedText(arena)
    , m_expectedPreedit(arena)
{}

bool EditModelSnapshot::matches(const EditModel& model)const noexcept{
    const auto composition = model.composition();
    return
        model.revision() == m_expectedRevision && model.externalRevision() == m_expectedExternalRevision
        && model.selectionGeneration() == m_expectedSelectionGeneration
        && model.compositionGeneration() == m_expectedCompositionGeneration
        && model.text() == AStringView(m_expectedText)
        && model.anchor() == m_expectedAnchor && model.caret() == m_expectedCaret
        && composition.active == m_expectedComposition.active && composition.text == AStringView(m_expectedPreedit)
        && composition.anchor == m_expectedComposition.anchor && composition.caret == m_expectedComposition.caret
        && composition.replacementStart == m_expectedComposition.replacementStart
        && composition.replacementEnd == m_expectedComposition.replacementEnd
    ;
}

void EditModelSnapshot::capture(const EditModel& model){
    m_expectedText.assign(model.text().data(), model.text().size());
    m_expectedAnchor = model.anchor();
    m_expectedCaret = model.caret();
    m_expectedRevision = model.revision();
    m_expectedExternalRevision = model.externalRevision();
    m_expectedSelectionGeneration = model.selectionGeneration();
    m_expectedCompositionGeneration = model.compositionGeneration();
    m_expectedComposition = model.composition();
    m_expectedPreedit.assign(m_expectedComposition.text.data(), m_expectedComposition.text.size());
    m_expectedComposition.text = {};
}

void EditModelSnapshot::clear()noexcept{
    m_expectedText.clear();
    m_expectedPreedit.clear();
    m_expectedComposition = {};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

