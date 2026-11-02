// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "model_snapshot.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 EditModelSnapshot::matchesSurrounding(const EditModel& model)const noexcept{
    return
        model.revision() == m_expectedStamp.revision && model.externalRevision() == m_expectedStamp.externalRevision
        && model.selectionGeneration() == m_expectedStamp.selectionGeneration
        && model.text() == AStringView(m_expectedText)
        && model.anchor() == m_expectedStamp.anchor && model.caret() == m_expectedStamp.caret
    ;
}

b8 EditModelSnapshot::matches(const EditModel& model)const noexcept{
    const auto composition = model.composition();
    return
        model.matchesStamp(m_expectedStamp)
        && model.text() == AStringView(m_expectedText)
        && composition.active == m_expectedComposition.active && composition.text == AStringView(m_expectedPreedit)
        && composition.anchor == m_expectedComposition.anchor && composition.caret == m_expectedComposition.caret
        && composition.replacementStart == m_expectedComposition.replacementStart
        && composition.replacementEnd == m_expectedComposition.replacementEnd
    ;
}

void EditModelSnapshot::captureSurrounding(const EditModel& model){
    m_expectedText.assign(model.text().data(), model.text().size());
    m_expectedStamp = model.stamp();
    m_expectedRevision = m_expectedStamp.revision;
    m_expectedExternalRevision = m_expectedStamp.externalRevision;
}

void EditModelSnapshot::capture(const EditModel& model){
    captureSurrounding(model);
    m_expectedComposition = model.composition();
    m_expectedPreedit.assign(m_expectedComposition.text.data(), m_expectedComposition.text.size());
    m_expectedComposition.text = {};
}

void EditModelSnapshot::clear()noexcept{
    m_expectedText.clear();
    m_expectedPreedit.clear();
    m_expectedComposition = {};
    m_expectedStamp = {};
    m_expectedRevision = 0u;
    m_expectedExternalRevision = 0u;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

