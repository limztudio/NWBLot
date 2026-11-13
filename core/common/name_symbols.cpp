// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "name_symbols.h"
#include "name_symbol_document.h"
#include "name_symbol_sidecars.h"

#include <global/algorithm.h>
#include <global/arena_memory.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_COMMON_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_common_name_symbols{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using SymbolArena = Alloc::GlobalArena;
using SymbolString = AString<SymbolArena>;
using SymbolMap = ParallelHashMap<NameHash, SymbolString, SymbolArena>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RuntimeRegistry final : NoCopy{
public:
    RuntimeRegistry()
        : m_arena(CommonArenaScope::s_NameSymbolsArena)
        , m_symbols(0u, Hasher<NameHash>(), EqualTo<NameHash>(), m_arena)
    {}


public:
    [[nodiscard]] b8 insert(const NameHash& hash, const AStringView text){
        if(IsZeroHash(hash))
            return false;

        ScopedLock lock(m_mutex);
        SymbolString canonicalText(m_arena);
        canonicalText.reserve(text.size());
        for(const char ch : text)
            canonicalText.push_back(Canonicalize(ch));

        const auto insertion = m_symbols.emplace(hash, Move(canonicalText));
        if(!insertion.second)
            return insertion.first->first == hash;
        return true;
    }

    [[nodiscard]] b8 resolve(char* const outText, const usize outTextSize, const NameHash& hash)const{
        if(outText == nullptr || outTextSize == 0u || IsZeroHash(hash))
            return false;

        ScopedLock lock(m_mutex);
        const auto it = m_symbols.find(hash);
        if(it == m_symbols.end())
            return false;

        const SymbolString& text = it->second;
        if(text.size() >= outTextSize)
            return false;

        for(usize i = 0u; i < text.size(); ++i)
            outText[i] = text[i];
        outText[text.size()] = 0;
        return true;
    }

    void clear()noexcept{
        ScopedLock lock(m_mutex);
        m_symbols.clear();
    }

    [[nodiscard]] usize size()const noexcept{
        ScopedLock lock(m_mutex);
        return m_symbols.size();
    }

    [[nodiscard]] Expected<void> serialize(SymbolString& outText){
        Alloc::ScratchArena scratchArena(CommonArenaScope::s_NameSymbolsScratchArena);
        const auto captured = recordArenaOwnerSymbols();
        if(!captured)
            return MakeUnexpected(captured.error());
        appendFileText(scratchArena, outText);
        return {};
    }

    [[nodiscard]] b8 writeDefaultFile(){
        Alloc::ScratchArena scratchArena(CommonArenaScope::s_NameSymbolsScratchArena);
        const auto outputPath = NameSymbolSidecars::DefaultPath(scratchArena);
        if(!outputPath)
            return false;
        return writeFile(*outputPath, scratchArena);
    }

    [[nodiscard]] b8 loadLine(const AStringView line, Alloc::ScratchArena& scratchArena){
        auto record = NameSymbolDocument::Decode(line, scratchArena);
        return record && insert(record->hash, AStringView(record->text));
    }


private:
#if defined(NWB_BUILDMODE)
    [[nodiscard]] Expected<void> recordArenaOwnerSymbols(){
        const ArenaMemoryOwnerRecord* owner = FirstArenaMemoryOwnerRecord();
        while(owner){
            ArenaMemoryOwnerSnapshot snapshot;
            owner = ReadArenaMemoryOwnerRecord(*owner, snapshot);
            const AStringView text(snapshot.ownerName.logText());
            const auto hash = ComputeNameHash(text);
            if(!hash)
                return MakeUnexpected(Failure{});
            // Verify stored text first; insert after the census releases its lock.
            if(snapshot.ownerName == Name(*hash))
                FOUNDATION_FATAL_ASSERT(insert(*hash, text));
        }
        return {};
    }
#else
    [[nodiscard]] Expected<void> recordArenaOwnerSymbols()noexcept{ return {}; }
#endif

    template<typename ArenaT>
    void appendFileText(Alloc::ScratchArena& scratchArena, AString<ArenaT>& outText)const{
        ScopedLock lock(m_mutex);
        Vector<NameSymbolDocumentView, Alloc::ScratchArena> records(scratchArena);
        records.reserve(m_symbols.size());
        for(const auto& symbol : m_symbols)
            records.push_back(NameSymbolDocumentView{ symbol.first, AStringView(symbol.second) });

        Sort(records.begin(), records.end(), [](const NameSymbolDocumentView& lhs, const NameSymbolDocumentView& rhs)noexcept{
            return LessNameHash(lhs.hash, rhs.hash);
        });
        NameSymbolDocument::Append(Span<const NameSymbolDocumentView>(records.data(), records.size()), outText);
    }

    [[nodiscard]] b8 writeFile(const ::Path<Alloc::ScratchArena>& path, Alloc::ScratchArena& scratchArena){
        if(!recordArenaOwnerSymbols())
            return false;
        AString<Alloc::ScratchArena> fileText(scratchArena);
        appendFileText(scratchArena, fileText);
        return NameSymbolSidecars::Write(path, AStringView(fileText));
    }



private:
    mutable Futex m_mutex;
    SymbolArena m_arena;
    SymbolMap m_symbols;
};


