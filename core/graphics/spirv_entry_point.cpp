// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "spirv_entry_point.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_spirv_entry_point{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u16 s_OpEntryPoint = 15u;
inline constexpr u32 s_SpirvMagic = 0x07230203u;
inline constexpr usize s_SpirvHeaderWords = 5u;


inline ShaderType::Mask ConvertExecutionModel(const u32 executionModel){
    switch(executionModel){
    case 0u: return ShaderType::Vertex;
    case 1u: return ShaderType::Hull;
    case 2u: return ShaderType::Domain;
    case 3u: return ShaderType::Geometry;
    case 4u: return ShaderType::Pixel;
    case 5u: return ShaderType::Compute;
    case 5267u: return ShaderType::Amplification;
    case 5268u: return ShaderType::Mesh;
    case 5313u: return ShaderType::RayGeneration;
    case 5314u: return ShaderType::Intersection;
    case 5315u: return ShaderType::AnyHit;
    case 5316u: return ShaderType::ClosestHit;
    case 5317u: return ShaderType::Miss;
    case 5318u: return ShaderType::Callable;
    case 5364u: return ShaderType::Amplification;
    case 5365u: return ShaderType::Mesh;
    default: return ShaderType::None;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SpirvEntryPointLookupResult::Enum ResolveSpirvEntryPointName(
    const u32* words,
    const usize wordCount,
    const AStringView entryName,
    const ShaderType::Mask shaderType,
    GraphicsString& outEntryPointName
){
    outEntryPointName.clear();

    if(entryName.empty() || shaderType == ShaderType::None)
        return SpirvEntryPointLookupResult::NotFound;

    if(!words || wordCount < __hidden_spirv_entry_point::s_SpirvHeaderWords)
        return SpirvEntryPointLookupResult::InvalidSpirv;

    if(words[0] != __hidden_spirv_entry_point::s_SpirvMagic)
        return SpirvEntryPointLookupResult::InvalidSpirv;

    for(usize instructionIndex = __hidden_spirv_entry_point::s_SpirvHeaderWords; instructionIndex < wordCount; ){
        const u32 instruction = words[instructionIndex];
        const u16 opcode = static_cast<u16>(instruction & 0xFFFFu);
        const u16 instructionWordCount = static_cast<u16>(instruction >> 16u);
        if(instructionWordCount == 0)
            return SpirvEntryPointLookupResult::InvalidSpirv;

        if(static_cast<usize>(instructionWordCount) > wordCount - instructionIndex)
            return SpirvEntryPointLookupResult::InvalidSpirv;

        const usize nextInstructionIndex = instructionIndex + instructionWordCount;
        if(opcode == __hidden_spirv_entry_point::s_OpEntryPoint){
            if(instructionWordCount <= 3)
                return SpirvEntryPointLookupResult::InvalidSpirv;

            const ShaderType::Mask candidateShaderType = __hidden_spirv_entry_point::ConvertExecutionModel(words[instructionIndex + 1]);
            if(candidateShaderType != ShaderType::None && candidateShaderType == shaderType){
                const auto* entryPointBytes = reinterpret_cast<const char*>(&words[instructionIndex + 3]);
                const usize entryPointMaxBytes = (instructionWordCount - 3u) * sizeof(u32);

                usize entryPointLength = 0;
                while(entryPointLength < entryPointMaxBytes && entryPointBytes[entryPointLength] != '\0')
                    ++entryPointLength;

                if(entryPointLength == entryPointMaxBytes)
                    return SpirvEntryPointLookupResult::InvalidSpirv;

                const AStringView candidateEntryPoint(entryPointBytes, entryPointLength);
                if(candidateEntryPoint == entryName){
                    outEntryPointName.assign(candidateEntryPoint.data(), candidateEntryPoint.size());
                    return SpirvEntryPointLookupResult::Found;
                }
            }
        }

        instructionIndex = nextInstructionIndex;
    }

    return SpirvEntryPointLookupResult::NotFound;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

