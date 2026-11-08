// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "name_symbol_document.h"
#include "name_symbols.h"

#include <global/allocation_size.h>
#include <global/name_hash_text.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_COMMON_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_name_symbol_document{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr char s_NamesymRecordSeparator = '\t';
inline constexpr char s_NamesymRecordTerminator = '\n';
inline constexpr AStringView s_NamesymProducerSuffix = "\tproducer=runtime\n";
inline constexpr AStringView s_NamesymRuntimeSource = "\truntime\t";
inline constexpr usize s_NamesymEscapedCharBytes = 2u;
inline constexpr AStringView s_NamesymEscapedBackslash = "\\\\";
inline constexpr AStringView s_NamesymEscapedNewline = "\\n";
inline constexpr AStringView s_NamesymEscapedCarriageReturn = "\\r";
inline constexpr AStringView s_NamesymEscapedTab = "\\t";


template<typename ArenaT>
void AppendDebugHashText(AString<ArenaT>& outText, const NameHash& hash){
    Array<char, NameHashTextCodec::s_TextLength + 1u> hashText;
    NameHashTextCodec::Format(hashText.data(), hashText.size(), hash);
    outText += AStringView(hashText.data(), NameHashTextCodec::s_TextLength);
}

template<typename ArenaT>
void AppendEscapedNamesymText(AString<ArenaT>& outText, const AStringView text){
    for(const char ch : text){
        switch(ch){
        case '\\':
            outText += s_NamesymEscapedBackslash;
            break;
        case '\n':
            outText += s_NamesymEscapedNewline;
            break;
        case '\r':
            outText += s_NamesymEscapedCarriageReturn;
            break;
        case '\t':
            outText += s_NamesymEscapedTab;
            break;
        default:
            outText += ch;
            break;
        }
    }
}

[[nodiscard]] Expected<AString<Alloc::ScratchArena>> UnescapeNamesymText(const AStringView text, Alloc::ScratchArena& scratchArena){
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


template<typename ArenaT>
void AppendFileText(const Span<const NameSymbolDocumentView> records, AString<ArenaT>& outText){
    usize requiredSize = AddSize(outText.size(), NameSymbols::s_FileHeader.size());
    requiredSize = AddSize(requiredSize, s_NamesymProducerSuffix.size());
    for(const NameSymbolDocumentView& record : records){
        requiredSize = AddSize(requiredSize, NameHashTextCodec::s_TextLength);
        requiredSize = AddSize(requiredSize, s_NamesymRuntimeSource.size());
        for(const char ch : record.text){
            const usize encodedSize = ch == '\\' || ch == '\n' || ch == '\r' || ch == '\t' ? s_NamesymEscapedCharBytes : 1u;
            requiredSize = AddSize(requiredSize, encodedSize);
        }
        requiredSize = AddSize(requiredSize, 1u);
    }
    outText.reserve(requiredSize);

    outText += NameSymbols::s_FileHeader;
    outText += s_NamesymProducerSuffix;
    for(const NameSymbolDocumentView& record : records){
        AppendDebugHashText(outText, record.hash);
        outText += s_NamesymRuntimeSource;
        AppendEscapedNamesymText(outText, record.text);
        outText += s_NamesymRecordTerminator;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<NameSymbolDocumentRecord> NameSymbolDocument::Decode(const AStringView line, Alloc::ScratchArena& scratchArena){
    using namespace __hidden_name_symbol_document;
    if(line.empty())
        return MakeUnexpected(Failure{});

    const usize hashEnd = line.find(s_NamesymRecordSeparator);
    if(hashEnd == AStringView::npos)
        return MakeUnexpected(Failure{});

    const usize sourceEnd = line.find(s_NamesymRecordSeparator, hashEnd + 1u);
    if(sourceEnd == AStringView::npos)
        return MakeUnexpected(Failure{});

    const auto hash = NameHashTextCodec::Decode(line.substr(0u, hashEnd));
    if(!hash)
        return MakeUnexpected(Failure{});

    auto unescapedText = UnescapeNamesymText(line.substr(sourceEnd + 1u), scratchArena);
    if(!unescapedText)
        return MakeUnexpected(Failure{});

    return NameSymbolDocumentRecord{ .hash = *hash, .text = Move(*unescapedText) };
}

void NameSymbolDocument::Append(const Span<const NameSymbolDocumentView> records, AString<Alloc::GlobalArena>& outText){
    __hidden_name_symbol_document::AppendFileText(records, outText);
}

void NameSymbolDocument::Append(const Span<const NameSymbolDocumentView> records, AString<Alloc::ScratchArena>& outText){
    __hidden_name_symbol_document::AppendFileText(records, outText);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_COMMON_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

