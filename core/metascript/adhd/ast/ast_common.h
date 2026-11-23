// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"

#include "contract_semantics.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct SourceSpan{
    u32 line = 1;
    u32 column = 1;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AccessSpecifier{
    enum Enum : u8{
        None,
#define NWB_ADHD_EXPORT_ACCESS_SPECIFIER(adhdKeyword, enumName, cppLabelPrefix, maybeUnusedOnField) \
        enumName,
#include "../export_keywords.h"
#undef NWB_ADHD_EXPORT_ACCESS_SPECIFIER
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ParamPassingMode{
    enum Enum : u8{
        None,
#define NWB_ADHD_EXPORT_PARAM_PASSING_MODE(adhdKeyword, enumName) \
        enumName,
#include "../export_keywords.h"
#undef NWB_ADHD_EXPORT_PARAM_PASSING_MODE
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ParallelCoreKind{
    enum Enum : u8{
#define NWB_ADHD_EXPORT_PARALLEL_CORE(adhdName, enumName, cppCostEnum, cppTargetEnum) \
        enumName,
#include "../export_keywords.h"
#undef NWB_ADHD_EXPORT_PARALLEL_CORE
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ParallelPriorityKind{
    enum Enum : u8{
#define NWB_ADHD_EXPORT_PARALLEL_PRIORITY(adhdName, enumName, cppPriorityEnum) \
        enumName,
#include "../export_keywords.h"
#undef NWB_ADHD_EXPORT_PARALLEL_PRIORITY
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace DiagnosticKind{
    enum Enum : u8{
#define NWB_ADHD_EXPORT_DIAGNOSTIC_KIND(adhdKeyword, enumName, defaultMacroPrefix, essentialMacroPrefix, defaultMsgMacroPrefix, essentialMsgMacroPrefix) \
        enumName,
#include "../export_keywords.h"
#undef NWB_ADHD_EXPORT_DIAGNOSTIC_KIND
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class AstNodePool final : NoCopy{
private:
    struct ChunkHeader{
        ChunkHeader* prev = nullptr;
        usize capacity = 0u;
        usize used = 0u;
        usize alignment = 16u;
    };

    struct DestructorNode{
        DestructorNode* prev = nullptr;
        void* object = nullptr;
        void (*destroy)(void*)noexcept = nullptr;
    };

    static inline constexpr usize s_ChunkBytes = 65536u;


public:
    static thread_local AstNodePool* s_ActivePool;


public:
    explicit AstNodePool(MetaArena& arena)noexcept
        : m_arena(arena)
    {}
    ~AstNodePool()noexcept{ clear(); }

    template<typename T>
    [[nodiscard]] T* allocate(){
        void* mem = allocateRaw(alignof(T), sizeof(T));
        T* obj = ::new(mem) T(m_arena);
        if constexpr(!IsTriviallyDestructible_V<T>){
            DestructorNode* const dtor = static_cast<DestructorNode*>(allocateRaw(alignof(DestructorNode), sizeof(DestructorNode)));
            dtor->prev = m_dtorHead;
            dtor->object = obj;
            dtor->destroy = [](void* const ptr)noexcept{ static_cast<T*>(ptr)->~T(); };
            m_dtorHead = dtor;
        }
        return obj;
    }

    void clear()noexcept{
        while(m_dtorHead){
            DestructorNode* const node = m_dtorHead;
            m_dtorHead = node->prev;
            node->destroy(node->object);
        }
        while(m_head){
            ChunkHeader* const chunk = m_head;
            m_head = chunk->prev;
            m_arena.deallocate(chunk, chunk->alignment, chunk->capacity);
        }
    }


private:
    [[nodiscard]] void* allocateRaw(usize align, const usize size){
        if(align < 8u)
            align = 8u;
        const usize headerBytes = AlignUp<usize>(sizeof(ChunkHeader), 16u);
        if(!m_head || AlignUp<usize>(m_head->used, align) + size > m_head->capacity){
            const usize bodyMin = headerBytes + size + align;
            const usize chunkBytes = bodyMin > s_ChunkBytes ? AlignUp<usize>(bodyMin, 4096u) : s_ChunkBytes;
            const usize chunkAlign = align > 16u ? align : 16u;
            void* const raw = m_arena.allocate(chunkAlign, chunkBytes);
            ChunkHeader* const chunk = static_cast<ChunkHeader*>(raw);
            chunk->prev = m_head;
            chunk->capacity = chunkBytes;
            chunk->used = headerBytes;
            chunk->alignment = chunkAlign;
            m_head = chunk;
        }
        const usize alignedOffset = AlignUp(m_head->used, align);
        m_head->used = alignedOffset + size;
        return reinterpret_cast<u8*>(m_head) + alignedOffset;
    }


private:
    MetaArena& m_arena;
    ChunkHeader* m_head = nullptr;
    DestructorNode* m_dtorHead = nullptr;
};

class ScopedAstNodePool final : NoCopy{
public:
    explicit ScopedAstNodePool(AstNodePool& pool)noexcept
        : m_previous(AstNodePool::s_ActivePool)
    {
        AstNodePool::s_ActivePool = &pool;
    }
    ~ScopedAstNodePool()noexcept{
        AstNodePool::s_ActivePool = m_previous;
    }


private:
    AstNodePool* m_previous = nullptr;
};

template<typename T>
[[nodiscard]] T* AllocateAstNode(MetaArena& arena){
    if(AstNodePool* const pool = AstNodePool::s_ActivePool)
        return pool->allocate<T>();
    void* mem = arena.allocate(alignof(T), sizeof(T));
    return ::new(mem) T(arena);
}

struct MStringViewHash{
    [[nodiscard]] usize operator()(const MStringView text)const noexcept{
        return static_cast<usize>(ComputeFnv64Bytes(text.data(), text.size()));
    }
};

inline void CopyArenaChars(MChar* dst, const MStringView text)noexcept{
    if(!text.empty())
        FOUNDATION_MEMCPY(dst, text.size(), text.data(), text.size());
}

[[nodiscard]] constexpr b8 ContainsStringView(const Span<const MStringView> items, const MStringView target)noexcept{
    for(const MStringView item : items){
        if(item == target)
            return true;
    }
    return false;
}

[[nodiscard]] inline MStringView StripHeaderDelimiters(const MStringView rawText, b8* const outHadAngles = nullptr, b8* const outHadQuotes = nullptr)noexcept{
    if(outHadAngles)
        *outHadAngles = false;
    if(outHadQuotes)
        *outHadQuotes = false;
    const MStringView text = TrimView(rawText);
    if(text.size() >= 2u){
        if(text.front() == '<' && text.back() == '>'){
            if(outHadAngles)
                *outHadAngles = true;
            return text.substr(1u, text.size() - 2u);
        }
        if(text.front() == '"' && text.back() == '"'){
            if(outHadQuotes)
                *outHadQuotes = true;
            return text.substr(1u, text.size() - 2u);
        }
    }
    return text;
}

[[nodiscard]] inline MStringView InternAngleBracketHeader(MetaArena& arena, const MStringView innerPath){
    const usize totalLen = innerPath.size() + 2u;
    auto* buf = static_cast<MChar*>(arena.allocate(alignof(MChar), totalLen + 1u));
    buf[0] = '<';
    CopyArenaChars(buf + 1u, innerPath);
    buf[totalLen - 1u] = '>';
    buf[totalLen] = '\0';
    return MStringView(buf, totalLen);
}

[[nodiscard]] inline MStringView InternArenaString(MetaArena& arena, const MStringView text){
    if(text.empty())
        return {};
    auto* buf = static_cast<MChar*>(arena.allocate(alignof(MChar), text.size() + 1u));
    CopyArenaChars(buf, text);
    buf[text.size()] = '\0';
    return MStringView(buf, text.size());
}

[[nodiscard]] inline MStringView JoinScopedCppName(MetaArena& arena, const MStringView scope, const MStringView member){
    if(scope.empty())
        return InternArenaString(arena, member);
    if(member.empty())
        return InternArenaString(arena, scope);
    constexpr MStringView s_Sep = ContractSemantics::s_CppScopeSeparator;
    const usize totalLen = scope.size() + s_Sep.size() + member.size();
    auto* buf = static_cast<MChar*>(arena.allocate(alignof(MChar), totalLen + 1u));
    CopyArenaChars(buf, scope);
    CopyArenaChars(buf + scope.size(), s_Sep);
    CopyArenaChars(buf + scope.size() + s_Sep.size(), member);
    buf[totalLen] = '\0';
    return MStringView(buf, totalLen);
}

[[nodiscard]] constexpr b8 HasAdhdExtension(const MStringView path)noexcept{
    constexpr MStringView s_Ext = ContractSemantics::s_AdhdExtension;
    return path.size() > s_Ext.size() && path.ends_with(s_Ext);
}

[[nodiscard]] constexpr MStringView StripAdhdExtension(const MStringView path)noexcept{
    constexpr MStringView s_Ext = ContractSemantics::s_AdhdExtension;
    if(HasAdhdExtension(path))
        return path.substr(0u, path.size() - s_Ext.size());
    return path;
}

[[nodiscard]] constexpr MStringView ExtractAdhdFileStem(const MStringView path)noexcept{
    usize fileStart = GlobalFilesystemPathDetail::RootNameLength(path);
    for(usize i = fileStart; i < path.size(); ++i){
        if(GlobalFilesystemPathDetail::IsSeparator(path[i]))
            fileStart = i + 1u;
    }
    return StripAdhdExtension(path.substr(fileStart));
}

[[nodiscard]] constexpr b8 TryMapSwizzleCharToLane(const MChar c, u32& outLane, b8* outIsRgba = nullptr)noexcept{
#define NWB_ADHD_EXPORT_SWIZZLE_COMPONENT(xyzwChar, rgbaChar, laneIndex, cppLaneCallSuffix) \
    if(c == xyzwChar || c == rgbaChar){ \
        outLane = laneIndex; \
        if(outIsRgba) \
            *outIsRgba = (c == rgbaChar); \
        return true; \
    }
#include "../export_types.h"
#undef NWB_ADHD_EXPORT_SWIZZLE_COMPONENT
    return false;
}

inline constexpr MStringView s_ProhibitedCStyleArrayError = "C-style arrays ('T name[N]') are prohibited in adhd; use 'Array<T, N>' or 'Vector<T>'";

inline void ReportAdhdDiagnostic(MetaArena& arena, MVector<ParseError>& errors, const SourceSpan span, const MStringView message){
    errors.push_back(ParseError{span.line, span.column, MString(message.data(), message.size(), arena)});
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