[[nodiscard]] RuntimeRegistry& Registry(){
    static RuntimeRegistry s_Registry;
    return s_Registry;
}

void RecordSymbol(const NameHash& hash, const AStringView text, void* const userData){
    auto* const registry = static_cast<RuntimeRegistry*>(userData);
    if(!registry)
        return;

    if(!registry->insert(hash, text))
        return;
}

[[nodiscard]] b8 ResolveSymbol(char* const outText, const usize outTextSize, const NameHash& hash, void* const userData){
    auto* const registry = static_cast<RuntimeRegistry*>(userData);
    return registry && registry->resolve(outText, outTextSize, hash);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace NameSymbols{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 Resolve(char* const outText, const usize outTextSize, const NameHash& hash){
    return __hidden_common_name_symbols::Registry().resolve(outText, outTextSize, hash);
}

void InstallRuntimeRegistry(){
    __hidden_common_name_symbols::RuntimeRegistry& registry = __hidden_common_name_symbols::Registry();
    SetNameSymbolRecordCallback(&__hidden_common_name_symbols::RecordSymbol, &registry);
    SetNameSymbolResolveCallback(&__hidden_common_name_symbols::ResolveSymbol, &registry);
}

void UninstallRuntimeRegistry()noexcept{
    SetNameSymbolRecordCallback(nullptr, nullptr);
    SetNameSymbolResolveCallback(nullptr, nullptr);
}

void ClearRuntimeSymbols(){
    __hidden_common_name_symbols::Registry().clear();
}

b8 WriteDefaultFile(){
    return __hidden_common_name_symbols::Registry().writeDefaultFile();
}

usize EntryCount(){
    return __hidden_common_name_symbols::Registry().size();
}

Expected<void> Serialize(AString<Alloc::GlobalArena>& outText){
    return __hidden_common_name_symbols::Registry().serialize(outText);
}

b8 LoadFromMemory(const AStringView text, Alloc::ScratchArena& scratchArena){
    const usize headerEnd = text.find('\n');
    AStringView header = text.substr(0u, headerEnd);
    if(!header.empty() && header.back() == '\r')
        header.remove_suffix(1u);
    if(header.substr(0u, header.find('\t')) != s_FileHeader)
        return false;

    __hidden_common_name_symbols::RuntimeRegistry& registry = __hidden_common_name_symbols::Registry();
    usize lineBegin = headerEnd == AStringView::npos ? text.size() : headerEnd + 1u;
    while(lineBegin < text.size()){
        const usize lineEnd = text.find('\n', lineBegin);
        const usize recordEnd = lineEnd == AStringView::npos ? text.size() : lineEnd;
        AStringView line = text.substr(lineBegin, recordEnd - lineBegin);
        if(!line.empty() && line.back() == '\r')
            line.remove_suffix(1u);
        lineBegin = lineEnd == AStringView::npos ? text.size() : lineEnd + 1u;
        if(!registry.loadLine(line, scratchArena))
            continue;
    }

    return true;
}

Expected<b8, DefaultFileLoadFailure> LoadDefaultFile(){
    return NameSymbolSidecars::LoadDefault();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_COMMON_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

