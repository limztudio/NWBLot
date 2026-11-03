// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "paint.h"
#include "rect_math.h"

#include <impl/ecs_ui/toolkit/layout/validation.adhd.h>

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void PaintBuilder::begin(const DisplayMetrics& metrics, const u64 generation, const u64 skinGeneration, const Core::Assets::AssetRef<UiSkin>& skinRef, const UiSkin& skin){
    NWB_ASSERT(skinRef.valid() && skinRef.name() == skin.virtualPath());
    NWB_ASSERT(skin.atlasWidth() > 0u && skin.atlasHeight() > 0u && skin.referenceDensity() > 0.0f);
    NWB_ASSERT(IsFinite(skin.referenceDensity()));
    NWB_ASSERT(IsFinite(metrics.logicalWidth) && metrics.logicalWidth >= 0.0f);
    NWB_ASSERT(IsFinite(metrics.logicalHeight) && metrics.logicalHeight >= 0.0f);
    NWB_ASSERT(IsFinite(metrics.pixelScaleX) && metrics.pixelScaleX > 0.0f);
    NWB_ASSERT(IsFinite(metrics.pixelScaleY) && metrics.pixelScaleY > 0.0f);
    m_snapshot.m_generation = generation;
    m_snapshot.m_displayMetrics = metrics;
    m_snapshot.m_skinBinding = { skinRef, skin.texture(), skinGeneration, skin.atlasWidth(), skin.atlasHeight(), skin.referenceDensity() };
    m_snapshot.m_vertices.clear();
    m_snapshot.m_indices.clear();
    m_snapshot.m_commands.clear();
    m_snapshot.m_glyphPages.clear();
    m_snapshot.m_sdfPages.clear();
    m_snapshot.m_textureImages.clear();
    m_regions.assign(skin.regions().begin(), skin.regions().end());
    m_clips.clear();
    m_clips.push_back({ 0.0f, 0.0f, metrics.logicalWidth, metrics.logicalHeight });
    m_overlayDepth = 0u;
    m_layer = 0u;
    m_commandsOrdered = true;
    m_recording = true;
}

void PaintBuilder::reserve(const usize quadCount, const usize clipDepth){
    if(quadCount > Limit<u32>::s_Max / s_PaintQuadIndexCount || clipDepth == Limit<usize>::s_Max){
        NWB_ASSERT(false);
        return;
    }
    m_snapshot.m_vertices.reserve(quadCount * s_PaintQuadVertexCount);
    m_snapshot.m_indices.reserve(quadCount * s_PaintQuadIndexCount);
    m_snapshot.m_commands.reserve(quadCount);
    m_clips.reserve(clipDepth + 1u);
}

void PaintBuilder::pushClip(const Rect& clip){
    NWB_ASSERT(m_recording);
    const Rect& parent = m_clips.back();
    Rect next;
    if(HasUiRectArea(parent) && HasUiRectArea(clip)){
        next = IntersectUiRectsBounds(parent, clip);
    }
    m_clips.push_back(next);
}

b8 PaintBuilder::popClip()noexcept{
    NWB_ASSERT(m_recording);
    if(m_clips.size() <= 1u || (m_overlayDepth != 0u && m_clips.size() <= m_overlays[m_overlayDepth - 1u].clipDepth + 1u))
        return false;
    m_clips.pop_back();
    return true;
}

b8 PaintBuilder::beginOverlay(const u32 layer){
    if(!m_recording || layer == 0u || m_overlayDepth == s_PaintMaxOverlayDepth)
        return false;
    const DisplayMetrics& display = m_snapshot.m_displayMetrics;
    m_overlays[m_overlayDepth++] = { m_clips.size(), m_layer };
    m_clips.push_back({ 0.0f, 0.0f, display.logicalWidth, display.logicalHeight });
    m_layer = layer;
    return true;
}

b8 PaintBuilder::endOverlay()noexcept{
    if(!m_recording || m_overlayDepth == 0u || m_clips.size() != m_overlays[m_overlayDepth - 1u].clipDepth + 1u)
        return false;
    m_clips.pop_back();
    m_layer = m_overlays[--m_overlayDepth].layer;
    return true;
}

b8 PaintBuilder::drawRegion(const Name& regionName, const Rect& rectangle, const Color& tint){
    NWB_ASSERT(m_recording);
    const auto region = FindIf(m_regions.begin(), m_regions.end(), [&regionName](const UiSkinRegion& value)noexcept{
        return value.name == regionName;
    });
    if(region == m_regions.end())
        return false;
    if(region->drawMode == UiSkinDrawMode::NineSlice)
        emitNineSlice(*region, rectangle, tint);
    else{
        const Rect uv = ResolveSkinRegionUv(region->rectangle, m_snapshot.m_skinBinding.atlasWidth, m_snapshot.m_skinBinding.atlasHeight);
        emitQuad(rectangle, uv, tint, PaintMaterial::Skin);
    }
    return true;
}

DrawSnapshot PaintBuilder::freeze(){
    NWB_ASSERT(m_recording && m_clips.size() == 1u && m_overlayDepth == 0u);
    if(!m_commandsOrdered){
        Sort(m_snapshot.m_commands.begin(), m_snapshot.m_commands.end(), [](const DrawCommand& lhs, const DrawCommand& rhs)noexcept{
            return lhs.layer != rhs.layer ? lhs.layer < rhs.layer : lhs.firstIndex < rhs.firstIndex;
        });
    }
    m_recording = false;
    m_clips.clear();
    return Move(m_snapshot);
}

