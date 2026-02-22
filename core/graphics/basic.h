// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/global.h>

#include <core/alloc/alloc.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifdef NWB_DEBUG
#define NWB_GRAPHICS_DEBUGGABLE
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


typedef u32 ObjectType;

namespace ObjectTypes{
    constexpr ObjectType VK_Device                              = 0x00030001;
    constexpr ObjectType VK_PhysicalDevice                      = 0x00030002;
    constexpr ObjectType VK_Instance                            = 0x00030003;
    constexpr ObjectType VK_Queue                               = 0x00030004;
    constexpr ObjectType VK_CommandBuffer                       = 0x00030005;
    constexpr ObjectType VK_DeviceMemory                        = 0x00030006;
    constexpr ObjectType VK_Buffer                              = 0x00030007;
    constexpr ObjectType VK_Image                               = 0x00030008;
    constexpr ObjectType VK_ImageView                           = 0x00030009;
    constexpr ObjectType VK_AccelerationStructureKHR            = 0x0003000a;
    constexpr ObjectType VK_Sampler                             = 0x0003000b;
    constexpr ObjectType VK_ShaderModule                        = 0x0003000c;
    constexpr ObjectType VK_DescriptorPool                      = 0x0003000f;
    constexpr ObjectType VK_DescriptorSetLayout                 = 0x00030010;
    constexpr ObjectType VK_DescriptorSet                       = 0x00030011;
    constexpr ObjectType VK_PipelineLayout                      = 0x00030012;
    constexpr ObjectType VK_Pipeline                            = 0x00030013;
    constexpr ObjectType VK_Micromap                            = 0x00030014;
    constexpr ObjectType VK_ImageCreateInfo                     = 0x00030015;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct Object{
    union{
        u64 integer;
        void* pointer;
    };
    
    
    constexpr Object(u64 i)noexcept : integer(i){}
    constexpr Object(void* p)noexcept : pointer(p){}
    
    template<typename T>
    explicit operator T*()const noexcept{ return static_cast<T*>(pointer); }
};

inline bool operator==(const Object& lhs, const Object& rhs)noexcept{ return lhs.integer == rhs.integer; }
inline bool operator!=(const Object& lhs, const Object& rhs)noexcept{ return lhs.integer != rhs.integer; }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class IResource : public Alloc::ITaskScheduler{
    template<typename> friend struct ArenaRefDeleter;


protected:
    inline explicit IResource(Alloc::ThreadPool& pool)noexcept : Alloc::ITaskScheduler(pool){}
    virtual ~IResource()noexcept = default;
    
    
public:
    IResource(const IResource&) = delete;
    IResource(IResource&&) = delete;
    IResource& operator=(const IResource&) = delete;
    IResource& operator=(IResource&&) = delete;

    
public:
    virtual u32 addReference()noexcept = 0;
    virtual u32 release()noexcept = 0;
    
    virtual Object getNativeHandle(ObjectType type){ (void)type; return nullptr; }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

