// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "array.h"
#include "assert.h"
#include "bit.h"
#include "containers.h"
#include "generic.h"
#include "span.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Initialize once with a nonempty span whose size fits a u32 index.
template<typename ArenaT>
struct HierarchicalIndexSet final : NoCopy{
    static constexpr usize s_IndexBitsPerLevel = 6u;
    static constexpr usize s_BitsPerWord = static_cast<usize>(1u) << s_IndexBitsPerLevel;
    static constexpr usize s_MaxLevels = (sizeof(u32) * 8u + s_IndexBitsPerLevel - 1u) / s_IndexBitsPerLevel;
    Vector<u64, ArenaT> m_words;
    Array<usize, s_MaxLevels> m_levelOffsets = {};
    usize m_levelCount = 0u;
    usize m_indexCount = 0u;


public:
    explicit HierarchicalIndexSet(ArenaT& arena)noexcept
        : m_words(arena)
    {}


public:
    template<typename T>
    void initializeZeros(const Span<const T> values){
        m_indexCount = values.size();
        usize levelWords = (m_indexCount - 1u) / s_BitsPerWord + 1u;
        usize wordCount = 0u;
        for(;;){
            FOUNDATION_ASSERT(m_levelCount < m_levelOffsets.size());
            m_levelOffsets[m_levelCount++] = wordCount;
            wordCount += levelWords;
            if(levelWords == 1u)
                break;
            levelWords = (levelWords - 1u) / s_BitsPerWord + 1u;
        }
        m_words.resize(wordCount, 0u);
        for(usize taskIndex = 0u; taskIndex < m_indexCount; ++taskIndex){
            if(values[taskIndex] == 0u)
                m_words[taskIndex / s_BitsPerWord] |= static_cast<u64>(1u) << (taskIndex % s_BitsPerWord);
        }
        for(usize level = 1u; level < m_levelCount; ++level){
            const usize lowerWordCount = m_levelOffsets[level] - m_levelOffsets[level - 1u];
            for(usize word = 0u; word < lowerWordCount; ++word){
                if(m_words[m_levelOffsets[level - 1u] + word] != 0u)
                    m_words[m_levelOffsets[level] + word / s_BitsPerWord] |= static_cast<u64>(1u) << (word % s_BitsPerWord);
            }
        }
    }

    void add(const u32 taskIndex)noexcept{
        usize index = taskIndex;
        for(usize level = 0u; level < m_levelCount; ++level){
            u64& word = m_words[m_levelOffsets[level] + index / s_BitsPerWord];
            const b8 wasEmpty = word == 0u;
            word |= static_cast<u64>(1u) << (index % s_BitsPerWord);
            if(!wasEmpty)
                break;
            index /= s_BitsPerWord;
        }
    }

    [[nodiscard]] usize takeLowest()noexcept{
        if(m_words[m_levelOffsets[m_levelCount - 1u]] == 0u)
            return m_indexCount;
        usize taskIndex = 0u;
        for(usize level = m_levelCount; level > 0u; --level){
            const u64 word = m_words[m_levelOffsets[level - 1u] + taskIndex];
            FOUNDATION_ASSERT(word != 0u);
            taskIndex = taskIndex * s_BitsPerWord + static_cast<usize>(CountTrailingZeros(word));
        }
        usize index = taskIndex;
        for(usize level = 0u; level < m_levelCount; ++level){
            u64& word = m_words[m_levelOffsets[level] + index / s_BitsPerWord];
            word &= ~(static_cast<u64>(1u) << (index % s_BitsPerWord));
            if(word != 0u)
                break;
            index /= s_BitsPerWord;
        }
        return taskIndex;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