Rect PaintBuilder::currentClip()const noexcept{
    NWB_ASSERT(m_recording);
    return m_clips.back();
}

void PaintBuilder::emitQuad(const Rect& rectangle, const Rect& uv, const Color& color, const PaintMaterial::Enum material, const u32 glyphPageIndex, const u32 sdfPageIndex, const u32 sdfChannel, const u32 textureImageIndex){
    NWB_ASSERT(m_recording);
    NWB_ASSERT(IsFinite(color.r) && IsFinite(color.g) && IsFinite(color.b) && IsFinite(color.a));
    NWB_ASSERT(color.a >= 0.0f && color.a <= 1.0f);
    const Rect clip = m_clips.back();
    if(!HasUiRectArea(rectangle) || !HasUiRectArea(clip))
        return;
    const Rect visible = IntersectUiRectsBounds(rectangle, clip);
    if(!HasUiRectArea(visible) || color.a <= 0.0f)
        return;
    if(m_snapshot.m_vertices.size() > Limit<u32>::s_Max - s_PaintQuadVertexCount || m_snapshot.m_indices.size() > Limit<u32>::s_Max - s_PaintQuadIndexCount){
        NWB_ASSERT(false);
        return;
    }
    const Float4 textureCoordinates = ResolveQuadTextureCoords(rectangle, visible, uv);
    const f32 left = textureCoordinates.x;
    const f32 top = textureCoordinates.y;
    const f32 right = textureCoordinates.z;
    const f32 bottom = textureCoordinates.w;
    const Color premultiplied = PremultiplyPaintColor(color);
    const u32 vertexBase = static_cast<u32>(m_snapshot.m_vertices.size());
    const u32 firstIndex = static_cast<u32>(m_snapshot.m_indices.size());
    const f32 visibleRight = visible.x + visible.width;
    const f32 visibleBottom = visible.y + visible.height;
    m_snapshot.m_vertices.push_back({ { visible.x, visible.y }, { left, top }, premultiplied });
    m_snapshot.m_vertices.push_back({ { visibleRight, visible.y }, { right, top }, premultiplied });
    m_snapshot.m_vertices.push_back({ { visibleRight, visibleBottom }, { right, bottom }, premultiplied });
    m_snapshot.m_vertices.push_back({ { visible.x, visibleBottom }, { left, bottom }, premultiplied });
    const Array<u32, s_PaintQuadIndexCount> indices = { vertexBase, vertexBase + 1u, vertexBase + 2u, vertexBase, vertexBase + 2u, vertexBase + 3u };
    m_snapshot.m_indices.insert(m_snapshot.m_indices.end(), indices.begin(), indices.end());
    if(!m_snapshot.m_commands.empty()){
        auto& previous = m_snapshot.m_commands.back();
        if(
            previous.layer == m_layer && previous.material == material && previous.glyphPageIndex == glyphPageIndex
            && previous.sdfPageIndex == sdfPageIndex && previous.sdfChannel == sdfChannel
            && previous.textureImageIndex == textureImageIndex
            && EqualUiRect(previous.clip, clip)
        ){
            previous.indexCount += s_PaintQuadIndexCount;
            return;
        }
        // Distinct commands already have increasing firstIndex; only a layer descent needs ordering at freeze.
        if(previous.layer > m_layer)
            m_commandsOrdered = false;
    }
    DrawCommand command;
    command.firstIndex = firstIndex;
    command.indexCount = s_PaintQuadIndexCount;
    command.clip = clip;
    command.material = material;
    command.glyphPageIndex = glyphPageIndex;
    command.sdfPageIndex = sdfPageIndex;
    command.sdfChannel = sdfChannel;
    command.layer = m_layer;
    command.textureImageIndex = textureImageIndex;
    m_snapshot.m_commands.push_back(command);
}

void PaintBuilder::emitNineSlice(const UiSkinRegion& region, const Rect& rectangle, const Color& tint){
    if(!HasUiRectArea(rectangle))
        return;
    const auto& binding = m_snapshot.m_skinBinding;
    Float4 xVec;
    Float4 yVec;
    Float4 uVec;
    Float4 vVec;
    ResolveNineSliceCoordinates(region.rectangle, region.sliceInsets, rectangle, binding.referenceDensity, xVec, yVec, uVec, vVec);
    const Array<f32, s_PaintNineSliceAxisCells + 1u> x = { xVec.x, xVec.y, xVec.z, xVec.w };
    const Array<f32, s_PaintNineSliceAxisCells + 1u> y = { yVec.x, yVec.y, yVec.z, yVec.w };
    const Array<f32, s_PaintNineSliceAxisCells + 1u> u = { uVec.x, uVec.y, uVec.z, uVec.w };
    const Array<f32, s_PaintNineSliceAxisCells + 1u> v = { vVec.x, vVec.y, vVec.z, vVec.w };
    const f32 atlasWidth = static_cast<f32>(binding.atlasWidth);
    const f32 atlasHeight = static_cast<f32>(binding.atlasHeight);
    for(u32 row = 0u; row < s_PaintNineSliceAxisCells; ++row){
        for(u32 column = 0u; column < s_PaintNineSliceAxisCells; ++column){
            const Rect target{ x[column], y[row], x[column + 1u] - x[column], y[row + 1u] - y[row] };
            const Rect uv{ u[column] / atlasWidth, v[row] / atlasHeight, (u[column + 1u] - u[column]) / atlasWidth, (v[row + 1u] - v[row]) / atlasHeight };
            emitQuad(target, uv, tint, PaintMaterial::Skin);
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

