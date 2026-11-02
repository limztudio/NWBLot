// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <tests/unit/ui/native_input.h>
#include "paint_assertions.h"
#include "widget_id_assertion.h"

#include <impl/ecs_ui/toolkit/builder.h>

#include <tests/common/font_fixture.h>

#include <global/filesystem.h>
#include <global/simdmath.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UiWidgetTests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Impl;
using namespace Impl::Ui;

inline constexpr AStringView s_TestSkinPath = "tests/ui/skin";
inline constexpr AStringView s_TestLatinFontPath = "tests/ui/fonts/latin";
inline constexpr AStringView s_TestKoreanFontPath = "tests/ui/fonts/korean";
inline constexpr Name s_FontScratchArenaName("tests/ui/fonts/operation_scratch");

[[nodiscard]] Point Center(const Rect& bounds)noexcept;
void ExpectRectExact(const Rect& actual, const Rect& expected);
void ExpectHitTargetIdentity(const HitTarget& actual, const HitTarget& expected);

template<usize Capacity>
struct AcceptedInputState{
    Array<HitTarget, Capacity> targets{};
    usize count = 0u;
    usize popupCount = 0u;
    u64 layoutGeneration = 0u;
    WidgetId focus;
    WidgetId capture;
};

template<usize Capacity, typename Input>
[[nodiscard]] AcceptedInputState<Capacity> CaptureAcceptedInput(const Input& input)noexcept(
    noexcept(DeclVal<usize&>() = input.targets().size())
    && noexcept(DeclVal<usize&>() = input.popupCount())
    && noexcept(DeclVal<u64&>() = input.layoutGeneration())
    && noexcept(DeclVal<WidgetId&>() = input.focus())
    && noexcept(DeclVal<WidgetId&>() = input.capture())
    && noexcept(DeclVal<HitTarget&>() = input.targets()[DeclVal<usize&>()])
){
    AcceptedInputState<Capacity> saved;
    saved.count = input.targets().size();
    saved.popupCount = input.popupCount();
    saved.layoutGeneration = input.layoutGeneration();
    saved.focus = input.focus();
    saved.capture = input.capture();
    for(usize index = 0u; index < Min(saved.count, saved.targets.size()); ++index)
        saved.targets[index] = input.targets()[index];
    return saved;
}

template<typename Input>
void ExpectAcceptedTargets(const Input& input, const Span<const HitTarget> targets, const usize count, const b8 checkOwnerDeclaration = false){
    ASSERT_EQ(input.targets().size(), count);
    ASSERT_LE(count, targets.size());
    for(usize index = 0u; index < count; ++index){
        const HitTarget& before = targets[index];
        const HitTarget& after = input.targets()[index];
        ExpectHitTargetIdentity(after, before);
        if(checkOwnerDeclaration)
            EXPECT_EQ(after.ownerDeclarationGeneration, before.ownerDeclarationGeneration);
        EXPECT_EQ(after.popup, before.popup);
        UiPaintAssertions::ExpectRect(after.rectangle, before.rectangle);
        UiPaintAssertions::ExpectRect(after.clip, before.clip);
    }
}

template<usize Capacity, typename Input>
void ExpectAcceptedInput(
    const Input& input,
    const AcceptedInputState<Capacity>& saved,
    const b8 checkOwnerDeclaration = true,
    const b8 checkFocus = false,
    const b8 checkPopupCount = false,
    const b8 checkCapture = true
){
    EXPECT_EQ(input.layoutGeneration(), saved.layoutGeneration);
    if(checkPopupCount)
        EXPECT_EQ(input.popupCount(), saved.popupCount);
    if(checkFocus)
        EXPECT_EQ(input.focus(), saved.focus);
    if(checkCapture)
        EXPECT_EQ(input.capture(), saved.capture);
    ExpectAcceptedTargets(input, saved.targets, saved.count, checkOwnerDeclaration);
}

template<typename VertexVector>
void CopyGlyphVertices(const DrawSnapshot& snapshot, VertexVector& vertices){
    vertices.reserve(snapshot.indices().size());
    for(const DrawCommand& command : snapshot.commands()){
        if(command.material != PaintMaterial::Glyph && command.material != PaintMaterial::SdfGlyph)
            continue;
        for(u32 index = command.firstIndex; index < command.firstIndex + command.indexCount; ++index)
            vertices.push_back(snapshot.vertices()[snapshot.indices()[index]]);
    }
}

