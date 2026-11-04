// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "algorithm.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename CacheT, typename RequestedT, typename TSameKey>
[[nodiscard]] inline usize CountMissingCacheEntries(
    const CacheT& cache,
    const RequestedT& requested,
    TSameKey&& sameKey
){
    usize missing = 0u;
    for(const auto& item : requested){
        const auto found = FindIf(cache.begin(), cache.end(), [&item, &sameKey](const auto& cached){
            return sameKey(cached, item);
        });
        if(found == cache.end())
            ++missing;
    }
    return missing;
}

template<typename CacheT, typename RequestedT, typename TSameKey>
inline void RetainReferencedCacheEntries(
    const RequestedT& requested,
    TSameKey&& sameKey,
    CacheT& cache
){
    for(usize index = 0u; index < cache.size();){
        const auto& cached = cache[index];
        const auto found = FindIf(requested.begin(), requested.end(), [&cached, &sameKey](const auto& item){
            return sameKey(cached, item);
        });
        if(found == requested.end())
            cache.erase(cache.begin() + static_cast<isize>(index));
        else
            ++index;
    }
}


template<typename Sequence, typename SameKey>
void StableDeduplicate(Sequence& values, const SameKey& sameKey)noexcept(
    noexcept(static_cast<b8>(DeclVal<usize&>() < values.size()))
    && noexcept(values[DeclVal<usize&>()])
    && noexcept(static_cast<b8>(sameKey(values[DeclVal<usize&>()], DeclVal<const RemoveReference_T<decltype(values[DeclVal<usize&>()])>&>())))
    && noexcept(values[DeclVal<usize&>()] = DeclVal<const RemoveReference_T<decltype(values[DeclVal<usize&>()])>&>())
    && noexcept(values.resize(DeclVal<usize&>()))
){
    usize writeIndex = 0u;
    for(usize readIndex = 0u; readIndex < values.size(); ++readIndex){
        const auto& candidate = values[readIndex];
        b8 duplicate = false;
        for(usize previous = 0u; previous < writeIndex; ++previous){
            if(sameKey(values[previous], candidate)){
                duplicate = true;
                break;
            }
        }
        if(!duplicate){
            if(writeIndex != readIndex)
                values[writeIndex] = candidate;
            ++writeIndex;
        }
    }
    values.resize(writeIndex);
}


template<typename Count, typename Predicate>
[[nodiscard]] inline Expected<u64> FindDirectionalIndex(const u64 start, const b8 reverse, Count&& count, Predicate&& matches)noexcept(
    noexcept(static_cast<b8>(start >= count()))
    && noexcept(static_cast<b8>(DeclVal<u64&>() < count()))
    && noexcept(static_cast<b8>(matches(DeclVal<u64&>())))
    && noexcept(Expected<u64>(DeclVal<u64&>()))
    && noexcept(Expected<u64>(MakeUnexpected(Failure{})))
){
    if(start >= count())
        return MakeUnexpected(Failure{});
    for(u64 candidate = start; candidate < count(); reverse ? --candidate : ++candidate){
        if(matches(candidate))
            return candidate;
        if(reverse && candidate == 0u)
            break;
    }
    return MakeUnexpected(Failure{});
}


template<typename T, typename Valid, typename Duplicate>
[[nodiscard]] inline b8 AllValidAndDistinct(const Span<const T> items, Valid&& valid, Duplicate&& duplicate)noexcept(
    noexcept(static_cast<b8>(!valid(DeclVal<const T&>())))
    && noexcept(static_cast<b8>(duplicate(DeclVal<const T&>(), DeclVal<const T&>())))
){
    for(usize index = 0u; index < items.size(); ++index){
        if(!valid(items[index]))
            return false;
        for(usize previous = 0u; previous < index; ++previous){
            if(duplicate(items[index], items[previous]))
                return false;
        }
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

