// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/capturing_logger.h>
#include <tests/unit/ecs_graphics/name_cases.adhd.h>

#include <impl/ecs_render/material/instance_typed_ranges.adhd.h>
#include <impl/ecs_render/shadow/shadow_material_context_policy.adhd.h>
#include <impl/ecs_render/shadow/types.adhd.h>

#include <core/alloc/global.h>

#include <global/limit.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(ShadowMaterialContext, TypedByteCountPreservesAlignmentAndWrapping){
    const usize maximum = Limit<usize>::s_Max;
    EXPECT_EQ(Impl::ResolveMaterialTypedBufferByteCount(0u, sizeof(u32)), sizeof(u32));
    EXPECT_EQ(Impl::ResolveMaterialTypedBufferByteCount(5u, sizeof(u32)), 8u);
    EXPECT_EQ(Impl::ResolveMaterialTypedBufferByteCount(maximum - 3u, sizeof(u32)), maximum - 3u);
    EXPECT_EQ(Impl::ResolveMaterialTypedBufferByteCount(maximum - 2u, sizeof(u32)), 0u);
    EXPECT_EQ(Impl::ResolveMaterialTypedBufferByteCount(maximum, sizeof(u32)), 0u);
}

TEST(ShadowMaterialContext, RetainedPayloadUsesActualInstanceStrideAndRejectsTruncation){
    CapturingLogger logger;
    const Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::ReportOnly);
    Core::Alloc::GlobalArena arena{Name(NWB::Tests::EcsGraphicsCases::s_EcsCaseTestsShadowMaterialContext)};
    const Array<Impl::NwbRtInstanceMaterialGpu, 2> materials{};
    const Array<Impl::InstanceGpuData, 2> instances{};
    const Array<u8, 4> typed{ 1u, 2u, 3u, 4u };
    Vector<u8, Core::Alloc::GlobalArena> materialBytes(arena);
    Vector<u8, Core::Alloc::GlobalArena> instanceBytes(arena);
    Vector<u8, Core::Alloc::GlobalArena> typedBytes(arena);
    Impl::ShadowMaterialContextPolicy().captureBytes(
        Span<const Impl::NwbRtInstanceMaterialGpu>(materials.data(), materials.size()),
        Span<const Impl::InstanceGpuData>(instances.data(), instances.size()),
        Span<const u8>(typed.data(), typed.size()),
        materialBytes,
        instanceBytes,
        typedBytes
    );
    EXPECT_EQ(instanceBytes.size(), instances.size() * sizeof(Impl::InstanceGpuData));
    const auto validate = [&](const usize materialSize, const usize instanceSize, const usize typedSize)noexcept{
        return Impl::ShadowMaterialContextPolicy().validateRetainState(
            Impl::PreparedShadowMaterialContextRoute::Hardware,
            materials.size(),
            instances.size(),
            typed.size(),
            true,
            materials.size(),
            materials.size(),
            instances.size(),
            instances.size(),
            typed.size(),
            typed.size(),
            true,
            true,
            false,
            false,
            0u,
            0u,
            0u,
            materialSize,
            instanceSize,
            typedSize
        );
    };
    EXPECT_TRUE(validate(materialBytes.size(), instanceBytes.size(), typedBytes.size()));
    EXPECT_FALSE(validate(materialBytes.size() - 1u, instanceBytes.size(), typedBytes.size()));
    EXPECT_FALSE(validate(materialBytes.size(), instanceBytes.size() - 1u, typedBytes.size()));
    EXPECT_FALSE(validate(materialBytes.size(), instanceBytes.size(), typedBytes.size() - 1u));
    EXPECT_EQ(logger.errorCount(), 3u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

