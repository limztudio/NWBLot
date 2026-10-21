// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "parser.h"

#include "arena_names.h"
#include "statement_parser.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_metascript_parser{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr usize s_ParserScratchArenaBytes = 4096u;
constexpr usize s_DocumentReaderChunkBytes = 4096u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Document::Document(MetaArena& arena)
    : m_arena(arena)
    , m_variables(0, MStringHash(), MStringEqual(), arena)
    , m_declarations(arena)
    , m_structs(0, MStringHash(), MStringEqual(), arena)
    , m_errors(arena)
{}


bool Document::parse(MStringView source){
    m_errors.clear();
    m_variables.clear();
    m_declarations.clear();
    m_structs.clear();

    Alloc::ScratchArena scratchArena(MetascriptArenaScope::s_ParserScratch, __hidden_metascript_parser::s_ParserScratchArenaBytes);
    StatementParser parser(source, m_arena, scratchArena, m_errors, m_variables, m_declarations, m_structs);
    return parser.parseInto();
}

bool Document::parse(IMetaReader& reader){
    m_errors.clear();
    m_variables.clear();
    m_declarations.clear();
    m_structs.clear();

    Alloc::ScratchArena scratchArena(MetascriptArenaScope::s_DocumentReaderScratch, __hidden_metascript_parser::s_DocumentReaderChunkBytes);
    BasicString<MChar, Alloc::ScratchArena> buffer{scratchArena};
    buffer.reserve(__hidden_metascript_parser::s_DocumentReaderChunkBytes);
    MChar chunk[__hidden_metascript_parser::s_DocumentReaderChunkBytes];

    for(;;){
        const auto bytesRead = reader.read(chunk, __hidden_metascript_parser::s_DocumentReaderChunkBytes);
        if(!bytesRead){
            m_errors.push_back(ParseError{0, 0, MString("read error", m_arena)});
            return false;
        }
        if(*bytesRead == 0u)
            break;
        if(*bytesRead > __hidden_metascript_parser::s_DocumentReaderChunkBytes){
            m_errors.push_back(ParseError{0, 0, MString("reader returned more bytes than requested", m_arena)});
            return false;
        }
        buffer.append(chunk, *bytesRead);
    }

    StatementParser parser(MStringView(buffer.data(), buffer.size()), m_arena, scratchArena, m_errors, m_variables, m_declarations, m_structs);
    return parser.parseInto();
}

const Value* Document::findVariable(MStringView name)const noexcept{
    auto it = m_variables.find(name);
    if(it == m_variables.end())
        return nullptr;
    return &it.value();
}

Value* Document::findVariable(MStringView name)noexcept{
    auto it = m_variables.find(name);
    if(it == m_variables.end())
        return nullptr;
    return &it.value();
}

const Document::Declaration* Document::findDeclaration(MStringView name)const noexcept{
    for(const Declaration& declaration : m_declarations){
        if(MStringView(declaration.variable.data(), declaration.variable.size()) == name)
            return &declaration;
    }
    return nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

