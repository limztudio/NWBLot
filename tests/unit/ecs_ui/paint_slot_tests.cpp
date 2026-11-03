// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "edit_host_fixture_support.h"

#include <impl/ecs_ui/components.adhd.h>
#include <impl/ecs_ui/paint_context.h>
#include <impl/ecs_ui/toolkit/context.h>

#include <core/ecs/entity.h>
#include <core/task/cpu/scheduler.h>

#include <tests/common/test_context.h>

#include <global/optional.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_paint_slot_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Core;
using namespace NWB::Impl;


class PaintSlotFixture{
public:
    Tests::TestArena<> arena;
    CpuTaskScheduler scheduler{0u};
    ECS::World world{arena.arena, scheduler};
    UiEditHostFixtureSupport::FakeClipboard clipboard{arena.arena};
    UiEditHostFixtureSupport::FakeTextInput textInput{arena.arena};
    Ui::Context uiContext{arena.arena};
    Ui::PaintBuilder paint{arena.arena};
    Ui::TextService text{arena.arena};
    Ui::DisplayMetrics display{};
    Ui::Builder builder{arena.arena, uiContext, paint, text};
    UiPaintContext context{world, clipboard, textInput, paint, text, display, builder};
};


TEST(PaintSlot, ClearAndRemoveReaddDoNotRestoreTheRunningCallback){
    PaintSlotFixture fixture;
    auto& world = fixture.world;
    auto& context = fixture.context;
    auto entity = world.createEntity();
    const ECS::EntityID id = entity.id();
    auto& component = entity.addComponent<UiPaintComponent>();
    component.paint = [id](UiPaintContext& ctx){ ctx.world.entity(id).getComponent<UiPaintComponent>().paint.reset(); };
    component.paint.invoke(context);
    EXPECT_FALSE(static_cast<b8>(entity.getComponent<UiPaintComponent>().paint));

    entity.getComponent<UiPaintComponent>().paint = [id](UiPaintContext& ctx){
        auto current = ctx.world.entity(id);
        current.removeComponent<UiPaintComponent>();
        current.addComponent<UiPaintComponent>();
    };
    entity.getComponent<UiPaintComponent>().paint.invoke(context);
    EXPECT_FALSE(static_cast<b8>(entity.getComponent<UiPaintComponent>().paint));

    entity.getComponent<UiPaintComponent>().paint = [id](UiPaintContext& ctx){
        auto current = ctx.world.entity(id);
        current.removeComponent<UiPaintComponent>();
        current.addComponent<UiPaintComponent>();
        throw 42;
    };
    EXPECT_THROW(entity.getComponent<UiPaintComponent>().paint.invoke(context), i32);
    EXPECT_FALSE(static_cast<b8>(entity.getComponent<UiPaintComponent>().paint));
}

TEST(PaintSlot, ReplacementAndRelocationKeepTheCurrentOwner){
    PaintSlotFixture fixture;
    auto& world = fixture.world;
    auto& context = fixture.context;
    auto first = world.createEntity();
    first.addComponent<UiPaintComponent>();
    auto second = world.createEntity();
    const ECS::EntityID firstId = first.id();
    const ECS::EntityID secondId = second.id();
    i32 observed = 0;
    second.addComponent<UiPaintComponent>().paint = [firstId, &observed, count = 0](UiPaintContext& ctx)mutable{
        observed = ++count;
        if(count == 1)
            ctx.world.entity(firstId).removeComponent<UiPaintComponent>();
    };
    second.getComponent<UiPaintComponent>().paint.invoke(context);
    world.entity(secondId).getComponent<UiPaintComponent>().paint.invoke(context);
    EXPECT_EQ(observed, 2);

    world.entity(secondId).getComponent<UiPaintComponent>().paint = [secondId, &observed](UiPaintContext& ctx){
        ctx.world.entity(secondId).getComponent<UiPaintComponent>().paint = [&observed](UiPaintContext&){ observed = 17; };
    };
    world.entity(secondId).getComponent<UiPaintComponent>().paint.invoke(context);
    world.entity(secondId).getComponent<UiPaintComponent>().paint.invoke(context);
    EXPECT_EQ(observed, 17);
}

