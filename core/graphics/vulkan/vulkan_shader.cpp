// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vulkan_backend.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Sampler


Sampler::Sampler(const VulkanContext& context)
    : m_Context(context)
{}

Sampler::~Sampler(){
    if(sampler != VK_NULL_HANDLE){
        vkDestroySampler(m_Context.device, sampler, m_Context.allocationCallbacks);
        sampler = VK_NULL_HANDLE;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Shader


Shader::Shader(const VulkanContext& context)
    : m_Context(context)
{}

Shader::~Shader(){
    if(shaderModule != VK_NULL_HANDLE){
        vkDestroyShaderModule(m_Context.device, shaderModule, m_Context.allocationCallbacks);
        shaderModule = VK_NULL_HANDLE;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Shader Library


ShaderLibrary::ShaderLibrary(const VulkanContext& context)
    : m_Context(context)
{}

ShaderLibrary::~ShaderLibrary(){}

void ShaderLibrary::getBytecode(const void** ppBytecode, usize* pSize)const{
    *ppBytecode = bytecode.data();
    *pSize = bytecode.size();
}

ShaderHandle ShaderLibrary::getShader(const Name& entryName, ShaderType::Mask shaderType){
    auto it = shaders.find(entryName);
    if(it != shaders.end())
        return ShaderHandle(it->second.get());
    return nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Device - Shader Implementation


ShaderHandle Device::createShader(const ShaderDesc& d, const void* binary, usize binarySize){
    Shader* shader = new Shader(m_Context);
    shader->desc = d;
    shader->bytecode.assign(static_cast<const u8*>(binary), static_cast<const u8*>(binary) + binarySize);
    
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = binarySize;
    createInfo.pCode = reinterpret_cast<const u32*>(binary);
    
    VkResult res = vkCreateShaderModule(m_Context.device, &createInfo, m_Context.allocationCallbacks, &shader->shaderModule);
    assert(res == VK_SUCCESS);
    
    return RefCountPtr<IShader, BlankDeleter<IShader>>(shader, AdoptRef);
}

ShaderHandle Device::createShaderSpecialization(IShader* baseShader, const ShaderSpecialization* constants, u32 numConstants){
    // TODO: Implement shader specialization
    return nullptr;
}

ShaderLibraryHandle Device::createShaderLibrary(const void* binary, usize binarySize){
    // TODO: Implement shader library
    return nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Device - Input Layout Implementation


InputLayoutHandle Device::createInputLayout(const VertexAttributeDesc* d, u32 attributeCount, IShader* vertexShader){
    InputLayout* layout = new InputLayout();
    layout->attributes.assign(d, d + attributeCount);
    
    // Group by buffer index
    HashMap<u32, u32> bufferStrides;
    for(const auto& attr : layout->attributes){
        u32 stride = attr.elementStride;
        if(stride == 0){
            const FormatInfo& formatInfo = GetFormatInfo(attr.format);
            stride = formatInfo.bytesPerBlock * attr.arraySize;
        }
        
        if(bufferStrides.find(attr.bufferIndex) == bufferStrides.end())
            bufferStrides[attr.bufferIndex] = stride;
        else
            bufferStrides[attr.bufferIndex] = max(bufferStrides[attr.bufferIndex], stride);
    }
    
    // Create binding descriptions
    for(const auto& pair : bufferStrides){
        VkVertexInputBindingDescription binding{};
        binding.binding = pair.first;
        binding.stride = pair.second;
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        layout->bindings.push_back(binding);
    }
    
    // Create attribute descriptions
    for(u32 i = 0; i < attributeCount; i++){
        const auto& attr = layout->attributes[i];
        
        VkVertexInputAttributeDescription vkAttr{};
        vkAttr.location = i;
        vkAttr.binding = attr.bufferIndex;
        vkAttr.format = convertFormat(attr.format);
        vkAttr.offset = attr.offset;
        
        layout->vkAttributes.push_back(vkAttr);
    }
    
    return RefCountPtr<IInputLayout, BlankDeleter<IInputLayout>>(layout, AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Framebuffer


Framebuffer::Framebuffer(const VulkanContext& context)
    : m_Context(context)
{}

Framebuffer::~Framebuffer(){
    if(framebuffer != VK_NULL_HANDLE){
        vkDestroyFramebuffer(m_Context.device, framebuffer, m_Context.allocationCallbacks);
        framebuffer = VK_NULL_HANDLE;
    }
    
    if(renderPass != VK_NULL_HANDLE){
        vkDestroyRenderPass(m_Context.device, renderPass, m_Context.allocationCallbacks);
        renderPass = VK_NULL_HANDLE;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Event Query


EventQuery::EventQuery(const VulkanContext& context)
    : m_Context(context)
{
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    
    VkResult res = vkCreateFence(m_Context.device, &fenceInfo, m_Context.allocationCallbacks, &fence);
    assert(res == VK_SUCCESS);
}

EventQuery::~EventQuery(){
    if(fence != VK_NULL_HANDLE){
        vkDestroyFence(m_Context.device, fence, m_Context.allocationCallbacks);
        fence = VK_NULL_HANDLE;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Timer Query


TimerQuery::TimerQuery(const VulkanContext& context)
    : m_Context(context)
{
    VkQueryPoolCreateInfo queryPoolInfo{};
    queryPoolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    queryPoolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
    queryPoolInfo.queryCount = 2; // Start and end timestamps
    
    VkResult res = vkCreateQueryPool(m_Context.device, &queryPoolInfo, m_Context.allocationCallbacks, &queryPool);
    assert(res == VK_SUCCESS);
}

TimerQuery::~TimerQuery(){
    if(queryPool != VK_NULL_HANDLE){
        vkDestroyQueryPool(m_Context.device, queryPool, m_Context.allocationCallbacks);
        queryPool = VK_NULL_HANDLE;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
