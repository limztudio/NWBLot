// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once

#include "../common.h"
#include "config.h"

#include <core/alloc/assetPool.h>

#include "vk_mem_alloc.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr const u8 s_maxImageOutputs = 8;
static constexpr const u8 s_maxDescriptorSetLayouts = 8;
static constexpr const u8 s_maxShaderStages = 5;
static constexpr const u8 s_maxDescriptorsPerSet = 16;
static constexpr const u8 s_maxVertexStreams = 16;
static constexpr const u8 s_maxVertexAttributes = 16;

static constexpr const u8 s_maxSpirVSets = 32;

static constexpr const u32 s_spirBindlessTextureBinding = 10;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_DEFINE_ASSET_TYPE(T, INDEX) \
    struct T; \
    template <typename Arena> \
    using T##Pool = Alloc::AssetPool<T, Arena>; \
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
    VkStencilOp vkFailOp = VK_STENCIL_OP_KEEP;
    VkStencilOp vkPassOp = VK_STENCIL_OP_KEEP;
    VkStencilOp vkDepthFailOp = VK_STENCIL_OP_KEEP;
    VkCompareOp vkCompareOp = VK_COMPARE_OP_ALWAYS;

    u32 compareMask = 0xff;
    u32 writeMask = 0xff;
    u32 reference = 0xff;
};

struct BlendState{
    VkBlendFactor vkSrcColorFactor = VK_BLEND_FACTOR_ONE;
    VkBlendFactor vkDstColorFactor = VK_BLEND_FACTOR_ZERO;
    VkBlendOp vkColorOp = VK_BLEND_OP_ADD;

    VkBlendFactor vkSrcAlphaFactor = VK_BLEND_FACTOR_ONE;
    VkBlendFactor vkDstAlphaFactor = VK_BLEND_FACTOR_ZERO;
    VkBlendOp vkAlphaOp = VK_BLEND_OP_ADD;

    ColorWrite::Mask colorWriteMask = ColorWrite::Mask::MASK_ALL;

    u8 blendEnabled : 1;
    u8 separateBlend : 1;
    u8 reserved : 6;


    BlendState() : blendEnabled(0), separateBlend(0) {}

    inline BlendState& setColor(VkBlendFactor src, VkBlendFactor dst, VkBlendOp op){
        vkSrcColorFactor = src;
        vkDstColorFactor = dst;
        vkColorOp = op;
        blendEnabled = 1;
        return *this;
    }
    inline BlendState& setAlpha(VkBlendFactor src, VkBlendFactor dst, VkBlendOp op){
        vkSrcAlphaFactor = src;
        vkDstAlphaFactor = dst;
        vkAlphaOp = op;
        blendEnabled = 1;
        return *this;
    }
    inline BlendState& setColorWriteMask(ColorWrite::Mask mask){
        colorWriteMask = mask;
        return *this;
    }
};

struct ShaderStage{
    const char* code = nullptr;
    u32 codeSize = 0;
    VkShaderStageFlagBits vkType = VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM;
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

struct RenderPassOutput{
    VkFormat vkColorFormats[s_maxImageOutputs];
    VkFormat vkDepthStencilFormat;
    u32 numColorFormats = 0;

    RenderPassOperation::Enum colorOP = RenderPassOperation::DONT_CARE;
    RenderPassOperation::Enum depthOP = RenderPassOperation::DONT_CARE;
    RenderPassOperation::Enum stencilOP = RenderPassOperation::DONT_CARE;


