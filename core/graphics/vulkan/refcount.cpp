#include "backend.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_DEFINE_VULKAN_REFCOUNT_HOOKS(Type) \
    u32 RefCountAddReference(Type* value)noexcept{ return value->addReference(); } \
    u32 RefCountRelease(Type* value)noexcept{ return value->release(); } \
    void DestroyArenaReference(Alloc::GlobalArena* arena, Type* value)noexcept{ \
        value->~Type(); \
        arena->deallocate(value, alignof(Type), sizeof(Type)); \
    }

NWB_DEFINE_VULKAN_REFCOUNT_HOOKS(Device)
NWB_DEFINE_VULKAN_REFCOUNT_HOOKS(Heap)
NWB_DEFINE_VULKAN_REFCOUNT_HOOKS(Texture)
NWB_DEFINE_VULKAN_REFCOUNT_HOOKS(StagingTexture)
NWB_DEFINE_VULKAN_REFCOUNT_HOOKS(SamplerFeedbackTexture)
NWB_DEFINE_VULKAN_REFCOUNT_HOOKS(Buffer)
NWB_DEFINE_VULKAN_REFCOUNT_HOOKS(Shader)
NWB_DEFINE_VULKAN_REFCOUNT_HOOKS(ShaderLibrary)
NWB_DEFINE_VULKAN_REFCOUNT_HOOKS(Sampler)
NWB_DEFINE_VULKAN_REFCOUNT_HOOKS(InputLayout)
NWB_DEFINE_VULKAN_REFCOUNT_HOOKS(Framebuffer)
NWB_DEFINE_VULKAN_REFCOUNT_HOOKS(AccelStruct)
NWB_DEFINE_VULKAN_REFCOUNT_HOOKS(OpacityMicromap)
NWB_DEFINE_VULKAN_REFCOUNT_HOOKS(BindingLayout)
NWB_DEFINE_VULKAN_REFCOUNT_HOOKS(BindingSet)
NWB_DEFINE_VULKAN_REFCOUNT_HOOKS(DescriptorTable)
NWB_DEFINE_VULKAN_REFCOUNT_HOOKS(GraphicsPipeline)
NWB_DEFINE_VULKAN_REFCOUNT_HOOKS(ComputePipeline)
NWB_DEFINE_VULKAN_REFCOUNT_HOOKS(MeshletPipeline)
NWB_DEFINE_VULKAN_REFCOUNT_HOOKS(EventQuery)
NWB_DEFINE_VULKAN_REFCOUNT_HOOKS(TimerQuery)
NWB_DEFINE_VULKAN_REFCOUNT_HOOKS(ShaderTable)
NWB_DEFINE_VULKAN_REFCOUNT_HOOKS(RayTracingPipeline)
NWB_DEFINE_VULKAN_REFCOUNT_HOOKS(CommandList)

#undef NWB_DEFINE_VULKAN_REFCOUNT_HOOKS


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

