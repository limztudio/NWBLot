// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "shader_define_planner.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Alloc = Core::Alloc;
using CookString = ShaderCook::CookString;
using DefineEntry = ShaderCook::DefineEntry;
using IncludeEntry = ShaderCook::IncludeEntry;
using ShaderEntry = ShaderCook::ShaderEntry;
using DefineCombo = ShaderCook::DefineCombo;
template<typename T> using CookVector = ShaderCook::CookVector<T>;
template<typename K, typename V> using CookMap = ShaderCook::CookMap<K, V>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void ShaderDefinePlanner::mergeInheritedDefines(const Span<const Path> dependencies, const CookMap<CookString, IncludeEntry>& includeMetadata, Alloc::ScratchArena& scratchArena, ShaderEntry& inOutEntry){
    Core::Assets::ScratchVector<NotNull<const IncludeEntry*>> inheritedEntries{scratchArena};
    inheritedEntries.reserve(dependencies.size());

    usize defineReserveCount = inOutEntry.defineValues.size();
    b8 canReserveDefineValues = true;
    for(const Path& dep : dependencies){
        Core::Assets::ScratchString depKey = PathToString(scratchArena, dep);
#if defined(NWB_PLATFORM_WINDOWS)
        CanonicalizeTextInPlace(depKey);
#endif
        const auto found = includeMetadata.find(AStringView(depKey));
        if(found == includeMetadata.end())
            continue;

        const IncludeEntry& includeEntry = found.value();
        inheritedEntries.push_back(MakeNotNull(&includeEntry));

        if(!ShaderBinaryPayload::TryAccumulateInheritedShaderDefineReserve(includeEntry.defineValues.size(), defineReserveCount))
            canReserveDefineValues = false;
    }

    if(canReserveDefineValues && defineReserveCount > inOutEntry.defineValues.size())
        inOutEntry.defineValues.reserve(defineReserveCount);

    for(const NotNull<const IncludeEntry*> includeEntry : inheritedEntries){
        for(const auto& [defineName, defineEntry] : includeEntry->defineValues){
            auto [defineIt, inserted] = inOutEntry.defineValues.try_emplace(defineName, m_memoryArena);
            if(inserted)
                defineIt.value().values.assign(defineEntry.values.begin(), defineEntry.values.end());
        }
    }
}

b8 ShaderDefinePlanner::expandDefineCombinations(const CookMap<CookString, DefineEntry>& defineValues, Alloc::ScratchArena& scratchArena, CookVector<DefineCombo>& outCombinations){
    outCombinations.clear();
    DefineCombo initialCombo{m_memoryArena};
    initialCombo.reserve(defineValues.size());
    outCombinations.push_back(Move(initialCombo));

    auto cloneComboWithEntry = [&](const DefineCombo& source, const AStringView defineName, const AStringView defineValue) -> Expected<DefineCombo>{
        DefineCombo copy{m_memoryArena};
        copy.reserve(defineValues.size());
        for(const auto& [sourceName, sourceValue] : source){
            if(!copy.try_emplace(sourceName, sourceValue).second){
                NWB_ASSERT(false);
                return MakeUnexpected(Failure{});
            }
        }
        if(!copy.try_emplace(defineName, defineValue).second){
            NWB_ASSERT(false);
            return MakeUnexpected(Failure{});
        }
        return copy;
    };

    for(const auto& entry : ShaderCook::SortedDefineEntries(defineValues, scratchArena)){
        const AStringView defineName(*entry.key);
        const CookVector<CookString>& values = entry.value->values;
        if(values.empty()){
            outCombinations.clear();
            return true;
        }

        if(values.size() == 1u){
            const AStringView value(values.front());
            for(DefineCombo& combo : outCombinations){
                combo.reserve(defineValues.size());
                if(!combo.try_emplace(defineName, value).second){
                    NWB_ASSERT(false);
                    outCombinations.clear();
                    return false;
                }
            }
            continue;
        }

        const auto expandedCount = ShaderBinaryPayload::TryComputeShaderExpandedCombinationCount(outCombinations.size(), values.size());
        if(!expandedCount){
            outCombinations.clear();
            return false;
        }

        CookVector<DefineCombo> expanded{m_memoryArena};
        expanded.reserve(*expandedCount);

        const usize copiedValueCount = values.size() - 1u;
        for(DefineCombo& combo : outCombinations){
            for(usize valueIndex = 0u; valueIndex < copiedValueCount; ++valueIndex){
                auto copy = cloneComboWithEntry(combo, defineName, values[valueIndex]);
                if(!copy){
                    outCombinations.clear();
                    return false;
                }
                expanded.push_back(Move(*copy));
            }

            combo.reserve(defineValues.size());
            if(!combo.try_emplace(defineName, AStringView(values.back())).second){
                NWB_ASSERT(false);
                outCombinations.clear();
                return false;
            }
            expanded.push_back(Move(combo));
        }

        outCombinations = Move(expanded);
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

