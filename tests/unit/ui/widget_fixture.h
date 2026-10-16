// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <tests/unit/ui/native_input.h>

#include <impl/ecs_ui/toolkit/builder.h>

#include <tests/common/font_fixture.h>

#include <global/filesystem.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UiWidgetTests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Impl;
using namespace Impl::Ui;

[[nodiscard]] inline Point Center(const Rect& bounds)noexcept{
    return { bounds.x + bounds.width * 0.5f, bounds.y + bounds.height * 0.5f };
}

inline void ExpectRect(const Rect& actual, const Rect& expected){
    EXPECT_FLOAT_EQ(actual.x, expected.x);
    EXPECT_FLOAT_EQ(actual.y, expected.y);
    EXPECT_FLOAT_EQ(actual.width, expected.width);
    EXPECT_FLOAT_EQ(actual.height, expected.height);
}

inline void ExpectRectExact(const Rect& actual, const Rect& expected){
    EXPECT_EQ(BitCast<u32>(actual.x), BitCast<u32>(expected.x));
    EXPECT_EQ(BitCast<u32>(actual.y), BitCast<u32>(expected.y));
    EXPECT_EQ(BitCast<u32>(actual.width), BitCast<u32>(expected.width));
    EXPECT_EQ(BitCast<u32>(actual.height), BitCast<u32>(expected.height));
}


inline void ExpectHitTargetIdentity(const HitTarget& actual, const HitTarget& expected){
    EXPECT_EQ(actual.id, expected.id);
    EXPECT_EQ(actual.declarationGeneration, expected.declarationGeneration);
    EXPECT_EQ(actual.control, expected.control);
    EXPECT_EQ(actual.owner, expected.owner);
    EXPECT_EQ(actual.enabled, expected.enabled);
}

template<usize Capacity>
struct AcceptedInputState{
    Array<HitTarget, Capacity> targets{};
    usize count = 0u;
    u64 layoutGeneration = 0u;
    WidgetId capture;
};

template<usize Capacity, typename Input>
[[nodiscard]] AcceptedInputState<Capacity> CaptureAcceptedInput(const Input& input){
    AcceptedInputState<Capacity> saved;
    saved.count = input.targets().size();
    saved.layoutGeneration = input.layoutGeneration();
    saved.capture = input.capture();
    for(usize index = 0u; index < Min(saved.count, saved.targets.size()); ++index)
        saved.targets[index] = input.targets()[index];
    return saved;
}

template<usize Capacity, typename Input>
void ExpectAcceptedTargets(const Input& input, const Array<HitTarget, Capacity>& targets, const usize count, const bool checkOwnerDeclaration = false){
    ASSERT_EQ(input.targets().size(), count);
    ASSERT_LE(count, targets.size());
    for(usize index = 0u; index < count; ++index){
        const HitTarget& before = targets[index];
        const HitTarget& after = input.targets()[index];
        ExpectHitTargetIdentity(after, before);
        if(checkOwnerDeclaration)
            EXPECT_EQ(after.ownerDeclarationGeneration, before.ownerDeclarationGeneration);
        ExpectRect(after.rectangle, before.rectangle);
        ExpectRect(after.clip, before.clip);
    }
}

template<usize Capacity, typename Input>
void ExpectAcceptedInput(const Input& input, const AcceptedInputState<Capacity>& saved){
    EXPECT_EQ(input.layoutGeneration(), saved.layoutGeneration);
    EXPECT_EQ(input.capture(), saved.capture);
    ExpectAcceptedTargets(input, saved.targets, saved.count, true);
}

[[nodiscard]] inline PopupOptions NestedParentOptions(){
    PopupOptions options;
    options.anchor = { 20.0f, 20.0f, 80.0f, 20.0f };
    options.size = { 300.0f, 380.0f };
    return options;
}

[[nodiscard]] inline PopupOptions NestedChildOptions(){
    PopupOptions options;
    options.anchor = { 420.0f, 60.0f, 80.0f, 24.0f };
    options.size = { 340.0f, 380.0f };
    return options;
}

[[nodiscard]] inline PopupOptions LargeLoanParentOptions(){
    PopupOptions options;
    options.anchor = { 20.0f, 20.0f, 80.0f, 20.0f };
    options.size = { 340.0f, 420.0f };
    return options;
}

[[nodiscard]] inline PopupOptions LargeLoanChildOptions(){
    PopupOptions options;
    options.anchor = { 420.0f, 20.0f, 80.0f, 20.0f };
    options.size = { 340.0f, 420.0f };
    return options;
}

[[nodiscard]] inline NWB::Impl::Ui::ListOptions LargeLoanListOptions()noexcept{
    NWB::Impl::Ui::ListOptions options;
    options.height = { LayoutSizePolicy::Fixed, 140.0f };
    return options;
}

template<typename KeyLookup>
[[nodiscard]] Expected<u64> FindSingleDisabledFixtureRow(const u64 count, const u64 start, const bool backwards, const u64 disabled, KeyLookup&& rawKey)noexcept(noexcept(rawKey(start) != disabled)){
    u64 index = 0u;
    if(start >= count)
        return MakeUnexpected(Failure{});
    index = start;
    if(rawKey(index) != disabled)
        return index;
    if(backwards){
        if(index == 0u)
            return MakeUnexpected(Failure{});
        --index;
    }
    else{
        if(index + 1u == count)
            return MakeUnexpected(Failure{});
        ++index;
    }
    return index;
}

