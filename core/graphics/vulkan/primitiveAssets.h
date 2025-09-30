// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once

#include "../common.h"
#include "config.h"

#include <core/alloc/assetPool.h>

#include "resources.h"

#include "vk_mem_alloc.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u8 s_maxShaderStages = 5;
constexpr u8 s_maxDescriptorSetLayouts = 8;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_DEFINE_ASSET_TYPE(T, INDEX) \
    struct T; \
    using T##Handle = Alloc::AssetHandle<T>; \
    namespace Alloc{ constexpr u8 AssetTypeIndex(Alloc::AssetTypeTag<T>)noexcept{ return static_cast<u8>(INDEX); } };


NWB_DEFINE_ASSET_TYPE(Buffer, 0);
NWB_DEFINE_ASSET_TYPE(Texture, 1);
NWB_DEFINE_ASSET_TYPE(ShaderState, 2);
NWB_DEFINE_ASSET_TYPE(Sampler, 3);
NWB_DEFINE_ASSET_TYPE(DescriptorSetLayout, 4);
NWB_DEFINE_ASSET_TYPE(DescriptorSet, 5);
NWB_DEFINE_ASSET_TYPE(Pipeline, 6);
NWB_DEFINE_ASSET_TYPE(RenderPass, 7);


#undef NWB_DEFINE_ASSET_TYPE


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ViewportState{
    u32 numViewports = 0;
    u32 numScissors = 0;

    Viewport* viewports = nullptr;
    Rect2DInt* scissors = nullptr;
};

struct StencilOperationState{
    VkStencilOp failOp = VK_STENCIL_OP_KEEP;
    VkStencilOp passOp = VK_STENCIL_OP_KEEP;
    VkStencilOp depthFailOp = VK_STENCIL_OP_KEEP;
    VkCompareOp compareOp = VK_COMPARE_OP_ALWAYS;

    u32 compareMask = 0xff;
    u32 writeMask = 0xff;
    u32 reference = 0xff;
};

struct BlendState{
    VkBlendFactor srcColorFactor = VK_BLEND_FACTOR_ONE;
    VkBlendFactor dstColorFactor = VK_BLEND_FACTOR_ZERO;
    VkBlendOp colorOp = VK_BLEND_OP_ADD;

    VkBlendFactor srcAlphaFactor = VK_BLEND_FACTOR_ONE;
    VkBlendFactor dstAlphaFactor = VK_BLEND_FACTOR_ZERO;
    VkBlendOp alphaOp = VK_BLEND_OP_ADD;

    ColorWrite::Mask colorWriteMask = ColorWrite::Mask::MASK_ALL;

    u8 blendEnabled : 1;
    u8 separateBlend : 1;
    u8 reserved : 6;


    BlendState() : blendEnabled(0), separateBlend(0) {}

    BlendState& setColor(VkBlendFactor src, VkBlendFactor dst, VkBlendOp op){
        srcColorFactor = src;
        dstColorFactor = dst;
        colorOp = op;
        blendEnabled = 1;
        return *this;
    }
    BlendState& setAlpha(VkBlendFactor src, VkBlendFactor dst, VkBlendOp op){
        srcAlphaFactor = src;
        dstAlphaFactor = dst;
        alphaOp = op;
        blendEnabled = 1;
        return *this;
    }
    BlendState& setColorWriteMask(ColorWrite::Mask mask){
        colorWriteMask = mask;
        return *this;
    }
};

struct ShaderStage{
    const char* code = nullptr;
    u32 codeSize = 0;
    VkShaderStageFlagBits type = VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM;
};

struct DescriptorSetUpdate{
    DescriptorSetHandle descriptorSet;
    u32 frameIssued = 0;
};

struct VertexAttribute{
    u16 location = 0;
    u16 binding = 0;
    u32 offset = 0;
    VertexComponentType::Enum format = VertexComponentType::kCount;
};

struct VertexStream{
    u16 binding = 0;
    u16 stride = 0;
    VertexInputRate::Enum inputRate = VertexInputRate::kCount;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct DepthStencilStateCreation{
    StencilOperationState front;
    StencilOperationState back;
    VkCompareOp depthComarison = VK_COMPARE_OP_ALWAYS;

    u8 depthEnabled : 1;
    u8 depthWriteEnabled : 1;
    u8 stencilEnabled : 1;
    u8 reserved : 5;


    DepthStencilStateCreation() : depthEnabled(0), depthWriteEnabled(0), stencilEnabled(0) {}

    DepthStencilStateCreation& setDepth(bool write, VkCompareOp comparisonTest){
        depthWriteEnabled = write ? 1 : 0;
        depthComarison = comparisonTest;
        depthEnabled = 1;
        return *this;
    }
};

struct BlendStateCreation{
    BlendState blendStates[s_maxImageOutputs];
    u32 activeStates = 0;


