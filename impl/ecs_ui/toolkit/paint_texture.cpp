// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "paint.h"

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool PaintBuilder::prepareTextureImages(const SharedImageSource* const images, const usize count){
    return prepareImages(nullptr, 0u, nullptr, 0u, images, count);
}

bool PaintBuilder::drawImage(
    const SharedImageSource& source,
    const Rect& rectangle,
    const Rect& uv,
    const Color& tint
){
    NWB_ASSERT(m_recording);
    if(
        !source || !IsFinite(rectangle.x) || !IsFinite(rectangle.y)
        || !IsFinite(rectangle.width) || !IsFinite(rectangle.height)
        || rectangle.width < 0.0f || rectangle.height < 0.0f
        || !IsFinite(rectangle.x + rectangle.width) || !IsFinite(rectangle.y + rectangle.height)
        || !IsFinite(uv.x) || !IsFinite(uv.y) || !IsFinite(uv.width) || !IsFinite(uv.height)
        || uv.x < 0.0f || uv.y < 0.0f || uv.width <= 0.0f || uv.height <= 0.0f
        || uv.x + uv.width > 1.0f || uv.y + uv.height > 1.0f
        || !IsFinite(tint.r) || !IsFinite(tint.g) || !IsFinite(tint.b) || !IsFinite(tint.a)
        || tint.a < 0.0f || tint.a > 1.0f
    )
        return false;
    const Rect clip = m_clips.back();
    const f32 visibleLeft = Max(rectangle.x, clip.x);
    const f32 visibleTop = Max(rectangle.y, clip.y);
    const f32 visibleRight = Min(rectangle.x + rectangle.width, clip.x + clip.width);
    const f32 visibleBottom = Min(rectangle.y + rectangle.height, clip.y + clip.height);
    if(
        rectangle.width == 0.0f || rectangle.height == 0.0f || clip.width <= 0.0f || clip.height <= 0.0f
        || tint.a == 0.0f || visibleRight <= visibleLeft || visibleBottom <= visibleTop
    )
        return true;
    if(!prepareTextureImages(&source, 1u))
        return false;
    u32 index = 0u;
    for(; index < m_snapshot.m_textureImages.size(); ++index){
        const ImageSource& bound = *m_snapshot.m_textureImages[index];
        if(bound.identity() == source->identity() && bound.generation() == source->generation())
            break;
    }
    NWB_ASSERT(index < m_snapshot.m_textureImages.size());
    emitQuad(rectangle, uv, tint, PaintMaterial::Image, Limit<u32>::s_Max, Limit<u32>::s_Max, 0u, index);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

