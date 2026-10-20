// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "name_symbols.h"

#include "arena_names.h"

#include <core/alloc/general.h>
#include <core/alloc/scratch.h>

#include <global/algorithm.h>
#include <global/allocation_size.h>
#include <global/arena_memory.h>
#include <global/filesystem/directory_iterator.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_COMMON_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_common_name_symbols{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using SymbolArena = Alloc::GlobalArena;
using SymbolString = AString<SymbolArena>;
using SymbolMap = ParallelHashMap<NameHash, SymbolString, SymbolArena>;

struct SymbolRecordView{
    NameHash hash = {};
    const SymbolString* text = nullptr;
};

inline constexpr char s_NamesymRecordSeparator = '\t';
inline constexpr char s_NamesymRecordTerminator = '\n';
inline constexpr AStringView s_NamesymProducerSuffix = "\tproducer=runtime\n";
inline constexpr AStringView s_NamesymRuntimeSource = "\truntime\t";
inline constexpr usize s_NamesymEscapedCharBytes = 2u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
void AppendDebugHashText(AString<ArenaT>& outText, const NameHash& hash){
    char hashText[::NameDetail::s_DebugHashTextLength + 1u];
    NameDetail::HashToDebugString(hash, hashText, sizeof(hashText));
    outText += AStringView(hashText, ::NameDetail::s_DebugHashTextLength);
}

template<typename ArenaT>
void AppendEscapedNamesymText(AString<ArenaT>& outText, const AStringView text){
    for(const char ch : text){
        switch(ch){
        case '\\':
            outText += "\\\\";
            break;
        case '\n':
            outText += "\\n";
            break;
        case '\r':
            outText += "\\r";
            break;
        case '\t':
            outText += "\\t";
            break;
        default:
            outText += ch;
            break;
        }
    }
}

class SymbolStore final : NoCopy{
public:
    explicit SymbolStore(SymbolArena& arena)
        : m_symbols(0u, Hasher<NameHash>(), EqualTo<NameHash>(), arena)
    {}


public:
    [[nodiscard]] bool insert(SymbolArena& arena, const NameHash& hash, const AStringView text){
        if(NameDetail::IsZeroHash(hash))
            return false;

        SymbolString canonicalText(arena);
        canonicalText.reserve(text.size());
        for(const char ch : text)
            canonicalText.push_back(Canonicalize(ch));

        const auto insertion = m_symbols.emplace(hash, Move(canonicalText));
        if(!insertion.second)
            return insertion.first->first == hash;
        return true;
    }

    [[nodiscard]] bool resolve(const NameHash& hash, char* const outText, const usize outTextSize)const{
        if(outText == nullptr || outTextSize == 0u || NameDetail::IsZeroHash(hash))
            return false;

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
        m_symbols.clear();
    }

    [[nodiscard]] usize size()const noexcept{
        return m_symbols.size();
    }

    template<typename ArenaT>
    void appendFileText(Alloc::ScratchArena& scratchArena, AString<ArenaT>& outText)const{
        Vector<SymbolRecordView, Alloc::ScratchArena> records(scratchArena);
        records.reserve(m_symbols.size());
        for(const auto& symbol : m_symbols)
            records.push_back(SymbolRecordView{ symbol.first, &symbol.second });

        Sort(records.begin(), records.end(), [](const SymbolRecordView& lhs, const SymbolRecordView& rhs)noexcept{
            return LessNameHash(lhs.hash, rhs.hash);
        }
        );

        usize requiredSize = AddSize(outText.size(), NameSymbols::s_FileHeader.size());
        requiredSize = AddSize(requiredSize, s_NamesymProducerSuffix.size());
        for(const SymbolRecordView& record : records){
            if(!record.text)
                continue;

            requiredSize = AddSize(requiredSize, ::NameDetail::s_DebugHashTextLength);
            requiredSize = AddSize(requiredSize, s_NamesymRuntimeSource.size());
            for(const char ch : *record.text){
                const usize encodedSize = ch == '\\' || ch == '\n' || ch == '\r' || ch == '\t' ? s_NamesymEscapedCharBytes : 1u;
                requiredSize = AddSize(requiredSize, encodedSize);
            }
            requiredSize = AddSize(requiredSize, 1u);
        }
        outText.reserve(requiredSize);

        outText += NameSymbols::s_FileHeader;
        outText += s_NamesymProducerSuffix;
        for(const SymbolRecordView& record : records){
            if(!record.text)
                continue;

            AppendDebugHashText(outText, record.hash);
            outText += s_NamesymRuntimeSource;
            AppendEscapedNamesymText(outText, AStringView(record.text->data(), record.text->size()));
            outText += s_NamesymRecordTerminator;
        }
    }


private:
    SymbolMap m_symbols;
};

class RuntimeRegistry final : NoCopy{
public:
    RuntimeRegistry()
        : m_arena(CommonArenaScope::s_NameSymbolsArena)
        , m_symbols(m_arena)
    {}


public:
    [[nodiscard]] bool insert(const NameHash& hash, const AStringView text){
        return m_symbols.insert(m_arena, hash, text);
    }

    [[nodiscard]] bool resolve(const NameHash& hash, char* const outText, const usize outTextSize)const{
        return m_symbols.resolve(hash, outText, outTextSize);
    }

    void clear()noexcept{
        m_symbols.clear();
    }

    [[nodiscard]] usize size()const noexcept{
        return m_symbols.size();
    }

    [[nodiscard]] Expected<void> serialize(SymbolString& outText){
        Alloc::ScratchArena scratchArena(CommonArenaScope::s_NameSymbolsScratchArena);
        const auto captured = recordArenaOwnerSymbols();
        if(!captured)
            return MakeUnexpected(captured.error());
        m_symbols.appendFileText(scratchArena, outText);
        return {};
    }

