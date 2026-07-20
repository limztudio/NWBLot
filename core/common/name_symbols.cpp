// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "name_symbols.h"

#include "arena_names.h"

#include <core/alloc/general.h>

#include <algorithm>


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

void AppendDebugHashText(SymbolString& outText, const NameHash& hash){
    char hashText[NameSymbols::s_DebugHashTextLength + 1u] = {};
    NameDetail::HashToDebugString(hash, hashText, sizeof(hashText));
    outText += AStringView(hashText, NameSymbols::s_DebugHashTextLength);
}

void AppendEscapedNamesymText(SymbolString& outText, const AStringView text){
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

    [[nodiscard]] bool insert(SymbolArena& arena, const NameHash& hash, const AStringView text){
        if(NameDetail::IsZeroHash(hash))
            return false;

        SymbolString canonicalText(arena);
        canonicalText.reserve(text.size());
        for(const char ch : text)
            canonicalText.push_back(Canonicalize(ch));

        m_symbols.emplace(hash, Move(canonicalText));
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

    void clear(){
        m_symbols.clear();
    }

    [[nodiscard]] usize size()const{
        return m_symbols.size();
    }

    void appendFileText(SymbolArena& arena, SymbolString& outText)const{
        Vector<SymbolRecordView, SymbolArena> records(arena);
        records.reserve(m_symbols.size());
        for(const auto& symbol : m_symbols)
            records.push_back(SymbolRecordView{ symbol.first, &symbol.second });

        std::sort(
            records.begin(),
            records.end(),
            [](const SymbolRecordView& lhs, const SymbolRecordView& rhs){
                return LessNameHash(lhs.hash, rhs.hash);
            }
        );

        outText += NameSymbols::s_FileHeader;
        outText += "\tproducer=runtime\n";
        for(const SymbolRecordView& record : records){
            if(!record.text)
                continue;

            AppendDebugHashText(outText, record.hash);
            outText += "\truntime\t";
            AppendEscapedNamesymText(outText, AStringView(record.text->data(), record.text->size()));
            outText += '\n';
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

    [[nodiscard]] bool insert(const NameHash& hash, const AStringView text){
        return m_symbols.insert(m_arena, hash, text);
    }

    [[nodiscard]] bool resolve(const NameHash& hash, char* const outText, const usize outTextSize)const{
        return m_symbols.resolve(hash, outText, outTextSize);
    }

    void clear(){
        m_symbols.clear();
    }

    [[nodiscard]] usize size()const{
        return m_symbols.size();
    }

    void serialize(SymbolString& outText){
        m_symbols.appendFileText(m_arena, outText);
    }

    [[nodiscard]] bool writeDefaultFile(){
        ::Path<SymbolArena> executableDirectory(m_arena);
        if(!::GetExecutableDirectory(executableDirectory))
            return false;

        ::Path<SymbolArena> executableName(m_arena);
        if(!::GetExecutableName(executableName))
            return false;

        ::Path<SymbolArena> outputPath = executableDirectory / executableName;
        outputPath.replace_extension(NWB_TEXT(".namesym"));
        return writeFile(outputPath);
    }

    [[nodiscard]] bool loadLine(const AStringView line){
        if(line.empty())
            return false;
        if(StartsWith(line, AStringView(NameSymbols::s_FileHeader)))
            return true;

        const usize hashEnd = line.find('\t');
        if(hashEnd == AStringView::npos)
            return false;

        const usize sourceEnd = line.find('\t', hashEnd + 1u);
        if(sourceEnd == AStringView::npos)
            return false;

        NameHash hash = {};
        if(!NameSymbols::DecodeDebugHashText(line.substr(0u, hashEnd), hash))
            return false;

        SymbolString unescapedText(m_arena);
        if(!unescapeNamesymText(unescapedText, line.substr(sourceEnd + 1u)))
            return false;

        return insert(hash, AStringView(unescapedText.data(), unescapedText.size()));
    }


private:
    [[nodiscard]] bool writeFile(const ::Path<SymbolArena>& path){
        SymbolString fileText(m_arena);
        m_symbols.appendFileText(m_arena, fileText);
        return ::WriteTextFile(path, AStringView(fileText.data(), fileText.size()));
    }

    [[nodiscard]] bool unescapeNamesymText(SymbolString& outText, const AStringView text){
        outText.clear();
        outText.reserve(text.size());

        for(usize i = 0u; i < text.size(); ++i){
            const char ch = text[i];
            if(ch != '\\'){
                outText += ch;
                continue;
            }

            if((i + 1u) >= text.size())
                return false;

            const char escaped = text[++i];
            switch(escaped){
            case '\\':
                outText += '\\';
                break;
            case 'n':
                outText += '\n';
                break;
            case 'r':
                outText += '\r';
                break;
            case 't':
                outText += '\t';
                break;
            default:
                return false;
            }
        }

        return true;
    }

private:
    SymbolArena m_arena;
    SymbolStore m_symbols;
};


[[nodiscard]] RuntimeRegistry& Registry(){
    static RuntimeRegistry registry;
    return registry;
}

void RecordSymbol(
    const NameHash& hash,
    const AStringView text,
    void* const userData
){
    auto* const registry = static_cast<RuntimeRegistry*>(userData);
    if(!registry)
        return;

    if(!registry->insert(hash, text))
        return;
}

[[nodiscard]] bool ResolveSymbol(
    const NameHash& hash,
    char* const outText,
    const usize outTextSize,
    void* const userData
){
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

void Serialize(AString<Alloc::GlobalArena>& outText){
    __hidden_common_name_symbols::Registry().serialize(outText);
}

void InstallRuntimeRegistry(){
    __hidden_common_name_symbols::RuntimeRegistry& registry = __hidden_common_name_symbols::Registry();
    SetNameSymbolRecordCallback(&__hidden_common_name_symbols::RecordSymbol, &registry);
    SetNameSymbolResolveCallback(&__hidden_common_name_symbols::ResolveSymbol, &registry);
}

void UninstallRuntimeRegistry(){
    SetNameSymbolRecordCallback(nullptr, nullptr);
    SetNameSymbolResolveCallback(nullptr, nullptr);
}

void ClearRuntimeSymbols(){
    __hidden_common_name_symbols::Registry().clear();
}

bool LoadLine(const AStringView line){
    return __hidden_common_name_symbols::Registry().loadLine(line);
}

bool WriteDefaultFile(){
    return __hidden_common_name_symbols::Registry().writeDefaultFile();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_COMMON_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

