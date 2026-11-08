// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "model_snapshot.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void EditModelSnapshot::captureSurrounding(const EditModel& model){
    captureSurroundingValues(model.text(), model.stamp());
}

void EditModelSnapshot::capture(const EditModel& model){
    captureValues(model.text(), model.stamp(), model.composition());
}

void EditModelSnapshot::clear()noexcept{
    m_expectedText.clear();
    m_expectedPreedit.clear();
    m_expectedComposition = EditCompositionView();
    m_expectedStamp = TextAreaModelStamp();
    m_expectedRevision = 0u;
    m_expectedExternalRevision = 0u;
}

void EditModelSnapshot::captureSurroundingValues(const AStringView text, const TextAreaModelStamp& stamp)noexcept{
    m_expectedText.assign(text);
    m_expectedStamp = stamp;
    m_expectedRevision = stamp.revision;
    m_expectedExternalRevision = stamp.externalRevision;
}

void EditModelSnapshot::captureValues(const AStringView text, const TextAreaModelStamp& stamp, const EditCompositionView& composition)noexcept{
    captureSurroundingValues(text, stamp);
    m_expectedComposition = composition;
    m_expectedPreedit.assign(composition.text);
    m_expectedComposition.text = "";
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

