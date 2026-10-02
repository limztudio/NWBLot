// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "texture_image_scene.h"

#include "../smoke_environment.h"

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_texture_image_scene{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr Core::Assets::AssetRef<Impl::Texture> s_DefaultTexture{"engine/ui/skins/default/texture"};
static constexpr Core::Assets::AssetRef<Impl::Texture> s_AlternateTexture{"project/ui/skins/alternate/texture"};
static constexpr Impl::Ui::Color s_Backdrop{ 0.07f, 0.13f, 0.2f, 1.0f };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Impl::Ui::Rect TextureUv(const f32 x, const f32 y, const f32 width = 24.0f){
    return { x / 256.0f, y / 256.0f, width / 256.0f, 24.0f / 256.0f };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UiTextureImageSmokeScene::UiTextureImageSmokeScene(
    Core::Alloc::GlobalArena& arena,
    Core::InputDispatcher& input,
    const Core::Assets::AssetManager& assets)
    : m_arena(arena)
    , m_input(input)
{
    using namespace __hidden_ui_texture_image_scene;
    m_default = Impl::Ui::LoadImageSource(arena, assets, s_DefaultTexture);
    m_alternate = Impl::Ui::LoadImageSource(arena, assets, s_AlternateTexture);
    m_replacement = m_default;
    m_sourcesValid = m_default && m_alternate && m_default->texture().width() == 256u
        && m_default->texture().height() == 256u && m_alternate->texture().width() == 256u
        && m_alternate->texture().height() == 256u && m_default->generation() != m_alternate->generation();
    m_input.addHandlerToBack(*this);
}

UiTextureImageSmokeScene::~UiTextureImageSmokeScene(){
    m_input.removeHandler(*this);
}

bool UiTextureImageSmokeScene::paint(Impl::UiPaintContext& context){
    if(!m_sourcesValid)
        return false;
    if(m_evictionRemaining != 0u){
        if(!replaceVersion()){
            m_sourcesValid = false;
            return false;
        }
        --m_evictionRemaining;
        ++m_evictionCompleted;
    }
    auto& ui = context.ui;
    ui.style().fontSize = 14.0f;
    ui.style().gap = 8.0f;
    m_declared = {};
    const f32 width = Max(300.0f, context.display.logicalWidth * 0.5f - 40.0f);
    const f32 right = 24.0f + width + 32.0f;
    const f32 otherWidth = Max(220.0f, context.display.logicalWidth - right - 24.0f);
    if(
        !paintControls(ui, { 24.0f, 24.0f, width, 112.0f })
        || !paintImages(context, width, right, otherWidth)
        || !paintBuilderImages(ui, { right, 24.0f, otherWidth, 112.0f }) || !paintPopups(context, right) || ui.failed()
    )
        return false;
    observeState(context);
    paintMarkers(context);
    return true;
}

bool UiTextureImageSmokeScene::keyboardUpdate(const i32 key, const i32 scancode, const i32 action, const i32 mods){
    static_cast<void>(scancode);
    static_cast<void>(mods);
    if(key != Core::Key::F4 && key != Core::Key::F5 && key != Core::Key::F6 && key != Core::Key::F7)
        return false;
    if(action != Core::InputAction::Press)
        return true;
    if(key == Core::Key::F4)
        m_phase = !m_phase;
    else if(key == Core::Key::F6)
        m_sourcesValid = replaceVersion();
    else if(key == Core::Key::F7){
        m_evictionRemaining = 72u;
        m_evictionCompleted = 0u;
    }
    else if(m_parent.isOpen()){
        m_child.close();
        m_parent.close();
    }
    else{
        m_parent.open();
        m_child.open();
    }
    return true;
}

bool UiTextureImageSmokeScene::replaceVersion(){
    if(!m_default || !m_alternate || !m_replacement)
        return false;
    const Impl::Texture& payload = m_replacementAlternate ? m_default->texture() : m_alternate->texture();
    Impl::Texture replacement(m_arena, m_default->identity().name());
    Impl::Texture::MipLevelVector mipLevels(m_arena);
    mipLevels.assign(payload.mipLevels().begin(), payload.mipLevels().end());
    Core::Assets::AssetBytes bytes(m_arena);
    bytes.assign(payload.payloadBytes().begin(), payload.payloadBytes().end());
    replacement.setPayload(
        payload.colorSpace(), payload.hasAlpha(), payload.width(), payload.height(), Move(mipLevels), Move(bytes),
        payload.dimension(), payload.depth(), payload.payloadFormat(), payload.alphaMode(), payload.alphaConstantUnorm8()
    );
    Impl::Ui::SharedImageSource next = Impl::Ui::MakeImageSource(m_arena, replacement);
    if(
        !next || next->generation() == m_replacement->generation()
        || next->generation() == m_default->generation() || next->generation() == m_alternate->generation()
        || next->identity().name() != m_default->identity().name()
    )
        return false;
    m_replacement = Move(next);
    ++m_replacementCount;
    m_replacementAlternate = !m_replacementAlternate;
    return true;
}

bool UiTextureImageSmokeScene::paintControls(Impl::Ui::Builder& ui, const Impl::Ui::Rect& bounds){
    using namespace Impl::Ui;
    if(!ui.beginPanel("texture_image_controls", bounds))
        return false;
    const WidgetOptions caption{ {}, { LayoutSizePolicy::Fixed, 16.0f } };
    const WidgetOptions button{ { LayoutSizePolicy::Stretch, 1.0f }, { LayoutSizePolicy::Fixed, 28.0f } };
    if(!ui.label("title", "Owned engine textures / mixed images and skin", caption))
        return false;
    if(ui.button("before", "Before passive engine images", button))
        ++m_beforeClicks;
    if(ui.button("after", "After passive engine images", button))
        ++m_afterClicks;
    return ui.endPanel();
}

bool UiTextureImageSmokeScene::paintImages(
    Impl::UiPaintContext& context,
    const f32 width,
    const f32 right,
    const f32 otherWidth){
    using namespace Impl::Ui;
    using namespace __hidden_ui_texture_image_scene;
    auto& paint = context.paint;
    paint.fillRect({ 24.0f, 152.0f, width, 288.0f }, s_Backdrop);
    paint.fillRect({ right, 152.0f, otherWidth, 288.0f }, s_Backdrop);
    auto& rectangles = m_declared.rectangles;
    rectangles[2u] = { 32.0f, 168.0f, 96.0f, 64.0f };
    rectangles[3u] = { 140.0f, 168.0f, 96.0f, 64.0f };
    rectangles[4u] = { 248.0f, 168.0f, 96.0f, 64.0f };
    rectangles[5u] = { 32.0f, 240.0f, 96.0f, 32.0f };
    rectangles[6u] = { right + 8.0f, 168.0f, otherWidth - 16.0f, 64.0f };
    rectangles[7u] = { right + 8.0f, 240.0f, otherWidth - 16.0f, 32.0f };
    rectangles[8u] = { right + 8.0f, 240.0f, (otherWidth - 16.0f) * 0.5f, 32.0f };
    rectangles[9u] = { 32.0f, 280.0f, 256.0f, 128.0f };
    rectangles[10u] = { right + 8.0f, 280.0f, 256.0f, 128.0f };
    rectangles[11u] = { right + 8.0f, 416.0f, otherWidth - 16.0f, 24.0f };
    if(
        !paint.drawImage(m_default, rectangles[2u], TextureUv(4.0f, 4.0f))
        || !paint.drawImage(m_alternate, rectangles[3u], TextureUv(196.0f, 100.0f))
        || !paint.drawImage(m_default, rectangles[4u], TextureUv(4.0f, 132.0f), { 0.4f, 0.7f, 0.9f, 0.5f })
        || !paint.drawImage(m_default, rectangles[5u], TextureUv(4.0f, 4.0f), { 1.0f, 1.0f, 1.0f, 0.0f })
    )
        return false;
    SharedImageSource declaredSource = m_phase ? m_alternate : m_replacement;
    Rect rectangle = rectangles[6u];
    Rect uv = m_phase ? TextureUv(196.0f, 100.0f) : TextureUv(4.0f, 4.0f);
    Color tint = m_phase ? Color{ 0.5f, 0.75f, 1.0f, 0.5f } : Color{};
    m_declared.values[8u] = m_phase ? 2u : 1u;
    if(!paint.drawImage(declaredSource, rectangle, uv, tint))
        return false;
    declaredSource = m_phase ? m_default : m_alternate;
    declaredSource.reset();
    rectangle = { 0.0f, 0.0f, 1.0f, 1.0f };
    uv = { 0.0f, 0.0f, 1.0f, 1.0f };
    tint = { 0.0f, 0.0f, 0.0f, 1.0f };
    m_declared.values[9u] = static_cast<u64>(!declaredSource);
    paint.pushClip(rectangles[8u]);
    const bool clipped = paint.drawImage(m_default, rectangles[7u], TextureUv(16.0f, 4.0f, 12.0f));
    const bool popped = paint.popClip();
    if(!clipped || !popped)
        return false;
    return paint.drawImage(m_default, rectangles[9u]) && paint.drawImage(m_alternate, rectangles[10u])
        && paint.drawRegion(Name("progress.fill"), rectangles[11u]);
}

bool UiTextureImageSmokeScene::paintBuilderImages(Impl::Ui::Builder& ui, const Impl::Ui::Rect& bounds){
    using namespace Impl::Ui;
    if(!ui.beginPanel("texture_image_builder", bounds))
        return false;
    ImageOptions options;
    options.width = { LayoutSizePolicy::Stretch, 1.0f };
    options.height = { LayoutSizePolicy::Fixed, 80.0f };
    options.tint = m_phase ? Color{ 0.5f, 0.75f, 1.0f, 0.5f } : Color{};
    SharedImageSource source = m_phase ? m_alternate : m_replacement;
    m_declared.rectangles[16u] = { bounds.x + 8.0f, bounds.y + 8.0f, bounds.width - 16.0f, 80.0f };
    m_declared.values[14u] = m_phase ? 2u : 1u;
    if(!ui.image("owned_source", source, options))
        return false;
    source = m_phase ? m_default : m_alternate;
    source.reset();
    options.width = { LayoutSizePolicy::Fixed, 1.0f };
    options.height = { LayoutSizePolicy::Fixed, 1.0f };
    options.tint = { 0.0f, 0.0f, 0.0f, 1.0f };
    m_declared.values[15u] = static_cast<u64>(!source);
    return ui.endPanel();
}

bool UiTextureImageSmokeScene::paintPopups(Impl::UiPaintContext& context, const f32 right){
    using namespace Impl::Ui;
    auto& ui = context.ui;
    PopupOptions parent;
    parent.anchor = { right, 300.0f, 8.0f, 24.0f };
    parent.size = { 280.0f, 128.0f };
    if(!ui.beginPopup("texture_image_parent", m_parent, parent))
        return !ui.failed();
    const WidgetOptions button{ { LayoutSizePolicy::Stretch, 1.0f }, { LayoutSizePolicy::Fixed, 28.0f } };
    if(ui.button("parent_action", "Parent action", button))
        m_parent.close();
    ImageOptions options;
    options.width = { LayoutSizePolicy::Stretch, 1.0f };
    options.height = { LayoutSizePolicy::Fixed, 64.0f };
    SharedImageSource source = m_alternate;
    if(!ui.image("parent_source", source, options))
        return false;
    source.reset();
    PopupOptions child;
    child.anchor = { right + 100.0f, 370.0f, 8.0f, 16.0f };
    child.size = { 240.0f, 128.0f };
    if(ui.beginPopup("texture_image_child", m_child, child)){
        source = m_default;
        options.tint = { 0.5f, 1.0f, 0.5f, 0.75f };
        if(!ui.image("child_source", source, options))
            return false;
        source.reset();
        if(ui.button("child_action", "Child action", button))
            m_child.close();
        if(!ui.endPopup())
            return false;
    }
    options.width = { LayoutSizePolicy::Fixed, 1.0f };
    options.height = { LayoutSizePolicy::Fixed, 1.0f };
    options.tint = { 0.0f, 0.0f, 0.0f, 1.0f };
    if(ui.failed() || !ui.endPopup())
        return false;
    m_declared.rectangles[14u] = m_parent.placement().bounds;
    const Rect& parentBounds = m_declared.rectangles[14u];
    m_declared.rectangles[12u] = { parentBounds.x + 8.0f, parentBounds.y + 44.0f, parentBounds.width - 16.0f, 64.0f };
    if(m_child.isOpen()){
        m_declared.rectangles[15u] = m_child.placement().bounds;
        const Rect& childBounds = m_declared.rectangles[15u];
        m_declared.rectangles[13u] = { childBounds.x + 8.0f, childBounds.y + 8.0f, childBounds.width - 16.0f, 64.0f };
    }
    // Both late base commands must remain beneath the actual deferred Builder popup chrome and source images.
    context.paint.fillRect(m_declared.rectangles[12u], { 0.8f, 0.0f, 0.0f, 1.0f });
    if(m_child.isOpen())
        context.paint.fillRect(m_declared.rectangles[13u], { 0.0f, 0.0f, 0.8f, 1.0f });
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool IsUiLayerTextureImageSmokeEnabled(){
    return ReadSmokeEnvironmentFlag("NWB_UI_LAYER_TEXTURE_IMAGE");
}

bool IsUiLayerTextureImageSkinSmokeEnabled(){
    return ReadSmokeEnvironmentFlag("NWB_UI_LAYER_TEXTURE_IMAGE_SKIN");
}

SharedUiTextureImageSmokeScene CreateUiTextureImageSmokeScene(
    Core::Alloc::GlobalArena& arena,
    Core::InputDispatcher& input,
    const Core::Assets::AssetManager& assets,
    Core::GraphicsRuntime& graphics){
    static_cast<void>(graphics);
    return SharedUiTextureImageSmokeScene(
        NewArenaObject<RefCounter<UiTextureImageSmokeScene>>(arena, arena, input, assets),
        ArenaRefDeleter<RefCounter<UiTextureImageSmokeScene>, Core::Alloc::GlobalArena>(&arena),
        AdoptRef
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