    [[nodiscard]] bool writeDefaultFile(){
        Alloc::ScratchArena scratchArena(CommonArenaScope::s_NameSymbolsScratchArena);
        const auto executableDirectory = ::GetExecutableDirectory(scratchArena);
        if(!executableDirectory)
            return false;

        const auto executableName = ::GetExecutableName(scratchArena);
        if(!executableName)
            return false;

        ::Path<Alloc::ScratchArena> outputPath = *executableDirectory / *executableName;
        outputPath.replaceExtension(NameSymbols::s_FileExtension);
        return writeFile(outputPath, scratchArena);
    }

    [[nodiscard]] bool loadLine(const AStringView line, Alloc::ScratchArena& scratchArena){
        if(line.empty())
            return false;

        const usize hashEnd = line.find(s_NamesymRecordSeparator);
        if(hashEnd == AStringView::npos)
            return false;

        const usize sourceEnd = line.find(s_NamesymRecordSeparator, hashEnd + 1u);
        if(sourceEnd == AStringView::npos)
            return false;

        const auto hash = ::NameDetail::DecodeDebugHashText(line.substr(0u, hashEnd));
        if(!hash)
            return false;

        const auto unescapedText = unescapeNamesymText(line.substr(sourceEnd + 1u), scratchArena);
        if(!unescapedText)
            return false;

        return insert(*hash, AStringView(*unescapedText));
    }


private:
    [[nodiscard]] Expected<void> recordArenaOwnerSymbols(){
#if defined(NWB_BUILDMODE)
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
                NWB_FATAL_ASSERT(insert(*hash, text));
        }
#endif
        return {};
    }

    [[nodiscard]] bool writeFile(const ::Path<Alloc::ScratchArena>& path, Alloc::ScratchArena& scratchArena){
        if(!recordArenaOwnerSymbols())
            return false;
        AString<Alloc::ScratchArena> fileText(scratchArena);
        m_symbols.appendFileText(scratchArena, fileText);
        return ::WriteTextFile(path, AStringView(fileText.data(), fileText.size()));
    }

    [[nodiscard]] Expected<AString<Alloc::ScratchArena>> unescapeNamesymText(const AStringView text, Alloc::ScratchArena& scratchArena){
        AString<Alloc::ScratchArena> unescapedText(scratchArena);
        unescapedText.reserve(text.size());

        for(usize i = 0u; i < text.size(); ++i){
            const char ch = text[i];
            if(ch != '\\'){
                unescapedText += ch;
                continue;
            }

            if((i + 1u) >= text.size())
                return MakeUnexpected(Failure{});

            const char escaped = text[++i];
            switch(escaped){
            case '\\':
                unescapedText += '\\';
                break;
            case 'n':
                unescapedText += '\n';
                break;
            case 'r':
                unescapedText += '\r';
                break;
            case 't':
                unescapedText += '\t';
                break;
            default:
                return MakeUnexpected(Failure{});
            }
        }

        return unescapedText;
    }


private:
    SymbolArena m_arena;
    SymbolStore m_symbols;
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

[[nodiscard]] bool ResolveSymbol(const NameHash& hash, char* const outText, const usize outTextSize, void* const userData){
    auto* const registry = static_cast<RuntimeRegistry*>(userData);
    return registry && registry->resolve(hash, outText, outTextSize);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace NameSymbols{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Resolve(const NameHash& hash, char* const outText, const usize outTextSize){
    return __hidden_common_name_symbols::Registry().resolve(hash, outText, outTextSize);
}

usize EntryCount(){
    return __hidden_common_name_symbols::Registry().size();
}

Expected<void> Serialize(AString<Alloc::GlobalArena>& outText){
    return __hidden_common_name_symbols::Registry().serialize(outText);
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

bool LoadFromMemory(const AStringView text, Alloc::ScratchArena& scratchArena){
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

bool LoadFromMemory(const AStringView text){
    Alloc::ScratchArena scratchArena(CommonArenaScope::s_NameSymbolsScratchArena);
    return LoadFromMemory(text, scratchArena);
}

Expected<bool, DefaultFileLoadFailure> LoadDefaultFile(){
    Alloc::ScratchArena scratchArena(CommonArenaScope::s_NameSymbolsScratchArena);
    const auto executableDirectory = ::GetExecutableDirectory(scratchArena);
    if(!executableDirectory)
        return MakeUnexpected(DefaultFileLoadFailure{ executableDirectory.error(), false });

    bool loadedAny = false;
    ErrorCode firstError;

    const auto directory = ::DirectoryIterator<Alloc::ScratchArena>::Create(*executableDirectory);
    if(!directory)
        return MakeUnexpected(DefaultFileLoadFailure{ directory.error(), loadedAny });

    for(const ::DirectoryEntry<Alloc::ScratchArena>& entry : *directory){
        const auto isRegularFile = entry.isRegularFile();
        if(!isRegularFile){
            if(!firstError)
                firstError = isRegularFile.error();
            continue;
        }
        if(!*isRegularFile)
            continue;

        const ::Path<Alloc::ScratchArena> extensionPath = entry.path().extension();
        if(extensionPath.native() != s_FileExtension)
            continue;

        const auto loaded = LoadFile(entry.path(), scratchArena);
        if(!loaded){
            if(!firstError)
                firstError = loaded.error();
            continue;
        }
        loadedAny = *loaded || loadedAny;
    }

    if(firstError)
        return MakeUnexpected(DefaultFileLoadFailure{ firstError, loadedAny });
    return loadedAny;
}

bool WriteDefaultFile(){
    return __hidden_common_name_symbols::Registry().writeDefaultFile();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_COMMON_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

