// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "round_trip_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct DescriptorHeapStorageBufferDispatchPushConstants{
    u32 storageBufferSlot = 0u;
    u32 seed = 0u;
    u32 pad0 = 0u;
    u32 pad1 = 0u;
};


static_assert(sizeof(DescriptorHeapStorageBufferDispatchPushConstants) == sizeof(u32) * 4u);


static_assert(NWB_BINDLESS_HEAP_RESOURCE_SET == 0u);


static_assert(NWB_BINDLESS_HEAP_SAMPLER_SET == 1u);


static_assert(NWB_BINDLESS_HEAP_ACCEL_STRUCT_SET == 2u);


static_assert(NWB_BINDLESS_HEAP_BINDING_STORAGE_BUFFER == 3u);


static_assert(IsNothrowDestructible_V<GpuTimingMeasure>);


static_assert(requires(
    CommandList& commandList,
    TimerQuery* timerQuery,
    const TimerQueryRecordingToken& recording
){
    { commandList.endTimerQueryFromExistingClaim(timerQuery, recording) } noexcept ->SameAs<bool>;
});


static_assert(requires(Futex& mutex){
    { mutex.lock() } noexcept;
    { mutex.try_lock() } noexcept ->SameAs<bool>;
    { mutex.unlock() } noexcept;
});


// Minimal Vulkan 1.3 compute shader: `void main(){}`. The retirement test needs a real pipeline layout containing
// the heap's explicit set-0/1 layouts so CommandList::bindDescriptorBufferHeap records an actual heap use.
inline constexpr u32 s_DescriptorHeapRetirementComputeSpirv[] = {
    0x07230203u, 0x00010600u, 0x00070000u, 0x00000005u, 0x00000000u,
    0x00020011u, 0x00000001u,
    0x0003000eu, 0x00000000u, 0x00000001u,
    0x0005000fu, 0x00000005u, 0x00000001u, 0x6e69616du, 0x00000000u,
    0x00060010u, 0x00000001u, 0x00000011u, 0x00000001u, 0x00000001u, 0x00000001u,
    0x00020013u, 0x00000002u,
    0x00030021u, 0x00000003u, 0x00000002u,
    0x00050036u, 0x00000002u, 0x00000001u, 0x00000000u, 0x00000003u,
    0x000200f8u, 0x00000004u,
    0x000100fdu,
    0x00010038u,
};


// Minimal Vulkan 1.3 vertex/fragment pair for state-only graphics-pipeline binding. Generated with glslc
// --target-env=vulkan1.3 -O and validated by pipeline creation on the validation-backed fixture.
inline constexpr u32 s_CommandBufferLifetimeVertexSpirv[] = {
    0x07230203u, 0x00010600u, 0x000d000bu, 0x00000015u, 0x00000000u, 0x00020011u, 0x00000001u, 0x0006000bu,
    0x00000001u, 0x4c534c47u, 0x6474732eu, 0x3035342eu, 0x00000000u, 0x0003000eu, 0x00000000u, 0x00000001u,
    0x0006000fu, 0x00000000u, 0x00000004u, 0x6e69616du, 0x00000000u, 0x0000000du, 0x00030047u, 0x0000000bu,
    0x00000002u, 0x00050048u, 0x0000000bu, 0x00000000u, 0x0000000bu, 0x00000000u, 0x00050048u, 0x0000000bu,
    0x00000001u, 0x0000000bu, 0x00000001u, 0x00050048u, 0x0000000bu, 0x00000002u, 0x0000000bu, 0x00000003u,
    0x00050048u, 0x0000000bu, 0x00000003u, 0x0000000bu, 0x00000004u, 0x00020013u, 0x00000002u, 0x00030021u,
    0x00000003u, 0x00000002u, 0x00030016u, 0x00000006u, 0x00000020u, 0x00040017u, 0x00000007u, 0x00000006u,
    0x00000004u, 0x00040015u, 0x00000008u, 0x00000020u, 0x00000000u, 0x0004002bu, 0x00000008u, 0x00000009u,
    0x00000001u, 0x0004001cu, 0x0000000au, 0x00000006u, 0x00000009u, 0x0006001eu, 0x0000000bu, 0x00000007u,
    0x00000006u, 0x0000000au, 0x0000000au, 0x00040020u, 0x0000000cu, 0x00000003u, 0x0000000bu, 0x0004003bu,
    0x0000000cu, 0x0000000du, 0x00000003u, 0x00040015u, 0x0000000eu, 0x00000020u, 0x00000001u, 0x0004002bu,
    0x0000000eu, 0x0000000fu, 0x00000000u, 0x0004002bu, 0x00000006u, 0x00000010u, 0x00000000u, 0x0004002bu,
    0x00000006u, 0x00000011u, 0x3f800000u, 0x0007002cu, 0x00000007u, 0x00000012u, 0x00000010u, 0x00000010u,
    0x00000010u, 0x00000011u, 0x00040020u, 0x00000013u, 0x00000003u, 0x00000007u, 0x00050036u, 0x00000002u,
    0x00000004u, 0x00000000u, 0x00000003u, 0x000200f8u, 0x00000005u, 0x00050041u, 0x00000013u, 0x00000014u,
    0x0000000du, 0x0000000fu, 0x0003003eu, 0x00000014u, 0x00000012u, 0x000100fdu, 0x00010038u,
};


inline constexpr u32 s_CommandBufferLifetimeFragmentSpirv[] = {
    0x07230203u, 0x00010600u, 0x000d000bu, 0x0000000cu, 0x00000000u, 0x00020011u, 0x00000001u, 0x0006000bu,
    0x00000001u, 0x4c534c47u, 0x6474732eu, 0x3035342eu, 0x00000000u, 0x0003000eu, 0x00000000u, 0x00000001u,
    0x0006000fu, 0x00000004u, 0x00000004u, 0x6e69616du, 0x00000000u, 0x00000009u, 0x00030010u, 0x00000004u,
    0x00000007u, 0x00040047u, 0x00000009u, 0x0000001eu, 0x00000000u, 0x00020013u, 0x00000002u, 0x00030021u,
    0x00000003u, 0x00000002u, 0x00030016u, 0x00000006u, 0x00000020u, 0x00040017u, 0x00000007u, 0x00000006u,
    0x00000004u, 0x00040020u, 0x00000008u, 0x00000003u, 0x00000007u, 0x0004003bu, 0x00000008u, 0x00000009u,
    0x00000003u, 0x0004002bu, 0x00000006u, 0x0000000au, 0x00000000u, 0x0007002cu, 0x00000007u, 0x0000000bu,
    0x0000000au, 0x0000000au, 0x0000000au, 0x0000000au, 0x00050036u, 0x00000002u, 0x00000004u, 0x00000000u,
    0x00000003u, 0x000200f8u, 0x00000005u, 0x0003003eu, 0x00000009u, 0x0000000bu, 0x000100fdu, 0x00010038u,
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

