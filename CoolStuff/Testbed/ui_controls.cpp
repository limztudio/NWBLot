// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "project.h"
#include "ui_gallery_constants.h"

#include <global/simd/vector2.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TESTBED_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void Project::drawUiControls(NWB::Impl::UiPaintContext& context){
    NWB::Impl::Ui::WindowOptions options;
    options.initialBounds = { s_GalleryMargin, s_GalleryMargin, s_OverviewWidth, s_OverviewHeight };
    options.contentHeightFirstUse = true;
    const auto gallery = m_uiPreview.galleryBounds(context);
    if(!gallery)
        return;
    const auto& overview = options.initialBounds;
    const SIMDVectorSingle overviewEnd = VectorAdd(VectorSet(overview.x, overview.y, 0.0f, 0.0f), VectorSet(overview.width, overview.height, 0.0f, 0.0f));
    if(Vector2Less(VectorSet(gallery->x, gallery->y, 0.0f, 0.0f), overviewEnd))
        return;
    NWB::Impl::Ui::SeparatorOptions separator;
    separator.thickness = 1.0f;
    if(context.ui.beginWindow("testbed_window", s_WindowTitleText, m_uiWindow, options)){
        const b8 declared = context.ui.label("renderer", s_RendererLine) && context.ui.separator("separator", separator) && context.ui.label("character", s_CharacterLine);
        NWB_FATAL_ASSERT(declared);
    }
    const b8 ended = context.ui.endWindow();
    NWB_FATAL_ASSERT(ended);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TESTBED_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