    BlendStateCreation& reset(){
        activeStates = 0;
        return *this;
    }
    BlendState& addBlendState(){
        NWB_ASSERT(activeStates < LengthOf(blendStates));
        return blendStates[activeStates++];
    }
};

struct RasterizationCreation{
    VkCullModeFlagBits cullMode = VK_CULL_MODE_NONE;
    VkFrontFace front = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    FillMode::Enum fill = FillMode::SOLID;
};

struct BufferCreation{
    VkBufferUsageFlags flags = 0;
    ResourceUsageType::Enum usage = ResourceUsageType::IMMUTABLE;
    u32 size = 0;
    void* initialData = nullptr;

    const char* name = nullptr;


    BufferCreation& reset(){
        size = 0;
        initialData = nullptr;
        return *this;
    }
    BufferCreation& set(VkBufferUsageFlags _flags, ResourceUsageType::Enum _usage, u32 _size){
        flags = _flags;
        usage = _usage;
        size = _size;
        return *this;
    }
    BufferCreation& setData(void* data){
        initialData = data;
        return *this;
    }
    BufferCreation& setName(const char* _name){
        name = _name;
        return *this;
};

struct TextureCreation{
    void* initialData = nullptr;
    u16 width = 1;
    u16 height = 1;
    u16 depth = 1;
    u8 mipmaps = 1;
    u8 flags = 0; // TextureFlags bitmasks

    VkFormat format = VK_FORMAT_UNDEFINED;
    TextureType::Enum type = TextureType::TEX2D;

    const char* name = nullptr;


    TextureCreation& setSize(u16 _width, u16 _height, u16 _depth){
        width = _width;
        height = _height;
        depth = _depth;
        return *this;
    }
    TextureCreation& setFlags(u8 _mipmaps, u8 _flags){
        mipmaps = _mipmaps;
        flags = _flags;
        return *this;
    }
    TextureCreation& setFormat(VkFormat _format, TextureType::Enum _type){
        format = _format;
        type = _type;
        return *this;
    }
    TextureCreation& setName(const char* _name){
        name = _name;
        return *this;
    }
    TextureCreation& setData(void* data){
        initialData = data;
        return *this;
    }
};

struct SamplerCreation{
    VkFilter minFilter = VK_FILTER_NEAREST;
    VkFilter magFilter = VK_FILTER_NEAREST;
    VkSamplerMipmapMode mipFilter = VK_SAMPLER_MIPMAP_MODE_NEAREST;

    VkSamplerAddressMode addressU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    VkSamplerAddressMode addressV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    VkSamplerAddressMode addressW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

    const char* name = nullptr;


    SamplerCreation& setMinMagMip(VkFilter min, VkFilter mag, VkSamplerMipmapMode mip){
        minFilter = min;
        magFilter = mag;
        mipFilter = mip;
        return *this;
    }
    SamplerCreation& setAddressU(VkSamplerAddressMode u){
        addressU = u;
        return *this;
    }
    SamplerCreation& setAddressUV(VkSamplerAddressMode u, VkSamplerAddressMode v){
        addressU = u;
        addressV = v;
        return *this;
    }
    SamplerCreation& setAddressUVW(VkSamplerAddressMode u, VkSamplerAddressMode v, VkSamplerAddressMode w){
        addressU = u;
        addressV = v;
        addressW = w;
        return *this;
    }
    SamplerCreation& setName(const char* _name){
        name = _name;
        return *this;
};

struct ShaderStateCreation{
    ShaderStage stages[s_maxShaderStages];

    const char* name = nullptr;

    u32 stagesCount = 0;
    u32 spvInput = 0;


