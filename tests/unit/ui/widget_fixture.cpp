// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "widget_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UiWidgetTests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Point Center(const Rect& bounds)noexcept{
    return { bounds.x + (bounds.width * 0.5f), bounds.y + (bounds.height * 0.5f) };
}

void ExpectRectExact(const Rect& actual, const Rect& expected){
    EXPECT_EQ(BitCast<u32>(actual.x), BitCast<u32>(expected.x));
    EXPECT_EQ(BitCast<u32>(actual.y), BitCast<u32>(expected.y));
    EXPECT_EQ(BitCast<u32>(actual.width), BitCast<u32>(expected.width));
    EXPECT_EQ(BitCast<u32>(actual.height), BitCast<u32>(expected.height));
}

void ExpectHitTargetIdentity(const HitTarget& actual, const HitTarget& expected){
    EXPECT_EQ(actual.id, expected.id);
    EXPECT_EQ(actual.declarationGeneration, expected.declarationGeneration);
    EXPECT_EQ(actual.control, expected.control);
    EXPECT_EQ(actual.owner, expected.owner);
    EXPECT_EQ(actual.enabled, expected.enabled);
}

PopupOptions NestedParentOptions()noexcept{
    PopupOptions options;
    options.anchor = { 20.0f, 20.0f, 80.0f, 20.0f };
    options.size = { 300.0f, 380.0f };
    return options;
}

PopupOptions NestedChildOptions()noexcept{
    PopupOptions options;
    options.anchor = { 420.0f, 60.0f, 80.0f, 24.0f };
    options.size = { 340.0f, 380.0f };
    return options;
}

PopupOptions LargeLoanParentOptions()noexcept{
    PopupOptions options;
    options.anchor = { 20.0f, 20.0f, 80.0f, 20.0f };
    options.size = { 340.0f, 420.0f };
    return options;
}

PopupOptions LargeLoanChildOptions()noexcept{
    PopupOptions options;
    options.anchor = { 420.0f, 20.0f, 80.0f, 20.0f };
    options.size = { 340.0f, 420.0f };
    return options;
}

NWB::Impl::Ui::ListOptions LargeLoanListOptions()noexcept{
    NWB::Impl::Ui::ListOptions options;
    options.height = { LayoutSizePolicy::Fixed, 140.0f };
    return options;
}

u64 ArithmeticFixtureRowKey(const u64 count, const u64 removed, const b8 reverse, const u64 index)noexcept{
    const u64 forward = reverse ? count - 1u - index : index;
    const u64 value = forward + 1u;
    return value + (removed != 0u && value >= removed ? 1u : 0u);
}

Expected<u64> FindArithmeticFixtureRowIndex(const u64 count, const u64 removed, const b8 reverse, const u64 value)noexcept{
    if(value == 0u || value == removed || value > count + (removed != 0u ? 1u : 0u))
        return MakeUnexpected(Failure{});
    const u64 forward = value - 1u - (removed != 0u && value > removed ? 1u : 0u);
    const u64 index = reverse ? count - 1u - forward : forward;
    return index < count ? Expected<u64>{ index } : MakeUnexpected(Failure{});
}

b8 LoadBundledFixtureFont(Core::Alloc::GlobalArena& arena, const StringView filename, Font& font){
    const ::Path<Core::Alloc::GlobalArena> path = ::Path<Core::Alloc::GlobalArena>(arena, NWB_TEST_FONT_DIRECTORY) / filename;
    Core::Assets::AssetBytes bytes(arena);
    auto bytesResult = Tests::ReadBundledFontBytes(path, bytes.get_allocator().arena());
    if(!bytesResult)
        return false;
    bytes = Move(*bytesResult);
    font.setFontBytes(Move(bytes));
    return font.validatePayload();
}

