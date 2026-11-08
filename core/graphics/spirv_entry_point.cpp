// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "spirv_entry_point.h"

#include "spirv_model.adhd.h"

#include <global/bit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_spirv_entry_point{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u16 s_OpEntryPoint = NWB::Core::s_SpirvDataOpEntryPoint;
inline constexpr u32 s_SpirvMagic = NWB::Core::s_SpirvDataSpirvMagic;
inline constexpr usize s_SpirvHeaderWords = NWB::Core::s_SpirvDataSpirvHeaderWords;
inline constexpr usize s_SpirvMagicWordIndex = NWB::Core::s_SpirvDataSpirvMagicWordIndex;
inline constexpr usize s_SpirvEntryPointExecutionModelWordIndex = NWB::Core::s_SpirvDataSpirvEntryPointExecutionModelWordIndex;
inline constexpr usize s_SpirvEntryPointFixedWordCount = NWB::Core::s_SpirvDataSpirvEntryPointFixedWordCount;
inline constexpr u32 s_SpirvInstructionOpcodeBitMask = NWB::Core::s_SpirvDataSpirvInstructionOpcodeBitMask;
inline constexpr u32 s_SpirvInstructionWordCountBitShift = NWB::Core::s_SpirvDataSpirvInstructionWordCountBitShift;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct SpirvEntryPointInstruction{
    AStringView name;
    ShaderType::Mask shaderType = ShaderType::None;
};

struct SpirvWordSource{
    Span<const u32> words;


public:
    [[nodiscard]] b8 valid()const noexcept{ return words.data() != nullptr; }

    [[nodiscard]] usize size()const noexcept{ return words.size(); }

    [[nodiscard]] u32 word(const usize index)const noexcept{ return words[index]; }

    [[nodiscard]] const u8* wordBytes(const usize index)const noexcept{
        return reinterpret_cast<const u8*>(words.data() + index);
    }
};

struct SpirvByteSource{
    BinaryByteView bytecode;


public:
    [[nodiscard]] b8 valid()const noexcept{ return bytecode.data() != nullptr && bytecode.size() % sizeof(u32) == 0u; }

    [[nodiscard]] usize size()const noexcept{ return bytecode.size() / sizeof(u32); }

    [[nodiscard]] u32 word(const usize index)const noexcept{
        const u8* const bytesPtr = wordBytes(index);
        const Array<u8, sizeof(u32)> bytes = {{ bytesPtr[0u], bytesPtr[1u], bytesPtr[2u], bytesPtr[3u] }};
        return BitCast<u32>(bytes);
    }

    [[nodiscard]] const u8* wordBytes(const usize index)const noexcept{
        return bytecode.data() + index * sizeof(u32);
    }
};

[[nodiscard]] inline AStringView SpirvNameBytes(const u8* const bytes, const usize wordCount)noexcept{
    return AStringView(reinterpret_cast<const char*>(bytes), wordCount * sizeof(u32));
}

template<typename Source>
[[nodiscard]] NWB_INLINE Expected<SpirvEntryPointInstruction> DecodeEntryPointInstruction(const Source& source, const usize instructionIndex, const u16 instructionWordCount)noexcept{
    SpirvEntryPointInstruction entryPoint;

    if(instructionWordCount <= s_SpirvEntryPointFixedWordCount)
        return MakeUnexpected(Failure{});

    entryPoint.shaderType = ResolveSpirvExecutionModel(source.word(instructionIndex + s_SpirvEntryPointExecutionModelWordIndex));

    const AStringView entryPointBytes = SpirvNameBytes(source.wordBytes(instructionIndex + s_SpirvEntryPointFixedWordCount), static_cast<usize>(instructionWordCount) - s_SpirvEntryPointFixedWordCount);
    const usize entryPointLength = entryPointBytes.find('\0');
    if(entryPointLength == AStringView::npos)
        return MakeUnexpected(Failure{});

    entryPoint.name = entryPointBytes.substr(0u, entryPointLength);
    return entryPoint;
}

template<typename Source, typename EntryPointCallback>
[[nodiscard]] NWB_INLINE b8 ScanSpirvEntryPoints(const Source& source, EntryPointCallback entryPointCallback)noexcept(noexcept(entryPointCallback(*static_cast<SpirvEntryPointInstruction*>(nullptr))) && IsNothrowDestructible_V<EntryPointCallback>){
    const usize wordCount = source.size();
    if(!source.valid() || wordCount < s_SpirvHeaderWords)
        return false;

    if(source.word(s_SpirvMagicWordIndex) != s_SpirvMagic)
        return false;

    for(usize instructionIndex = s_SpirvHeaderWords; instructionIndex < wordCount; ){
        const u32 instruction = source.word(instructionIndex);
        const u16 opcode = static_cast<u16>(instruction & s_SpirvInstructionOpcodeBitMask);
        const u16 instructionWordCount = static_cast<u16>(instruction >> s_SpirvInstructionWordCountBitShift);
        if(instructionWordCount == 0)
            return false;

        if(static_cast<usize>(instructionWordCount) > wordCount - instructionIndex)
            return false;

        if(opcode == s_OpEntryPoint){
            const auto entryPoint = DecodeEntryPointInstruction(source, instructionIndex, instructionWordCount);
            if(!entryPoint)
                return false;

            entryPointCallback(*entryPoint);
        }

        instructionIndex += instructionWordCount;
    }

    return true;
}

template<typename Source>
[[nodiscard]] NWB_INLINE Expected<AStringView, SpirvEntryPointLookupResult::Enum> ResolveEntryPointName(const Source& source, const AStringView entryName, const ShaderType::Mask shaderType)noexcept{
    AStringView entryPointName;

    if(entryName.empty() || shaderType == ShaderType::None)
        return MakeUnexpected(SpirvEntryPointLookupResult::NotFound);

    b8 found = false;
    const b8 validModule = ScanSpirvEntryPoints(source, [&](const SpirvEntryPointInstruction& entryPoint)noexcept{
        if(found || entryPoint.shaderType == ShaderType::None || entryPoint.shaderType != shaderType || entryPoint.name != entryName)
            return;

        entryPointName = entryPoint.name;
        found = true;
    });
    if(!validModule)
        return MakeUnexpected(SpirvEntryPointLookupResult::InvalidSpirv);
    if(!found)
        return MakeUnexpected(SpirvEntryPointLookupResult::NotFound);

    return entryPointName;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 IsValidSpirvModuleWords(const Span<const u32> words)noexcept{
    return __hidden_spirv_entry_point::ScanSpirvEntryPoints(__hidden_spirv_entry_point::SpirvWordSource{ words }, [](const __hidden_spirv_entry_point::SpirvEntryPointInstruction&)noexcept{});
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<AStringView, SpirvEntryPointLookupResult::Enum> ResolveSpirvEntryPointName(const Span<const u32> words, const AStringView entryName, const ShaderType::Mask shaderType)noexcept{
    return __hidden_spirv_entry_point::ResolveEntryPointName(__hidden_spirv_entry_point::SpirvWordSource{ words }, entryName, shaderType);
}

Expected<AStringView, SpirvEntryPointLookupResult::Enum> ResolveSpirvEntryPointName(const BinaryByteView bytecode, const AStringView entryName, const ShaderType::Mask shaderType)noexcept{
    return __hidden_spirv_entry_point::ResolveEntryPointName(__hidden_spirv_entry_point::SpirvByteSource{ bytecode }, entryName, shaderType);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

