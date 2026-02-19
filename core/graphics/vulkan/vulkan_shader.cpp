// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vulkan_backend.h"

#include <logger/client/logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Sampler::Sampler(const VulkanContext& context)
    : m_context(context)
{}
Sampler::~Sampler(){
    if(sampler != VK_NULL_HANDLE){
        vkDestroySampler(m_context.device, sampler, m_context.allocationCallbacks);
        sampler = VK_NULL_HANDLE;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Shader::Shader(const VulkanContext& context)
    : m_context(context)
{}
Shader::~Shader(){
    if(shaderModule != VK_NULL_HANDLE){
        vkDestroyShaderModule(m_context.device, shaderModule, m_context.allocationCallbacks);
        shaderModule = VK_NULL_HANDLE;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ShaderLibrary::ShaderLibrary(const VulkanContext& context)
    : m_context(context)
{}
ShaderLibrary::~ShaderLibrary(){}

void ShaderLibrary::getBytecode(const void** ppBytecode, usize* pSize)const{
    *ppBytecode = bytecode.data();
    *pSize = bytecode.size();
}

ShaderHandle ShaderLibrary::getShader(const Name& entryName, ShaderType::Mask shaderType){
    VkResult res = VK_SUCCESS;

    auto it = shaders.find(entryName);
    if(it != shaders.end())
        return ShaderHandle(it->second.get());

    Shader* shader = new Shader(m_context);
    shader->desc.shaderType = shaderType;
    shader->desc.entryName = entryName;
    shader->bytecode = bytecode;

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = bytecode.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(bytecode.data());

    res = vkCreateShaderModule(m_context.device, &createInfo, m_context.allocationCallbacks, &shader->shaderModule);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create shader module for entry '{}': {}"), StringConvert(entryName.c_str()), ResultToString(res));
        delete shader;
        return nullptr;
    }

    shaders[entryName] = RefCountPtr<Shader, ArenaRefDeleter<Shader>>(shader);
    return ShaderHandle(shader);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ShaderHandle Device::createShader(const ShaderDesc& d, const void* binary, usize binarySize){
    VkResult res = VK_SUCCESS;

    auto* shader = new Shader(m_context);
    shader->desc = d;
    shader->bytecode.assign(static_cast<const u8*>(binary), static_cast<const u8*>(binary) + binarySize);

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = binarySize;
    createInfo.pCode = reinterpret_cast<const uint32_t*>(binary);

    res = vkCreateShaderModule(m_context.device, &createInfo, m_context.allocationCallbacks, &shader->shaderModule);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create shader module: {}"), ResultToString(res));
        delete shader;
        return nullptr;
    }

    return RefCountPtr<IShader, ArenaRefDeleter<IShader>>(shader, AdoptRef);
}

ShaderHandle Device::createShaderSpecialization(IShader* baseShader, const ShaderSpecialization* constants, u32 numConstants){
    VkResult res = VK_SUCCESS;

    if(!baseShader)
        return nullptr;

    auto* base = static_cast<Shader*>(baseShader);
    auto* shader = new Shader(m_context);
    shader->desc = base->desc;
    shader->bytecode = base->bytecode;

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = shader->bytecode.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(shader->bytecode.data());

    res = vkCreateShaderModule(m_context.device, &createInfo, m_context.allocationCallbacks, &shader->shaderModule);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create shader module for specialization: {}"), ResultToString(res));
        delete shader;
        return nullptr;
    }

    if(constants && numConstants > 0){
        shader->specializationData.resize(numConstants * sizeof(u32));
        shader->specializationEntries.resize(numConstants);

        for(u32 i = 0; i < numConstants; ++i){
            VkSpecializationMapEntry& entry = shader->specializationEntries[i];
            entry.constantID = constants[i].constantID;
            entry.offset = i * sizeof(u32);
            entry.size = sizeof(u32);

            NWB_MEMCPY(shader->specializationData.data() + entry.offset, sizeof(u32), &constants[i].value, sizeof(u32));
        }
    }

    return RefCountPtr<IShader, ArenaRefDeleter<IShader>>(shader, AdoptRef);
}

ShaderLibraryHandle Device::createShaderLibrary(const void* binary, usize binarySize){
    auto* lib = new ShaderLibrary(m_context);
    lib->bytecode.assign(static_cast<const u8*>(binary), static_cast<const u8*>(binary) + binarySize);

    return RefCountPtr<IShaderLibrary, ArenaRefDeleter<IShaderLibrary>>(lib, AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


InputLayoutHandle Device::createInputLayout(const VertexAttributeDesc* d, u32 attributeCount, IShader*){
    auto* layout = new InputLayout();
    layout->attributes.assign(d, d + attributeCount);

    HashMap<u32, u32> bufferStrides;
    HashMap<u32, bool> bufferInstanced;
    for(const auto& attr : layout->attributes){
        u32 stride = attr.elementStride;
        if(stride == 0){
            const FormatInfo& formatInfo = GetFormatInfo(attr.format);
            stride = formatInfo.bytesPerBlock * attr.arraySize;
        }

        if(bufferStrides.find(attr.bufferIndex) == bufferStrides.end()){
            bufferStrides[attr.bufferIndex] = stride;
            bufferInstanced[attr.bufferIndex] = attr.isInstanced;
        }
        else
            bufferStrides[attr.bufferIndex] = Max(bufferStrides[attr.bufferIndex], stride);
    }

    for(const auto& pair : bufferStrides){
        VkVertexInputBindingDescription binding{};
        binding.binding = pair.first;
        binding.stride = pair.second;
        binding.inputRate = bufferInstanced[pair.first] ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX;
        layout->bindings.push_back(binding);
    }

    for(u32 i = 0; i < attributeCount; ++i){
        const auto& attr = layout->attributes[i];

        VkVertexInputAttributeDescription vkAttr{};
        vkAttr.location = i;
        vkAttr.binding = attr.bufferIndex;
        vkAttr.format = ConvertFormat(attr.format);
        vkAttr.offset = attr.offset;

        layout->vkAttributes.push_back(vkAttr);
    }

    return RefCountPtr<IInputLayout, ArenaRefDeleter<IInputLayout>>(layout, AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Framebuffer::Framebuffer(const VulkanContext& context)
    : m_context(context)
{}
Framebuffer::~Framebuffer(){
    if(framebuffer != VK_NULL_HANDLE){
        vkDestroyFramebuffer(m_context.device, framebuffer, m_context.allocationCallbacks);
        framebuffer = VK_NULL_HANDLE;
    }

    if(renderPass != VK_NULL_HANDLE){
        vkDestroyRenderPass(m_context.device, renderPass, m_context.allocationCallbacks);
        renderPass = VK_NULL_HANDLE;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


EventQuery::EventQuery(const VulkanContext& context)
    : m_context(context)
{
    VkResult res = VK_SUCCESS;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

    res = vkCreateFence(m_context.device, &fenceInfo, m_context.allocationCallbacks, &fence);
    NWB_ASSERT_MSG(res == VK_SUCCESS, NWB_TEXT("Vulkan: Failed to create fence for EventQuery"));
}
EventQuery::~EventQuery(){
    if(fence != VK_NULL_HANDLE){
        vkDestroyFence(m_context.device, fence, m_context.allocationCallbacks);
        fence = VK_NULL_HANDLE;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TimerQuery::TimerQuery(const VulkanContext& context)
    : m_context(context)
{
    VkResult res = VK_SUCCESS;

    VkQueryPoolCreateInfo queryPoolInfo{};
    queryPoolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    queryPoolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
    queryPoolInfo.queryCount = 2; // Start and end timestamps

    res = vkCreateQueryPool(m_context.device, &queryPoolInfo, m_context.allocationCallbacks, &queryPool);
    NWB_ASSERT_MSG(res == VK_SUCCESS, NWB_TEXT("Vulkan: Failed to create query pool for TimerQuery"));
}
TimerQuery::~TimerQuery(){
    if(queryPool != VK_NULL_HANDLE){
        vkDestroyQueryPool(m_context.device, queryPool, m_context.allocationCallbacks);
        queryPool = VK_NULL_HANDLE;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

