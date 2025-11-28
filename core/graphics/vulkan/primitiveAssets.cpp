// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "primitiveAssets.h"

#if defined(NWB_PLATFORM_WINDOWS)
#include <spirv-headers/spirv.h>
#else
#include <spirv_cross/spirv.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct SpirVMember{
    u32 idIndex = 0;
    u32 offset = 0;

    AStringView name;
};

struct SpirVId{
    SpvOp op = SpvOpNop;
    u32 set = 0;
    u32 binding = 0;

    // for integer, float
    u8 width = 0;
    u8 sign = 0;

    // for arrays, vectors, matrices
    u32 typeIndex = 0;
    u32 count = 0;

    // for variables
    SpvStorageClass storageClass = SpvStorageClassUniformConstant;

    // for constants
    u32 value = 0;

    // for structs
    AStringView name;
    CustomUniquePtr<SpirVMember[]> members;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool SpirVParseResult::parse(const void* rawData, usize size, Alloc::CustomArena& arena){
    NWB_ASSERT(!(size % 4));

    u32 numWord = static_cast<u32>(size / 4);
    const u32* data = static_cast<const u32*>(rawData);

    NWB_ASSERT(data[0] == 0x07230203);

    const u32 idBound = data[3];
    CustomUniquePtr<SpirVId[]> ids = MakeCustomUnique<SpirVId[]>(arena, idBound);
    
    VkShaderStageFlags stage = 0;

    usize wordIndex = 5;
    while(wordIndex < numWord){
        SpvOp op = static_cast<decltype(op)>(data[wordIndex] & 0xff);
        u16 wordCount = static_cast<decltype(wordCount)>(data[wordIndex] >> 16);

        switch(op){
        case SpvOpEntryPoint:
        {
            NWB_ASSERT(wordCount >= 4);

            SpvExecutionModel model = static_cast<decltype(model)>(data[wordIndex + 1]);
            switch(model){
            case SpvExecutionModelVertex:
                stage = VK_SHADER_STAGE_VERTEX_BIT;
                break;
            case SpvExecutionModelGeometry:
                stage = VK_SHADER_STAGE_GEOMETRY_BIT;
                break;
            case SpvExecutionModelFragment:
                stage = VK_SHADER_STAGE_FRAGMENT_BIT;
                break;
            case SpvExecutionModelKernel:
                stage = VK_SHADER_STAGE_COMPUTE_BIT;
                break;
            }
            NWB_ASSERT(stage != 0);
        }
        break;

        case SpvOpDecorate:
        {
            NWB_ASSERT(wordCount >= 3);

            u32 idIndex = data[wordIndex + 1];
            NWB_ASSERT(idIndex < idBound);

            SpirVId& id = ids[idIndex];

            SpvDecoration decoration = static_cast<decltype(decoration)>(data[wordIndex + 2]);
            switch(decoration){
            case SpvDecorationBinding:
                id.binding = data[wordIndex + 3];
                break;
            case SpvDecorationDescriptorSet:
                id.set = data[wordIndex + 3];
                break;
            }
        }
        break;

        case SpvOpMemberDecorate:
        {
            NWB_ASSERT(wordCount >= 4);

            u32 idIndex = data[wordIndex + 1];
            NWB_ASSERT(idIndex < idBound);

            SpirVId& id = ids[idIndex];

            u32 memberIndex = data[wordIndex + 2];

            if(!id.members)
                id.members = MakeCustomUnique<SpirVMember[]>(arena, 64);

            SpirVMember& member = id.members[memberIndex];

            SpvDecoration decoration = static_cast<decltype(decoration)>(data[wordIndex + 3]);
            switch(decoration){
            case SpvDecorationOffset:
                member.offset = data[wordIndex + 4];
                break;
            }
        }
        break;

        case SpvOpName:
        {
            NWB_ASSERT(wordCount >= 3);

            u32 idIndex = data[wordIndex + 1];
            NWB_ASSERT(idIndex < idBound);

            SpirVId& id = ids[idIndex];

            char* name = nullptr;
            {
                const auto* raw = reinterpret_cast<const char*>(data + (wordIndex + 2));
                const usize len = NWB_STRLEN(raw);

                name = arena.allocate<char>(len + 1);
                NWB_STRNCPY(name, len, raw, len);
                name[len] = 0;
            }
            id.name = name;
        }
        break;

        case SpvOpMemberName:
        {
            NWB_ASSERT(wordCount >= 4);

            u32 idIndex = data[wordIndex + 1];
            NWB_ASSERT(idIndex < idBound);

            SpirVId& id = ids[idIndex];

            u32 memberIndex = data[wordIndex + 2];

            if(!id.members)
                id.members = MakeCustomUnique<SpirVMember[]>(arena, 64);

            SpirVMember& member = id.members[memberIndex];

            char* name = nullptr;
            {
                const auto* raw = reinterpret_cast<const char*>(data + (wordIndex + 3));
                const usize len = NWB_STRLEN(raw);
                name = arena.allocate<char>(len + 1);
                NWB_STRNCPY(name, len, raw, len);
                name[len] = 0;
            }

            member.name = name;
        }
        break;

        case SpvOpTypeInt:
        {
            NWB_ASSERT(wordCount == 4);

            u32 idIndex = data[wordIndex + 1];
            NWB_ASSERT(idIndex < idBound);

            SpirVId& id = ids[idIndex];
            id.op = op;
            id.width = static_cast<decltype(id.width)>(data[wordIndex + 2]);
            id.sign = static_cast<decltype(id.sign)>(data[wordIndex + 3]);
        }
        break;

        case SpvOpTypeFloat:
        {
            NWB_ASSERT(wordCount == 3);

            u32 idIndex = data[wordIndex + 1];
            NWB_ASSERT(idIndex < idBound);

            SpirVId& id = ids[idIndex];
            id.op = op;
            id.width = static_cast<decltype(id.width)>(data[wordIndex + 2]);
        }
        break;

        case SpvOpTypeVector:
        {
            NWB_ASSERT(wordCount == 4);

            u32 idIndex = data[wordIndex + 1];
            NWB_ASSERT(idIndex < idBound);

            SpirVId& id = ids[idIndex];
            id.op = op;
            id.typeIndex = data[wordIndex + 2];
            id.count = data[wordIndex + 3];
        }
        break;

        case SpvOpTypeMatrix:
        {
            NWB_ASSERT(wordCount == 4);

            u32 idIndex = data[wordIndex + 1];
            NWB_ASSERT(idIndex < idBound);

            SpirVId& id = ids[idIndex];
            id.op = op;
            id.typeIndex = data[wordIndex + 2];
            id.count = data[wordIndex + 3];
        }
        break;

        case SpvOpTypeImage:
        {
            NWB_ASSERT(wordCount >= 9);
            // not implemented yet
        }
        break;

        case SpvOpTypeSampler:
        {
            NWB_ASSERT(wordCount == 2);
            
            u32 idIndex = data[wordIndex + 1];
            NWB_ASSERT(idIndex < idBound);

            SpirVId& id = ids[idIndex];
            id.op = op;
        }
        break;

        case SpvOpTypeSampledImage:
        {
            NWB_ASSERT(wordCount == 3);
            
            u32 idIndex = data[wordIndex + 1];
            NWB_ASSERT(idIndex < idBound);

            SpirVId& id = ids[idIndex];
            id.op = op;
        }
        break;

        case SpvOpTypeArray:
        {
            NWB_ASSERT(wordCount == 4);

            u32 idIndex = data[wordIndex + 1];
            NWB_ASSERT(idIndex < idBound);

            SpirVId& id = ids[idIndex];
            id.op = op;
            id.typeIndex = data[wordIndex + 2];
            id.count = data[wordIndex + 3];
        }
        break;

        case SpvOpTypeRuntimeArray:
        {
            NWB_ASSERT(wordCount == 3);

            u32 idIndex = data[wordIndex + 1];
            NWB_ASSERT(idIndex < idBound);

            SpirVId& id = ids[idIndex];
            id.op = op;
            id.typeIndex = data[wordIndex + 2];
        }
        break;

        case SpvOpTypeStruct:
        {
            NWB_ASSERT(wordCount >= 2);

            u32 idIndex = data[wordIndex + 1];
            NWB_ASSERT(idIndex < idBound);

            SpirVId& id = ids[idIndex];
            id.op = op;

            if(wordCount > 2){
                for(u16 memberIndex = 0; memberIndex < wordCount - 2; ++memberIndex)
                    id.members[memberIndex].idIndex = data[wordIndex + memberIndex + 2];
            }
        }
        break;

        case SpvOpTypePointer:
        {
            NWB_ASSERT(wordCount == 4);

            u32 idIndex = data[wordIndex + 1];
            NWB_ASSERT(idIndex < idBound);

            SpirVId& id = ids[idIndex];
            id.op = op;
            id.typeIndex = data[wordIndex + 3];
        }
        break;

        case SpvOpConstant:
        {
            NWB_ASSERT(wordCount >= 4);

            u32 idIndex = data[wordIndex + 1];
            NWB_ASSERT(idIndex < idBound);

            SpirVId& id = ids[idIndex];
            id.op = op;
            id.typeIndex = data[wordIndex + 2];
            id.value = data[wordIndex + 3]; // assume all constants are 32-bit for now
        }
        break;

        case SpvOpVariable:
        {
            NWB_ASSERT(wordCount == 4);

            u32 idIndex = data[wordIndex + 1];
            NWB_ASSERT(idIndex < idBound);

            SpirVId& id = ids[idIndex];
            id.op = op;
            id.typeIndex = data[wordIndex + 1];
            id.storageClass = static_cast<decltype(id.storageClass)>(data[wordIndex + 3]);
        }
        break;
        }

        wordIndex += wordCount;
    }

    for(u32 idIndex = 0; idIndex < idBound; ++idIndex){
        SpirVId& id = ids[idIndex];

        if(id.op == SpvOpVariable){
            switch(id.storageClass){
            case SpvStorageClassUniform:
            case SpvStorageClassUniformConstant:
            {
                if(
                    (id.set == 1)
                    && ((id.binding == s_spirBindlessTextureBinding) || (id.binding == s_spirBindlessTextureBinding + 1))
                    )
                {
                    // these are managed by the GPU
                    continue;
                }

                SpirVId& uniformType = ids[ids[id.typeIndex].typeIndex];

                DescriptorSetLayoutCreation& setLayout = sets[id.set];
                setLayout.setIndex(id.set);

                DescriptorSetLayoutCreation::Binding binding{};
                {
                    binding.index = static_cast<decltype(binding.index)>(id.binding);
                    binding.count = 1;
                }

                switch(uniformType.op){
                case SpvOpTypeStruct:
                {
                    binding.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                    binding.name = uniformType.name.data();
                }
                break;

                case SpvOpTypeSampledImage:
                {
                    binding.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                    binding.name = uniformType.name.data();
                }
                break;
                }

                setLayout.addBindingAtIndex(binding, binding.index);
                
                if(numSets < id.set + 1)
                    numSets = id.set + 1;
            }
            break;
            }
        }

        id.members.release();
    }

    ids.release();
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