b8 InstallLatinFixtureFont(Core::Alloc::ScratchArena& scratchArena, Core::Alloc::GlobalArena& arena, Font& font, TextService& text){
    if(!LoadBundledFixtureFont(arena, "latin.font", font))
        return false;
    const FontSource source{ Core::Assets::AssetRef<Font>(font.virtualPath()), font, 1u };
    return text.setFonts(scratchArena, Span(&source, 1u));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ImageOptions WidgetFixture::Fixed(const f32 width, const f32 height)noexcept{
    ImageOptions options;
    options.width = { LayoutSizePolicy::Fixed, width };
    options.height = { LayoutSizePolicy::Fixed, height };
    return options;
}

WidgetFixture::WidgetFixture()
    : m_arena(Name("tests/ui/widgets"))
    , m_font(m_arena, Name(s_TestLatinFontPath))
    , m_text(m_arena)
    , m_skin(m_arena, Name(s_TestSkinPath))
    , m_paint(m_arena)
    , m_context(m_arena)
    , m_builder(m_arena, m_context, m_paint, m_text)
{}

void WidgetFixture::SetUp(){
    Core::Alloc::ScratchArena fontScratchArena(s_FontScratchArenaName);
    ASSERT_TRUE(UiWidgetTests::InstallLatinFixtureFont(fontScratchArena, m_arena, m_font, m_text));
    configureSkin();
    ASSERT_TRUE(m_skin.validatePayload());
    m_builder.setSkin(m_skin);
    // The synthetic skin's icon minimum determines title height independently of font line metrics.
    m_builder.style().fontSize = 14.0f;
}

void WidgetFixture::configureSkin(const AStringView omitted){
    UiSkin::RegionVector regions(m_arena);
    constexpr Array<AStringView, 11u> names = {
        "window.normal",
        "window.title",
        "window.collapse",
        "white",
        "separator",
        "panel.normal",
        "button.normal",
        "button.hover",
        "button.pressed",
        "button.disabled",
        "focus.overlay",
    };
    for(u32 index = 0u; index < static_cast<u32>(names.size()); ++index){
        if(names[index] == omitted)
            continue;
        UiSkinRegion region;
        const auto regionName = Name::FromText(names[index]);
        ASSERT_TRUE(regionName);
        region.name = *regionName;
        region.rectangle = { index * 8u, 0u, 8u, 8u };
        if(index == 0u || index == 5u)
            region.padding = { 3.0f, 4.0f, 5.0f, 6.0f };
        else if(index == 1u)
            region.padding = { 5.0f, 6.0f, 7.0f, 8.0f };
        else if(index == 4u){
            region.minimumWidth = 2.0f;
            region.minimumHeight = 3.0f;
        }
        else if(index >= 6u && index <= 9u){
            region.padding = { 2.0f, 3.0f, 4.0f, 5.0f };
            region.minimumWidth = 30.0f;
            region.minimumHeight = 24.0f;
        }
        regions.push_back(Move(region));
    }
    m_skin.setAtlas(Core::Assets::AssetRef<Texture>("tests/ui/texture"), 128u, 16u, 1.0f, Move(regions));
}

b8 WidgetFixture::begin(const u64 generation, const DisplayMetrics& display){
    if(!m_context.beginFrame(generation))
        return false;
    m_paint.begin(display, generation, m_skinGeneration, Core::Assets::AssetRef<UiSkin>(s_TestSkinPath), m_skin);
    return m_context.beginRoot(m_root);
}

InputRoutingResult WidgetFixture::send(const InputEvent& event){
    if(!m_context.input().queue(UiTestSupport::NativeInput(event))){
        ADD_FAILURE() << "input admission failed";
        return {};
    }
    return m_context.input().process();
}

void WidgetFixture::click(const Point& point){
    EXPECT_TRUE(send({ .position = point , .type = InputEventType::PrimaryDown }).pointerConsumed);
    EXPECT_TRUE(send({ .position = point , .type = InputEventType::PrimaryUp }).pointerConsumed);
}

void WidgetFixture::drag(const Point& origin, const Point& destination){
    EXPECT_TRUE(send({ .position = origin , .type = InputEventType::PrimaryDown }).pointerConsumed);
    EXPECT_TRUE(send({ .position = destination , .type = InputEventType::PointerMove }).pointerConsumed);
    EXPECT_TRUE(send({ .position = destination , .type = InputEventType::PrimaryUp }).pointerConsumed);
}

void WidgetFixture::press(const Core::Key::Enum value, const b8 repeat, const b8 shift){
    InputEvent event;
    event.type = InputEventType::KeyDown;
    event.key = value;
    event.repeat = repeat;
    event.shift = shift;
    EXPECT_TRUE(send(event).commandConsumed);
    if(!repeat){
        event.type = InputEventType::KeyUp;
        EXPECT_TRUE(send(event).commandConsumed);
    }
}

Expected<SkinPaintSample> WidgetFixture::sampleSkinPaint(const DrawSnapshot& snapshot, const Name& name)const noexcept{
    const UiSkinRegion* region = m_skin.findRegion(name);
    if(!region)
        return MakeUnexpected(Failure{});
    const f32 atlasWidth = static_cast<f32>(m_skin.atlasWidth());
    const f32 atlasHeight = static_cast<f32>(m_skin.atlasHeight());
    const SIMDVectorSingle edges = VectorSet(static_cast<f32>(region->rectangle.x), static_cast<f32>(region->rectangle.x + region->rectangle.width), static_cast<f32>(region->rectangle.y), static_cast<f32>(region->rectangle.y + region->rectangle.height));
    const SIMDVectorSingle texCoordEdges = VectorDivide(edges, VectorSet(atlasWidth, atlasWidth, atlasHeight, atlasHeight));
    const f32 leftU = VectorGetX(texCoordEdges);
    const f32 rightU = VectorGetY(texCoordEdges);
    const f32 topV = VectorGetZ(texCoordEdges);
    const f32 bottomV = VectorGetW(texCoordEdges);
    SkinPaintSample candidate;
    // Lanes are x, y, and maximum right U in z.
    SIMDVectorSingle origin = VectorZero();
    SIMDVectorSingle corner = VectorZero();
    for(const DrawCommand& command : snapshot.commands()){
        if(command.material != PaintMaterial::Skin)
            continue;
        for(u32 index = command.firstIndex; index + 5u < command.firstIndex + command.indexCount; index += 6u){
            const Vertex& first = snapshot.vertices()[snapshot.indices()[index]];
            const Vertex& opposite = snapshot.vertices()[snapshot.indices()[index + 2u]];
            if(first.texCoord.x < leftU || opposite.texCoord.x > rightU || first.texCoord.y < topV || opposite.texCoord.y > bottomV)
                continue;
            const SIMDVectorSingle firstPosition = VectorSet(first.position.x, first.position.y, 0.0f, 0.0f);
            const SIMDVectorSingle oppositeCorner = VectorSet(opposite.position.x, opposite.position.y, opposite.texCoord.x, 0.0f);
            if(candidate.quads == 0u){
                origin = firstPosition;
                corner = VectorSet(opposite.position.x, opposite.position.y, candidate.maximumU, 0.0f);
                candidate.color = first.color;
            }
            origin = VectorMin(origin, firstPosition);
            corner = VectorMax(corner, oppositeCorner);
            ++candidate.quads;
        }
    }
    if(candidate.quads == 0u)
        return MakeUnexpected(Failure{});
    const SIMDVectorSingle size = VectorSubtract(corner, origin);
    candidate.bounds = { VectorGetX(origin), VectorGetY(origin), VectorGetX(size), VectorGetY(size) };
    candidate.maximumU = VectorGetZ(corner);
    return candidate;
}

b8 WidgetFixture::installKoreanFallback(){
    Core::Alloc::ScratchArena fontScratchArena(s_FontScratchArenaName);
    Font korean(m_arena, Name(s_TestKoreanFontPath));
    if(!LoadBundledFixtureFont(m_arena, "korean.font", korean))
        return false;
    const Array<FontSource, 2u> sources = {
        FontSource{ Core::Assets::AssetRef<Font>(s_TestLatinFontPath), m_font, 1u },
        FontSource{ Core::Assets::AssetRef<Font>(s_TestKoreanFontPath), korean, 1u },
    };
    return m_text.setFonts(fontScratchArena, sources);
}

Expected<Rect> WidgetFixture::skinQuad(const DrawSnapshot& snapshot, const u32 slot)const noexcept{
    const f32 left = static_cast<f32>(slot) / 16.0f;
    const f32 right = static_cast<f32>(slot + 1u) / 16.0f;
    for(const DrawCommand& command : snapshot.commands()){
        if(command.material != PaintMaterial::Skin)
            continue;
        for(u32 index = command.firstIndex; index + 5u < command.firstIndex + command.indexCount; index += 6u){
            const Vertex& first = snapshot.vertices()[snapshot.indices()[index]];
            const Vertex& opposite = snapshot.vertices()[snapshot.indices()[index + 2u]];
            if(first.texCoord.x == left && opposite.texCoord.x == right)
                return Rect{ first.position.x, first.position.y, opposite.position.x - first.position.x, opposite.position.y - first.position.y };
        }
    }
    return MakeUnexpected(Failure{});
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

