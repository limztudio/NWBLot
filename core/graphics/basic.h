// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/global.h>

#include <core/common/common.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ObjectTypes{
    enum Enum : u32{
        SharedHandle = 0x00000001,
        
        VK_Device = 0x00010001,
        VK_PhysicalDevice = 0x00010002,
        VK_Instance = 0x00010003,
        VK_Queue = 0x00010004,
        VK_CommandBuffer = 0x00010005,
        VK_DeviceMemory = 0x00010006,
        VK_Buffer = 0x00010007,
        VK_Image = 0x00010008,
        VK_ImageView = 0x00010009,
        VK_AccelerationStructureKHR = 0x0001000A,
        VK_Sampler = 0x0001000B,
        VK_ShaderModule = 0x0001000C,
        VK_RenderPass = 0x0001000D, // deprecated
        VK_Framebuffer = 0x0001000E, // deprecated
        VK_DescriptorPool = 0x0001000F,
        VK_DescriptorSetLayout = 0x00010010,
        VK_DescriptorSet = 0x00010011,
        VK_PipelineLayout = 0x00010012,
        VK_Pipeline = 0x00010013,
        VK_Micromap = 0x00010014,
        VK_ImageCreateInfo = 0x00010015,
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct Object{
    union{
        u64 integer;
        void* pointer;
    };
    
    
    constexpr Object(u64 i)noexcept : integer(i){}
    constexpr Object(void* p)noexcept : pointer(p){}
    
    template <typename T>
    explicit operator T*()const noexcept{ return static_cast<T*>(pointer); }
};

inline bool operator==(const Object& lhs, const Object& rhs)noexcept{ return lhs.integer == rhs.integer; }
inline bool operator!=(const Object& lhs, const Object& rhs)noexcept{ return lhs.integer != rhs.integer; }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class IResource{
protected:
    IResource()noexcept = default;
    virtual ~IResource()noexcept = default;
    
    
public:
    IResource(const IResource&) = delete;
    IResource(IResource&&) = delete;
    IResource& operator=(const IResource&) = delete;
    IResource& operator=(IResource&&) = delete;

    
public:
    virtual u32 addReference()noexcept = 0;
    virtual u32 releaseReference()noexcept = 0;
    virtual u32 getReferenceCount()const noexcept = 0;
    
    virtual Object getNativeHandle(ObjectTypes::Enum type){ (void)type; return nullptr; }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

