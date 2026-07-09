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

struct SpirvEntryPointInstruction{
    ShaderType::Mask shaderType = ShaderType::None;
    AStringView name;
};


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

[[nodiscard]] inline bool DecodeEntryPointInstruction(
    const u32* instructionWords,
    const u16 instructionWordCount,
    SpirvEntryPointInstruction& outEntryPoint
){
    outEntryPoint = SpirvEntryPointInstruction();

    if(instructionWordCount <= 3)
        return false;

    outEntryPoint.shaderType = ConvertExecutionModel(instructionWords[1]);

    const auto* entryPointBytes = reinterpret_cast<const char*>(&instructionWords[3]);
    const usize entryPointMaxBytes = (static_cast<usize>(instructionWordCount) - 3u) * sizeof(u32);

    usize entryPointLength = 0;
    while(entryPointLength < entryPointMaxBytes && entryPointBytes[entryPointLength] != '\0')
        ++entryPointLength;

    if(entryPointLength == entryPointMaxBytes)
        return false;

    outEntryPoint.name = AStringView(entryPointBytes, entryPointLength);
    return true;
}

template<typename EntryPointCallback>
[[nodiscard]] bool ScanSpirvEntryPoints(
    const u32* words,
    const usize wordCount,
    EntryPointCallback entryPointCallback
){
    if(!words || wordCount < s_SpirvHeaderWords)
        return false;

    if(words[0] != s_SpirvMagic)
        return false;

    for(usize instructionIndex = s_SpirvHeaderWords; instructionIndex < wordCount; ){
        const u32 instruction = words[instructionIndex];
        const u16 opcode = static_cast<u16>(instruction & 0xFFFFu);
        const u16 instructionWordCount = static_cast<u16>(instruction >> 16u);
        if(instructionWordCount == 0)
            return false;

        if(static_cast<usize>(instructionWordCount) > wordCount - instructionIndex)
            return false;

        if(opcode == s_OpEntryPoint){
            SpirvEntryPointInstruction entryPoint;
            if(!DecodeEntryPointInstruction(words + instructionIndex, instructionWordCount, entryPoint))
                return false;

            entryPointCallback(entryPoint);
        }

        instructionIndex += instructionWordCount;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool IsValidSpirvModuleWords(
    const u32* words,
    const usize wordCount
){
    return __hidden_spirv_entry_point::ScanSpirvEntryPoints(
        words,
        wordCount,
        [](const __hidden_spirv_entry_point::SpirvEntryPointInstruction&){}
    );
}

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

    bool found = false;
    const bool validModule = __hidden_spirv_entry_point::ScanSpirvEntryPoints(
        words,
        wordCount,
        [&](const __hidden_spirv_entry_point::SpirvEntryPointInstruction& entryPoint){
            if(found || entryPoint.shaderType == ShaderType::None || entryPoint.shaderType != shaderType || entryPoint.name != entryName)
                return;

            outEntryPointName.assign(entryPoint.name.data(), entryPoint.name.size());
            found = true;
        }
    );
    if(!validModule){
        outEntryPointName.clear();
        return SpirvEntryPointLookupResult::InvalidSpirv;
    }

    return found ? SpirvEntryPointLookupResult::Found : SpirvEntryPointLookupResult::NotFound;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