inline constexpr AStringView s_ParentPopupKey = "parent";
inline constexpr AStringView s_ChildPopupKey = "child";

[[nodiscard]] PopupOptions NestedParentOptions()noexcept;
[[nodiscard]] PopupOptions NestedChildOptions()noexcept;
[[nodiscard]] PopupOptions LargeLoanParentOptions()noexcept;
[[nodiscard]] PopupOptions LargeLoanChildOptions()noexcept;
[[nodiscard]] NWB::Impl::Ui::ListOptions LargeLoanListOptions()noexcept;
[[nodiscard]] u64 ArithmeticFixtureRowKey(u64 count, u64 removed, b8 reverse, u64 index)noexcept;
[[nodiscard]] Expected<u64> FindArithmeticFixtureRowIndex(u64 count, u64 removed, b8 reverse, u64 value)noexcept;

template<typename KeyLookup>
[[nodiscard]] Expected<u64> FindSingleDisabledFixtureRow(const u64 count, const u64 start, const b8 backwards, const u64 disabled, KeyLookup&& rawKey)noexcept(noexcept(static_cast<b8>(rawKey(DeclVal<u64&>()) != disabled))){
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
    usize quads = 0u;
    Rect bounds;
    Color color;
    f32 maximumU = 0.0f;
};

[[nodiscard]] b8 LoadBundledFixtureFont(Core::Alloc::GlobalArena& arena, StringView filename, Font& font);
[[nodiscard]] b8 InstallLatinFixtureFont(Core::Alloc::ScratchArena& scratchArena, Core::Alloc::GlobalArena& arena, Font& font, TextService& text);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class WidgetFixture : public testing::Test{
protected:
    [[nodiscard]] static ImageOptions Fixed(f32 width = 96.0f, f32 height = 40.0f)noexcept;


public:
    WidgetFixture();


protected:
    virtual void SetUp()override;
    void configureSkin(AStringView omitted = {});
    [[nodiscard]] b8 begin(u64 generation, const DisplayMetrics& display = { 800.0f, 600.0f, 1.0f, 1.0f });
    [[nodiscard]] b8 finishWindow(){ return m_builder.endWindow() && m_context.endRoot() && m_context.finishFrame(); }
    [[nodiscard]] b8 panel(const u64 generation, const AStringView key = "panel", const Rect& bounds = { 10.0f, 10.0f, 360.0f, 400.0f }){ return begin(generation) && m_builder.beginPanel(key, bounds); }
    [[nodiscard]] b8 finishRoot(){ return m_context.endRoot() && m_context.finishFrame(); }
    [[nodiscard]] b8 finishPanel(){ return m_builder.endPanel() && finishRoot(); }
    [[nodiscard]] WidgetId parentId()const{ return UiTestSupport::RequireWidgetId(UiTestSupport::RequireRootId(m_root), s_ParentPopupKey); }
    [[nodiscard]] WidgetId childId()const{ return UiTestSupport::RequireWidgetId(parentId(), s_ChildPopupKey); }
    [[nodiscard]] WidgetId id(const AStringView child, const AStringView parent = "window")const{ return UiTestSupport::RequireWidgetId(UiTestSupport::RequireWidgetId(UiTestSupport::RequireRootId(m_root), parent), child); }
    [[nodiscard]] const HitTarget* target(const WidgetId widget)const noexcept{ return UiTestSupport::FindHitTarget(m_context.input().targets(), widget); }
    [[nodiscard]] InputRoutingResult send(const InputEvent& event);
    void click(const Point& point);
    void drag(const Point& origin, const Point& destination);
    void press(Core::Key::Enum value, b8 repeat = false, b8 shift = false);
    [[nodiscard]] Expected<SkinPaintSample> sampleSkinPaint(const DrawSnapshot& snapshot, const Name& name)const noexcept;
    [[nodiscard]] b8 installKoreanFallback();
    [[nodiscard]] Expected<Rect> skinQuad(const DrawSnapshot& snapshot, u32 slot)const noexcept;


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