    inline RenderPassOutput& reset(){
        numColorFormats = 0;
        for(auto& cur : vkColorFormats)
            cur = VK_FORMAT_UNDEFINED;
        vkDepthStencilFormat = VK_FORMAT_UNDEFINED;
        colorOP = RenderPassOperation::DONT_CARE;
        depthOP = RenderPassOperation::DONT_CARE;
        stencilOP = RenderPassOperation::DONT_CARE;
        return *this;
    }
    inline RenderPassOutput& addColor(VkFormat format){
        NWB_ASSERT(numColorFormats < LengthOf(vkColorFormats));
        vkColorFormats[numColorFormats] = format;
        ++numColorFormats;
        return *this;
    }
    inline RenderPassOutput& setDepthStencil(VkFormat format){
        vkDepthStencilFormat = format;
        return *this;
    }
    inline RenderPassOutput& setOperations(RenderPassOperation::Enum color, RenderPassOperation::Enum depth, RenderPassOperation::Enum stencil){
        colorOP = color;
        depthOP = depth;
        stencilOP = stencil;
        return *this;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct DepthStencilStateCreation{
    StencilOperationState front;
    StencilOperationState back;
    VkCompareOp vkDepthComparison = VK_COMPARE_OP_ALWAYS;

    u8 depthEnabled : 1;
    u8 depthWriteEnabled : 1;
    u8 stencilEnabled : 1;
    u8 reserved : 5;


    DepthStencilStateCreation() : depthEnabled(0), depthWriteEnabled(0), stencilEnabled(0) {}

    inline DepthStencilStateCreation& setDepth(bool write, VkCompareOp comparisonTest){
        depthWriteEnabled = write ? 1 : 0;
        vkDepthComparison = comparisonTest;
        depthEnabled = 1;
        return *this;
    }
};

struct BlendStateCreation{
    BlendState blendStates[s_maxImageOutputs];
    u32 activeStates = 0;


    inline BlendStateCreation& reset(){
        activeStates = 0;
        return *this;
    }
    inline BlendState& addBlendState(){
        NWB_ASSERT(activeStates < LengthOf(blendStates));
        return blendStates[activeStates++];
    }
};

struct RasterizationCreation{
    VkCullModeFlagBits vkCullMode = VK_CULL_MODE_NONE;
    VkFrontFace vkFront = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    FillMode::Enum fill = FillMode::SOLID;
};

struct BufferCreation{
    VkBufferUsageFlags vkFlags = 0;
    ResourceUsageType::Enum usage = ResourceUsageType::IMMUTABLE;
    u32 size = 0;
    void* initialData = nullptr;

    const char* name = nullptr;


    inline BufferCreation& reset(){
        size = 0;
        initialData = nullptr;
        return *this;
    }
    inline BufferCreation& set(VkBufferUsageFlags _flags, ResourceUsageType::Enum _usage, u32 _size){
        vkFlags = _flags;
        usage = _usage;
        size = _size;
        return *this;
    }
    inline BufferCreation& setData(void* data){
        initialData = data;
        return *this;
    }
    inline BufferCreation& setName(const char* _name){
        name = _name;
        return *this;
    }
};

struct TextureCreation{
    void* initialData = nullptr;
    u16 width = 1;
    u16 height = 1;
    u16 depth = 1;
    u8 mipmaps = 1;
    u8 flags = 0; // TextureFlags bitmasks

    VkFormat vkFormat = VK_FORMAT_UNDEFINED;
    TextureType::Enum type = TextureType::TEX2D;

    const char* name = nullptr;


    inline TextureCreation& setSize(u16 _width, u16 _height, u16 _depth){
        width = _width;
        height = _height;
        depth = _depth;
        return *this;
    }
    inline TextureCreation& setFlags(u8 _mipmaps, u8 _flags){
        mipmaps = _mipmaps;
        flags = _flags;
        return *this;
    }
    inline TextureCreation& setFormat(VkFormat _format, TextureType::Enum _type){
        vkFormat = _format;
        type = _type;
        return *this;
    }
    inline TextureCreation& setName(const char* _name){
        name = _name;
        return *this;
    }
    inline TextureCreation& setData(void* data){
        initialData = data;
        return *this;
    }
};

struct SamplerCreation{
    VkFilter vkMinFilter = VK_FILTER_NEAREST;
    VkFilter vkMagFilter = VK_FILTER_NEAREST;
    VkSamplerMipmapMode vkMipFilter = VK_SAMPLER_MIPMAP_MODE_NEAREST;

    VkSamplerAddressMode vkAddressU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    VkSamplerAddressMode vkAddressV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    VkSamplerAddressMode vkAddressW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

    const char* name = nullptr;


    inline SamplerCreation& setMinMagMip(VkFilter min, VkFilter mag, VkSamplerMipmapMode mip){
        vkMinFilter = min;
        vkMagFilter = mag;
        vkMipFilter = mip;
        return *this;
    }
    inline SamplerCreation& setAddressU(VkSamplerAddressMode u){
        vkAddressU = u;
        return *this;
    }
    inline SamplerCreation& setAddressUV(VkSamplerAddressMode u, VkSamplerAddressMode v){
        vkAddressU = u;
        vkAddressV = v;
        return *this;
    }
    inline SamplerCreation& setAddressUVW(VkSamplerAddressMode u, VkSamplerAddressMode v, VkSamplerAddressMode w){
        vkAddressU = u;
        vkAddressV = v;
        vkAddressW = w;
        return *this;
    }
    inline SamplerCreation& setName(const char* _name){
        name = _name;
        return *this;
    }
};

struct ShaderStateCreation{
    ShaderStage stages[s_maxShaderStages];

    const char* name = nullptr;

    u32 stagesCount = 0;
    u32 spvInput = 0;


    inline ShaderStateCreation& reset(){
        stagesCount = 0;
        return *this;
    }
    inline ShaderStateCreation& setName(const char* _name){
        name = _name;
        return *this;
    }
    inline ShaderStateCreation& addStage(const char* code, u32 codeSize, VkShaderStageFlagBits type){
        NWB_ASSERT(stagesCount < LengthOf(stages));
        stages[stagesCount].code = code;
        stages[stagesCount].codeSize = codeSize;
        stages[stagesCount].vkType = type;
        ++stagesCount;
        return *this;
    }
    inline ShaderStateCreation& setSPVInput(u32 input){
        spvInput = input;
        return *this;
    }
};

struct DescriptorSetLayoutCreation{
    struct Binding{
        VkDescriptorType type = VK_DESCRIPTOR_TYPE_MAX_ENUM;
        u16 index = 0;
        u16 count = 0;
        const char* name = nullptr;
    };

    Binding bindings[s_maxDescriptorsPerSet];
    u32 numBindings = 0;
    u32 curIndex = 0;

    const char* name = nullptr;


    inline DescriptorSetLayoutCreation& reset(){
        numBindings = 0;
        curIndex = 0;
        return *this;
    }
    inline DescriptorSetLayoutCreation& addBinding(const Binding& binding){
        NWB_ASSERT(numBindings < LengthOf(bindings));
        bindings[numBindings] = binding;
        ++numBindings;
        return *this;
    }
    inline DescriptorSetLayoutCreation& addBinding(VkDescriptorType type, u16 index, u16 count, const char* _name){
        NWB_ASSERT(numBindings < LengthOf(bindings));
        bindings[numBindings] = { type, index, count, _name };
        ++numBindings;
        return *this;
    }
    inline DescriptorSetLayoutCreation& addBindingAtIndex(const Binding& binding, u32 index){
        NWB_ASSERT(index < LengthOf(bindings));
        bindings[index] = binding;
        if((index + 1) > numBindings)
            numBindings = index + 1;
        return *this;
    }
    inline DescriptorSetLayoutCreation& setName(const char* _name){
        name = _name;
        return *this;
    }
    inline DescriptorSetLayoutCreation& setIndex(u32 index){
        curIndex = index;
        return *this;
    }
};

struct DescriptorSetCreation{
    Alloc::AssetHandleFlexible resources[s_maxDescriptorsPerSet];
    SamplerHandle samplers[s_maxDescriptorsPerSet];
    u16 bindings[s_maxDescriptorsPerSet];

    DescriptorSetLayoutHandle layout;
    u32 numResources = 0;

    const char* name = nullptr;


    inline DescriptorSetCreation& reset(){
        numResources = 0;
        return *this;
    }
    inline DescriptorSetCreation& setLayout(DescriptorSetLayoutHandle _layout){
        layout = _layout;
        return *this;
    }
    inline DescriptorSetCreation& addTexture(TextureHandle texture, u16 binding){
        NWB_ASSERT(numResources < LengthOf(resources));
        resources[numResources] = texture;
        samplers[numResources] = SamplerHandle();
        bindings[numResources] = binding;
        ++numResources;
        return *this;
    }
    inline DescriptorSetCreation& addBuffer(BufferHandle buffer, u16 binding){
        NWB_ASSERT(numResources < LengthOf(resources));
        resources[numResources] = buffer;
        samplers[numResources] = SamplerHandle();
        bindings[numResources] = binding;
        ++numResources;
        return *this;
    }
    inline DescriptorSetCreation& addTextureSampler(TextureHandle texture, SamplerHandle sampler, u16 binding){
        NWB_ASSERT(numResources < LengthOf(resources));
        resources[numResources] = texture;
        samplers[numResources] = sampler;
        bindings[numResources] = binding;
        ++numResources;
        return *this;
    }
    inline DescriptorSetCreation& setName(const char* _name){
        name = _name;
        return *this;
    }
};

struct VertexInputCreation{
    u32 numVertexStreams = 0;
    u32 numVertexAttributes = 0;

    VertexStream vertexStreams[s_maxVertexStreams];
    VertexAttribute vertexAttributes[s_maxVertexAttributes];


    inline VertexInputCreation& reset(){
        numVertexStreams = 0;
        numVertexAttributes = 0;
        return *this;
    }
    inline VertexInputCreation& addVertexStream(const VertexStream& stream){
        NWB_ASSERT(numVertexStreams < LengthOf(vertexStreams));
        vertexStreams[numVertexStreams] = stream;
        ++numVertexStreams;
        return *this;
    }
    inline VertexInputCreation& addVertexAttribute(const VertexAttribute& attribute){
        NWB_ASSERT(numVertexAttributes < LengthOf(vertexAttributes));
        vertexAttributes[numVertexAttributes] = attribute;
        ++numVertexAttributes;
        return *this;
    }
};

struct RenderPassCreation{
    u16 numRenderTargets = 0;
    RenderPassType::Enum type = RenderPassType::GEOMETRY;

    TextureHandle outputTextures[s_maxImageOutputs];
    TextureHandle depthStencilTexture;

    f32 scaleX = 1;
    f32 scaleY = 1;
    u8 resize = 1;

    RenderPassOperation::Enum colorOp = RenderPassOperation::DONT_CARE;
    RenderPassOperation::Enum depthOp = RenderPassOperation::DONT_CARE;
    RenderPassOperation::Enum stencilOp = RenderPassOperation::DONT_CARE;

    const char* name = nullptr;


    inline RenderPassCreation& reset(){
        numRenderTargets = 0;
        depthStencilTexture = TextureHandle();
        scaleX = 1;
        scaleY = 1;
        resize = 0;
        colorOp = RenderPassOperation::DONT_CARE;
        depthOp = RenderPassOperation::DONT_CARE;
        stencilOp = RenderPassOperation::DONT_CARE;
        return *this;
    }
    inline RenderPassCreation& addRenderTexture(TextureHandle texture){
        NWB_ASSERT(numRenderTargets < LengthOf(outputTextures));
        outputTextures[numRenderTargets] = texture;
        ++numRenderTargets;
        return *this;
    }
    inline RenderPassCreation& setScale(f32 _scaleX, f32 _scaleY, u8 _resize){
        scaleX = _scaleX;
        scaleY = _scaleY;
        resize = _resize;
        return *this;
    }
    inline RenderPassCreation& setDepthStencilTexture(TextureHandle texture){
        depthStencilTexture = texture;
        return *this;
    }
    inline RenderPassCreation& setType(RenderPassType::Enum _type){
        type = _type;
        return *this;
    }
    inline RenderPassCreation& setOperations(RenderPassOperation::Enum color, RenderPassOperation::Enum depth, RenderPassOperation::Enum stencil){
        colorOp = color;
        depthOp = depth;
        stencilOp = stencil;
        return *this;
    }
    inline RenderPassCreation& setName(const char* _name){
        name = _name;
        return *this;
    }
};

struct PipelineCreation{
    RasterizationCreation rasterization;
    DepthStencilStateCreation depthStencil;
    BlendStateCreation blendState;
    VertexInputCreation vertexInput;
    ShaderStateCreation shaderState;

    RenderPassOutput renderPass;
    DescriptorSetLayoutHandle descriptorSetLayout[s_maxDescriptorSetLayouts];
    const ViewportState* viewport = nullptr;

    u32 numActiveLayouts = 0;

    const char* name = nullptr;


    inline PipelineCreation& addDescriptorSetLayout(DescriptorSetLayoutHandle layout){
        NWB_ASSERT(numActiveLayouts < LengthOf(descriptorSetLayout));
        descriptorSetLayout[numActiveLayouts] = layout;
        ++numActiveLayouts;
        return *this;
    }
    inline RenderPassOutput& getRenderPassOutput(){
        return renderPass;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct SpirVParseResult{
    u32 numSets = 0;
    DescriptorSetLayoutCreation sets[s_maxSpirVSets];


    bool parse(const void* data, usize size, Alloc::CustomArena& arena);
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct Buffer{
    VkBuffer vkBuffer;
    VmaAllocation vkAllocation;
    VkDeviceMemory vkDeviceMemory;
    VkDeviceSize vkDeviceSize;

    VkBufferUsageFlags vkTypeFlags = 0;
    ResourceUsageType::Enum usage = ResourceUsageType::IMMUTABLE;
    u32 size = 0;
    u32 globalOffset = 0;

    BufferHandle handle;
    BufferHandle presentHandle;

    const char* name = nullptr;
};

struct Sampler{
    VkSampler vkSampler;

    VkFilter vkMinFilter = VK_FILTER_NEAREST;
    VkFilter vkMagFilter = VK_FILTER_NEAREST;
    VkSamplerMipmapMode vkMipFilter = VK_SAMPLER_MIPMAP_MODE_NEAREST;

    VkSamplerAddressMode vkAddressU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    VkSamplerAddressMode vkAddressV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    VkSamplerAddressMode vkAddressW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

    const char* name = nullptr;
};

struct Texture{
    VkImage vkImage;
    VkImageView vkImageView;
    VkFormat vkFormat;
    VkImageLayout vkLayout;
    VmaAllocation vkAllocation;

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
    VkPipelineShaderStageCreateInfo vkStageInfo[s_maxShaderStages];

    const char* name = nullptr;

    u32 activeShaderCount = 0;
    bool graphicsPipeline = false;

    SpirVParseResult parseResult;
};

struct DescriptorBinding{
    VkDescriptorType vkType;
    u16 start = 0;
    u16 count = 0;
    u16 set = 0;

    const char* name = nullptr;
};

struct DescriptorSetLayout{
    VkDescriptorSetLayout vkDescLayout;

    VkDescriptorSetLayoutBinding* vkBinding = nullptr;
    DescriptorBinding* bindings = nullptr;
    u16 numBindings = 0;
    u16 setIndex = 0;

    DescriptorSetLayoutHandle handle;
};

struct DescriptorSet{
    VkDescriptorSet vkDescSet;
    
    Alloc::AssetHandleAny* resources = nullptr;
    SamplerHandle* samplers = nullptr;
    u16* bindings = nullptr;

    const DescriptorSetLayout* layout = nullptr;
    u32 numResources = 0;
};

struct Pipeline{
    VkPipeline vkPipeline;
    VkPipelineLayout vkPipeLayout;

    VkPipelineBindPoint vkBindPoint;

    ShaderStateHandle shaderState;

    const DescriptorSetLayout* descriptorSet[s_maxDescriptorSetLayouts];
    DescriptorSetLayoutHandle descriptorSetLayoutHandle[s_maxDescriptorSetLayouts];
    u32 numActiveLayouts = 0;

    DepthStencilStateCreation depthStencil;
    BlendStateCreation blendState;
    RasterizationCreation rasterization;

    PipelineHandle handle;
    bool graphicsPipeline = true;
};

struct RenderPass{
    VkRenderPass vkRenderPass;
    VkFramebuffer vkFrameBuffer;

    RenderPassOutput output;

    TextureHandle outputTextures[s_maxImageOutputs];
    TextureHandle outputDepthStencil;

    RenderPassType::Enum type;

    f32 scaleX = 1;
    f32 scaleY = 1;
    u16 width = 0;
    u16 height = 0;
    u16 dispatchX = 0;
    u16 dispatchY = 0;
    u16 dispatchZ = 0;

    u8 resize = 0;
    u8 numRenderTargets = 0;

    const char* name = nullptr;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