struct SkinPaintSample{
    Rect bounds;
    Color color;
    f32 maximumU = 0.0f;
    usize quads = 0u;
};

[[nodiscard]] bool InstallLatinFixtureFont(Core::Alloc::GlobalArena& arena, Font& font, TextService& text);

class WidgetFixture : public testing::Test{
public:
    WidgetFixture()
        : m_arena(Name("tests/ui/widgets"))
        , m_font(m_arena, Name("tests/ui/fonts/latin"))
        , m_text(m_arena)
        , m_skin(m_arena, Name("tests/ui/skin"))
        , m_paint(m_arena)
        , m_context(m_arena)
        , m_builder(m_arena, m_context, m_paint, m_text)
    {}


protected:
    virtual void SetUp()override{
        ASSERT_TRUE(UiWidgetTests::InstallLatinFixtureFont(m_arena, m_font, m_text));
        configureSkin();
        ASSERT_TRUE(m_skin.validatePayload());
        m_builder.setSkin(m_skin);
        // The synthetic skin's icon minimum determines title height independently of font line metrics.
        m_builder.style().fontSize = 14.0f;
    }

    void configureSkin(const AStringView omitted = {}){
        UiSkin::RegionVector regions(m_arena);
        const AStringView names[]{ "window.normal", "window.title", "window.collapse", "white", "separator", "panel.normal", "button.normal", "button.hover", "button.pressed", "button.disabled", "focus.overlay" };
        for(u32 index = 0u; index < sizeof(names) / sizeof(names[0u]); ++index){
            if(names[index] == omitted)
                continue;
            UiSkinRegion region;
            region.name = Name(names[index]);
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

    [[nodiscard]] bool begin(const u64 generation, const DisplayMetrics& display = { 800.0f, 600.0f, 1.0f, 1.0f }){
        if(!m_context.beginFrame(generation))
            return false;
        m_paint.begin(display, generation, m_skinGeneration, Core::Assets::AssetRef<UiSkin>("tests/ui/skin"), m_skin);
        return m_context.beginRoot(m_root);
    }

    [[nodiscard]] bool finishWindow(){
        return m_builder.endWindow() && m_context.endRoot() && m_context.finishFrame();
    }

    [[nodiscard]] bool finishPanel(){
        return m_builder.endPanel() && m_context.endRoot() && m_context.finishFrame();
    }

    [[nodiscard]] WidgetId id(const AStringView child, const AStringView parent = "window")const noexcept{
        return MakeWidgetId(MakeWidgetId(MakeRootId(m_root), parent), child);
    }

    [[nodiscard]] const HitTarget* target(const WidgetId widget)const noexcept{
        return UiTestSupport::FindHitTarget(m_context.input().targets(), widget);
    }

    [[nodiscard]] InputRoutingResult send(const InputEvent& event){
        if(!m_context.input().queue(UiTestSupport::NativeInput(event))){
            ADD_FAILURE() << "input admission failed";
            return {};
        }
        return m_context.input().process();
    }

    void click(const Point& point){
        EXPECT_TRUE(send({ InputEventType::PrimaryDown, point }).pointerConsumed);
        EXPECT_TRUE(send({ InputEventType::PrimaryUp, point }).pointerConsumed);
    }

    void drag(const Point& origin, const Point& destination){
        EXPECT_TRUE(send({ InputEventType::PrimaryDown, origin }).pointerConsumed);
        EXPECT_TRUE(send({ InputEventType::PointerMove, destination }).pointerConsumed);
        EXPECT_TRUE(send({ InputEventType::PrimaryUp, destination }).pointerConsumed);
    }

    void press(const Core::Key::Enum value, const bool repeat = false, const bool shift = false){
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

    [[nodiscard]] Expected<SkinPaintSample> sampleSkinPaint(const DrawSnapshot& snapshot, const Name& name)const noexcept;

    [[nodiscard]] bool installKoreanFallback();

    [[nodiscard]] Expected<Rect> skinQuad(const DrawSnapshot& snapshot, const u32 slot)const noexcept{
        const f32 left = static_cast<f32>(slot) / 16.0f;
        const f32 right = static_cast<f32>(slot + 1u) / 16.0f;
        for(const DrawCommand& command : snapshot.commands()){
            if(command.material != PaintMaterial::Skin)
                continue;
            for(u32 index = command.firstIndex; index + 5u < command.firstIndex + command.indexCount; index += 6u){
                const Vertex& first = snapshot.vertices()[snapshot.indices()[index]];
                const Vertex& opposite = snapshot.vertices()[snapshot.indices()[index + 2u]];
                if(first.texCoord.x == left && opposite.texCoord.x == right){
                    return Rect{ first.position.x, first.position.y, opposite.position.x - first.position.x, opposite.position.y - first.position.y };
                }
            }
        }
        return MakeUnexpected(Failure{});
    }


protected:
    Core::Alloc::GlobalArena m_arena;
    Font m_font;
    TextService m_text;
    UiSkin m_skin;
    PaintBuilder m_paint;
    Context m_context;
    Builder m_builder;
    WidgetRoot m_root{ 86u, 2u };
    u64 m_skinGeneration = 1u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