    ShaderStateCreation& reset(){
        stagesCount = 0;
        return *this;
    }
    ShaderStateCreation& setName(const char* _name){
        name = _name;
        return *this;
    }
    ShaderStateCreation& addStage(const char* code, u32 codeSize, VkShaderStageFlagBits type){
        NWB_ASSERT(stagesCount < LengthOf(stages));
        stages[stagesCount].code = code;
        stages[stagesCount].codeSize = codeSize;
        stages[stagesCount].type = type;
        ++stagesCount;
        return *this;
    }
    ShaderStateCreation& setSPVInput(u32 input){
        spvInput = input;
        return *this;
    }
};

struct DescriptorSetLayoutCreation{
    struct Binding{
        VkDescriptorType type = VK_DESCRIPTOR_TYPE_MAX_ENUM;
        u16 start = 0;
        u16 count = 0;
        const char* name = nullptr;
    };

    Binding bindings[s_maxDescriptorsPerSet];
    u32 numBindings = 0;
    u32 setIndex = 0;

    const char* name = nullptr;


    DescriptorSetLayoutCreation& reset(){
        numBindings = 0;
        setIndex = 0;
        return *this;
    }
    DescriptorSetLayoutCreation& addBinding(const Binding& binding){
        NWB_ASSERT(numBindings < LengthOf(bindings));
        bindings[numBindings++] = binding;
        return *this;
    }
    DescriptorSetLayoutCreation& addBindingAtIndex(const Binding& binding, u32 index){
        NWB_ASSERT(index < LengthOf(bindings));
        if((index + 1) > numBindings)
            numBindings = index + 1;
        return *this;
    }
    DescriptorSetLayoutCreation& setName(const char* _name){
        name = _name;
        return *this;
    }
    DescriptorSetLayoutCreation& setIndex(u32 index){
        setIndex = index;
        return *this;
    }
};

struct DescriptorSetCreation{
    
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct Buffer{
    VkBuffer buffer;
    VmaAllocation allocation;
    VkDeviceMemory deviceMemory;
    VkDeviceSize deviceSize;

    VkBufferUsageFlags typeFlags = 0;
    ResourceUsageType::Enum usage = ResourceUsageType::IMMUTABLE;
    u32 size = 0;
    u32 globalOffset = 0;

    BufferHandle handle;
    BufferHandle presentHandle;

    const char* name = nullptr;
};


struct Sampler{
    VkSampler sampler;

    VkFilter minFilter = VK_FILTER_NEAREST;
    VkFilter magFilter = VK_FILTER_NEAREST;
    VkSamplerMipmapMode mipFilter = VK_SAMPLER_MIPMAP_MODE_NEAREST;

    VkSamplerAddressMode addressU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    VkSamplerAddressMode addressV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    VkSamplerAddressMode addressW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

    const char* name = nullptr;
};


struct Texture{
    VkImage image;
    VkImageView imageView;
    VkFormat format;
    VkImageLayout layout;
    VmaAllocation allocation;

    u16 width = 1;
    u16 height = 1;
    u16 depth = 1;
    u8 mipLevels = 1;
    u8 flags = 0;

    TextureHandle handle;
    TextureType::Enum type = TextureType::TEX2D;

    Sampler* sampler = nullptr;

    const char* name = nullptr;
};


struct ShaderState{
    VkPipelineShaderStageCreateInfo stageInfo[s_maxShaderStages];

    const char* name = nullptr;

    u32 activeShaderCount = 0;
    bool graphicsPipeline = false;

    spirVResult;
};


struct DescriptorBinding{
    VkDescriptorType type;
    u16 start = 0;
    u16 count = 0;
    u16 set = 0;

    const char* name = nullptr;
};


struct DescriptorSetLayout{
    VkDescriptorSetLayout descLayout;

    VkDescriptorSetLayoutBinding* binding = nullptr;
    DescriptorBinding* bindings = nullptr;
    u16 numBindings = 0;
    u16 setIndex = 0;

    DescriptorSetLayoutHandle handle;
};


struct DescriptorSet{
    VkDescriptorSet descSet;
    
    Alloc::AssetHandleAny* resources = nullptr;
    SamplerHandle* samplers = nullptr;
    u16* bindings = nullptr;

    const DescriptorSetLayout* layout = nullptr;
    u32 numResources = 0;
};


struct Pipeline{
    VkPipeline pipeline;
    VkPipelineLayout pipeLayout;

    VkPipelineBindPoint bindPoint;

    ShaderStateHandle shaderState;

    const DescriptorSetLayout* descriptorSet[s_maxDescriptorSetLayouts];
    DescriptorSetLayoutHandle descriptorSetLayoutHandle[s_maxDescriptorSetLayouts];
    u32 numActiveLayouts = 0;

    
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

