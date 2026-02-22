// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vulkan_backend.h"

#include <logger/client/logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Sampler::Sampler(const VulkanContext& context)
    : RefCounter<ISampler>(*context.threadPool)
    , m_context(context)
{}
Sampler::~Sampler(){
    if(m_sampler != VK_NULL_HANDLE){
        vkDestroySampler(m_context.device, m_sampler, m_context.allocationCallbacks);
        m_sampler = VK_NULL_HANDLE;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Shader::Shader(const VulkanContext& context)
    : RefCounter<IShader>(*context.threadPool)
    , m_context(context)
    , m_bytecode(Alloc::CustomAllocator<u8>(*context.objectArena))
    , m_specializationEntries(Alloc::CustomAllocator<VkSpecializationMapEntry>(*context.objectArena))
    , m_specializationData(Alloc::CustomAllocator<u8>(*context.objectArena))
{}
Shader::~Shader(){
    if(m_shaderModule != VK_NULL_HANDLE){
        vkDestroyShaderModule(m_context.device, m_shaderModule, m_context.allocationCallbacks);
        m_shaderModule = VK_NULL_HANDLE;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ShaderLibrary::ShaderLibrary(const VulkanContext& context)
    : RefCounter<IShaderLibrary>(*context.threadPool)
    , m_context(context)
    , m_bytecode(Alloc::CustomAllocator<u8>(*context.objectArena))
    , m_shaders(0, Hasher<Name>(), EqualTo<Name>(), Alloc::CustomAllocator<Pair<const Name, RefCountPtr<Shader, ArenaRefDeleter<Shader>>>>(*context.objectArena))
{}
ShaderLibrary::~ShaderLibrary(){}

void ShaderLibrary::getBytecode(const void** ppBytecode, usize* pSize)const{
    *ppBytecode = m_bytecode.data();
    *pSize = m_bytecode.size();
}

ShaderHandle ShaderLibrary::getShader(const Name& entryName, ShaderType::Mask shaderType){
    VkResult res = VK_SUCCESS;

    auto it = m_shaders.find(entryName);
    if(it != m_shaders.end())
        return ShaderHandle(it->second.get(), ShaderHandle::deleter_type(m_context.objectArena));

    Shader* shader = NewArenaObject<Shader>(*m_context.objectArena, m_context);
    shader->m_desc.shaderType = shaderType;
    shader->m_desc.entryName = entryName;
    shader->m_bytecode = m_bytecode;

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = m_bytecode.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(m_bytecode.data());

    res = vkCreateShaderModule(m_context.device, &createInfo, m_context.allocationCallbacks, &shader->m_shaderModule);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create shader module for entry '{}': {}"), StringConvert(entryName.c_str()), ResultToString(res));
        DestroyArenaObject(*m_context.objectArena, shader);
        return nullptr;
    }

    m_shaders[entryName] = RefCountPtr<Shader, ArenaRefDeleter<Shader>>(shader, ArenaRefDeleter<Shader>(m_context.objectArena));
    return ShaderHandle(shader, ShaderHandle::deleter_type(m_context.objectArena));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ShaderHandle Device::createShader(const ShaderDesc& d, const void* binary, usize binarySize){
    VkResult res = VK_SUCCESS;

    auto* shader = NewArenaObject<Shader>(*m_context.objectArena, m_context);
    shader->m_desc = d;
    shader->m_bytecode.assign(static_cast<const u8*>(binary), static_cast<const u8*>(binary) + binarySize);

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = binarySize;
    createInfo.pCode = reinterpret_cast<const uint32_t*>(binary);

    res = vkCreateShaderModule(m_context.device, &createInfo, m_context.allocationCallbacks, &shader->m_shaderModule);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create shader module: {}"), ResultToString(res));
        DestroyArenaObject(*m_context.objectArena, shader);
        return nullptr;
    }

    return ShaderHandle(shader, ShaderHandle::deleter_type(m_context.objectArena), AdoptRef);
}

ShaderHandle Device::createShaderSpecialization(IShader* baseShader, const ShaderSpecialization* constants, u32 numConstants){
    VkResult res = VK_SUCCESS;

    if(!baseShader)
        return nullptr;

    auto* base = static_cast<Shader*>(baseShader);
    auto* shader = NewArenaObject<Shader>(*m_context.objectArena, m_context);
    shader->m_desc = base->m_desc;
    shader->m_bytecode = base->m_bytecode;

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = shader->m_bytecode.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(shader->m_bytecode.data());

    res = vkCreateShaderModule(m_context.device, &createInfo, m_context.allocationCallbacks, &shader->m_shaderModule);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create shader module for specialization: {}"), ResultToString(res));
        DestroyArenaObject(*m_context.objectArena, shader);
        return nullptr;
    }

    if(constants && numConstants > 0){
        shader->m_specializationData.resize(numConstants * sizeof(u32));
        shader->m_specializationEntries.resize(numConstants);

        auto fillConstant = [&](usize i){
            VkSpecializationMapEntry& entry = shader->m_specializationEntries[i];
            entry.constantID = constants[i].constantID;
            entry.offset = static_cast<u32>(i * sizeof(u32));
            entry.size = sizeof(u32);

            NWB_MEMCPY(shader->m_specializationData.data() + entry.offset, sizeof(u32), &constants[i].value, sizeof(u32));
        };

        constexpr usize kParallelSpecializationThreshold = 256;
        if(m_context.threadPool->isParallelEnabled() && numConstants >= kParallelSpecializationThreshold)
            m_context.threadPool->parallelFor(static_cast<usize>(0), numConstants, fillConstant);
        else{
            for(usize i = 0; i < numConstants; ++i)
                fillConstant(i);
        }
    }

    return ShaderHandle(shader, ShaderHandle::deleter_type(m_context.objectArena), AdoptRef);
}

ShaderLibraryHandle Device::createShaderLibrary(const void* binary, usize binarySize){
    auto* lib = NewArenaObject<ShaderLibrary>(*m_context.objectArena, m_context);
    lib->m_bytecode.assign(static_cast<const u8*>(binary), static_cast<const u8*>(binary) + binarySize);

    return ShaderLibraryHandle(lib, ShaderLibraryHandle::deleter_type(m_context.objectArena), AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


InputLayout::InputLayout(const VulkanContext& context)
    : RefCounter<IInputLayout>(*context.threadPool)
    , m_context(context)
    , m_attributes(Alloc::CustomAllocator<VertexAttributeDesc>(*context.objectArena))
    , m_bindings(Alloc::CustomAllocator<VkVertexInputBindingDescription>(*context.objectArena))
    , m_vkAttributes(Alloc::CustomAllocator<VkVertexInputAttributeDescription>(*context.objectArena))
{}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


InputLayoutHandle Device::createInputLayout(const VertexAttributeDesc* d, u32 attributeCount, IShader*){
    auto* layout = NewArenaObject<InputLayout>(*m_context.objectArena, m_context);
    layout->m_attributes.assign(d, d + attributeCount);

    Alloc::ScratchArena<> scratchArena;

    HashMap<u32, u32, Hasher<u32>, EqualTo<u32>, Alloc::ScratchAllocator<Pair<const u32, u32>>> bufferStrides(0, Hasher<u32>(), EqualTo<u32>(), Alloc::ScratchAllocator<Pair<const u32, u32>>(scratchArena));
    HashMap<u32, bool, Hasher<u32>, EqualTo<u32>, Alloc::ScratchAllocator<Pair<const u32, bool>>> bufferInstanced(0, Hasher<u32>(), EqualTo<u32>(), Alloc::ScratchAllocator<Pair<const u32, bool>>(scratchArena));
    for(const auto& attr : layout->m_attributes){
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
        layout->m_bindings.push_back(binding);
    }

    for(u32 i = 0; i < attributeCount; ++i){
        const auto& attr = layout->m_attributes[i];

        VkVertexInputAttributeDescription vkAttr{};
        vkAttr.location = i;
        vkAttr.binding = attr.bufferIndex;
        vkAttr.format = ConvertFormat(attr.format);
        vkAttr.offset = attr.offset;

        layout->m_vkAttributes.push_back(vkAttr);
    }

    return InputLayoutHandle(layout, InputLayoutHandle::deleter_type(m_context.objectArena), AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Framebuffer::Framebuffer(const VulkanContext& context)
    : RefCounter<IFramebuffer>(*context.threadPool)
    , m_context(context)
    , m_resources(Alloc::CustomAllocator<RefCountPtr<ITexture, ArenaRefDeleter<ITexture>>>(*context.objectArena))
{}
Framebuffer::~Framebuffer(){
    if(m_framebuffer != VK_NULL_HANDLE){
        vkDestroyFramebuffer(m_context.device, m_framebuffer, m_context.allocationCallbacks);
        m_framebuffer = VK_NULL_HANDLE;
    }

    if(m_renderPass != VK_NULL_HANDLE){
        vkDestroyRenderPass(m_context.device, m_renderPass, m_context.allocationCallbacks);
        m_renderPass = VK_NULL_HANDLE;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


EventQuery::EventQuery(const VulkanContext& context)
    : RefCounter<IEventQuery>(*context.threadPool)
    , m_context(context)
{
    VkResult res = VK_SUCCESS;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

    res = vkCreateFence(m_context.device, &fenceInfo, m_context.allocationCallbacks, &m_fence);
    NWB_ASSERT_MSG(res == VK_SUCCESS, NWB_TEXT("Vulkan: Failed to create fence for EventQuery"));
}
EventQuery::~EventQuery(){
    if(m_fence != VK_NULL_HANDLE){
        vkDestroyFence(m_context.device, m_fence, m_context.allocationCallbacks);
        m_fence = VK_NULL_HANDLE;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TimerQuery::TimerQuery(const VulkanContext& context)
    : RefCounter<ITimerQuery>(*context.threadPool)
    , m_context(context)
{
    VkResult res = VK_SUCCESS;

    VkQueryPoolCreateInfo queryPoolInfo{};
    queryPoolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    queryPoolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
    queryPoolInfo.queryCount = 2; // Start and end timestamps

    res = vkCreateQueryPool(m_context.device, &queryPoolInfo, m_context.allocationCallbacks, &m_queryPool);
    NWB_ASSERT_MSG(res == VK_SUCCESS, NWB_TEXT("Vulkan: Failed to create query pool for TimerQuery"));
}
TimerQuery::~TimerQuery(){
    if(m_queryPool != VK_NULL_HANDLE){
        vkDestroyQueryPool(m_context.device, m_queryPool, m_context.allocationCallbacks);
        m_queryPool = VK_NULL_HANDLE;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