TEST(PaintSlot, VectorGrowthAndExplicitMovePreserveMutableCaptures){
    PaintSlotFixture fixture;
    auto& world = fixture.world;
    auto& context = fixture.context;
    auto entity = world.createEntity();
    const ECS::EntityID id = entity.id();
    i32 observed = 0;
    entity.addComponent<UiPaintComponent>().paint = [&observed, count = 0](UiPaintContext& ctx)mutable{
        observed = ++count;
        if(count == 1){
            for(u32 i = 0u; i < 128u; ++i)
                ctx.world.createEntity().addComponent<UiPaintComponent>();
        }
    };
    entity.getComponent<UiPaintComponent>().paint.invoke(context);
    world.entity(id).getComponent<UiPaintComponent>().paint.invoke(context);
    EXPECT_EQ(observed, 2);

    UiPaintSlot first;
    UiPaintSlot second;
    first = [&first, &second, &observed, count = 0](UiPaintContext&)mutable{
        observed = ++count;
        if(count == 1)
            second = Move(first);
    };
    first.invoke(context);
    EXPECT_FALSE(static_cast<b8>(first));
    ASSERT_TRUE(static_cast<b8>(second));
    second.invoke(context);
    EXPECT_EQ(observed, 2);
}

TEST(PaintSlot, ExceptionRestoresCallbackWhenItStillHasAnOwner){
    PaintSlotFixture fixture;
    auto& context = fixture.context;
    UiPaintSlot slot;
    i32 calls = 0;
    slot = [&calls](UiPaintContext&){ ++calls; throw 42; };
    EXPECT_THROW(slot.invoke(context), i32);
    ASSERT_TRUE(static_cast<b8>(slot));
    EXPECT_THROW(slot.invoke(context), i32);
    EXPECT_EQ(calls, 2);
}

TEST(PaintSlot, NestedReplacementAndExceptionDoNotRestoreTheDisplacedCallback){
    PaintSlotFixture fixture;
    auto& context = fixture.context;
    UiPaintSlot slot;
    i32 calls = 0;
    slot = [&slot, &calls](UiPaintContext& ctx){
        slot = [&calls](UiPaintContext&){ ++calls; };
        slot.invoke(ctx);
        throw 42;
    };
    EXPECT_THROW(slot.invoke(context), i32);
    ASSERT_TRUE(static_cast<b8>(slot));
    slot.invoke(context);
    EXPECT_EQ(calls, 2);

    slot = [&slot](UiPaintContext&){ slot.reset(); throw 42; };
    EXPECT_THROW(slot.invoke(context), i32);
    EXPECT_FALSE(static_cast<b8>(slot));
}

TEST(PaintSlot, RunningCallbackRemainsVisibleUntilCleared){
    PaintSlotFixture fixture;
    UiPaintSlot slot;
    b8 visibleDuringCall = false;
    b8 visibleAfterClear = true;
    b8 callableDuringCall = true;
    slot = [&slot, &visibleDuringCall, &visibleAfterClear, &callableDuringCall](UiPaintContext& ctx){
        visibleDuringCall = slot.present();
        callableDuringCall = static_cast<b8>(slot);
        if(slot)
            slot.invoke(ctx);
        slot.reset();
        visibleAfterClear = slot.present();
    };
    slot.invoke(fixture.context);
    EXPECT_TRUE(visibleDuringCall);
    EXPECT_FALSE(callableDuringCall);
    EXPECT_FALSE(visibleAfterClear);
    EXPECT_FALSE(static_cast<b8>(slot));
}

TEST(PaintSlot, MoveConstructionTransfersTheActiveLease){
    PaintSlotFixture fixture;
    auto& context = fixture.context;
    UiPaintSlot first;
    Optional<UiPaintSlot> second;
    i32 calls = 0;
    first = [&first, &second, &calls](UiPaintContext&){
        ++calls;
        if(calls == 1)
            second.emplace(Move(first));
    };
    first.invoke(context);
    EXPECT_FALSE(static_cast<b8>(first));
    ASSERT_TRUE(second.has_value());
    ASSERT_TRUE(static_cast<b8>(*second));
    second->invoke(context);
    EXPECT_EQ(calls, 2);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

