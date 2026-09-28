// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

#include <impl/assets_ui_skin/asset.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct Point{
    f32 x = 0.0f;
    f32 y = 0.0f;
};

struct Rect{
    f32 x = 0.0f;
    f32 y = 0.0f;
    f32 width = 0.0f;
    f32 height = 0.0f;
};

struct Insets{
    f32 left = 0.0f;
    f32 top = 0.0f;
    f32 right = 0.0f;
    f32 bottom = 0.0f;
};

// Input colors use linear RGB and straight alpha; frozen vertices use premultiplied alpha.
struct Color{
    f32 r = 1.0f;
    f32 g = 1.0f;
    f32 b = 1.0f;
    f32 a = 1.0f;
};

struct Vertex{
    Point position;
    Point texCoord;
    Color color;
};

struct DisplayMetrics{
    f32 logicalWidth = 0.0f;
    f32 logicalHeight = 0.0f;
    f32 pixelScaleX = 1.0f;
    f32 pixelScaleY = 1.0f;
};

namespace PaintMaterial{
    enum Enum : u8{ Solid, Skin };
};

struct DrawCommand{
    u32 firstIndex = 0u;
    u32 indexCount = 0u;
    Rect clip;
    PaintMaterial::Enum material = PaintMaterial::Solid;
};

// Typed references identify assets; this copy captures atlas metadata, not loaded/GPU resource ownership.
struct SkinBinding{
    Core::Assets::AssetRef<UiSkin> skin;
    Core::Assets::AssetRef<Texture> texture;
    u64 generation = 0u;
    u32 atlasWidth = 0u;
    u32 atlasHeight = 0u;
    f32 referenceDensity = 1.0f;
};

template<typename T>
using PaintVector = Vector<T, Core::Alloc::GlobalArena>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The caller's arena must outlive the snapshot and every consumer of it.
class DrawSnapshot final{
    friend class PaintBuilder;


public:
    explicit DrawSnapshot(Core::Alloc::GlobalArena& arena);
    DrawSnapshot(DrawSnapshot&&) = default;
    DrawSnapshot& operator=(DrawSnapshot&&) = default;


public:
    DrawSnapshot(const DrawSnapshot&) = delete;
    DrawSnapshot& operator=(const DrawSnapshot&) = delete;


public:
    [[nodiscard]] u64 generation()const{ return m_generation; }
    [[nodiscard]] const DisplayMetrics& displayMetrics()const{ return m_displayMetrics; }
    [[nodiscard]] const SkinBinding& skinBinding()const{ return m_skinBinding; }
    [[nodiscard]] const PaintVector<Vertex>& vertices()const{ return m_vertices; }
    [[nodiscard]] const PaintVector<u32>& indices()const{ return m_indices; }
    [[nodiscard]] const PaintVector<DrawCommand>& commands()const{ return m_commands; }


private:
    u64 m_generation = 0u;
    DisplayMetrics m_displayMetrics;
    SkinBinding m_skinBinding;
    PaintVector<Vertex> m_vertices;
    PaintVector<u32> m_indices;
    PaintVector<DrawCommand> m_commands;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class PaintBuilder final : NoCopy{
public:
    explicit PaintBuilder(Core::Alloc::GlobalArena& arena);


public:
    // Skin payload validation belongs to loading; begin captures its descriptor for this frame.
    void begin(
        const DisplayMetrics& metrics,
        u64 generation,
        u64 skinGeneration,
        const Core::Assets::AssetRef<UiSkin>& skinRef,
        const UiSkin& skin
    );
    void reserve(usize quadCount, usize clipDepth = 8u);
    void pushClip(const Rect& clip);
    [[nodiscard]] bool popClip();
    void fillRect(const Rect& rectangle, const Color& color = {});
    [[nodiscard]] bool drawRegion(const Name& regionName, const Rect& rectangle, const Color& tint = {});
    [[nodiscard]] DrawSnapshot freeze();


private:
    void emitQuad(const Rect& rectangle, const Rect& uv, const Color& color, PaintMaterial::Enum material);
    void emitNineSlice(const UiSkinRegion& region, const Rect& rectangle, const Color& tint);


private:
    DrawSnapshot m_snapshot;
    PaintVector<UiSkinRegion> m_regions;
    PaintVector<Rect> m_clips;
    bool m_recording = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

