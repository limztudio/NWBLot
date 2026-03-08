// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "value.h"
#include "lexer.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ParseError{
    u32 line = 0;
    u32 column = 0;
    MString message;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Document{
public:
    using ErrorList = MVector<ParseError>;
    using VariableMap = MStringMap<Value>;


public:
    explicit Document(Alloc::CustomArena& arena);


public:
    [[nodiscard]] bool parse(MStringView source);
    [[nodiscard]] bool parse(IMetaReader& reader);

    [[nodiscard]] MStringView assetType()const{ return MStringView(m_assetType.data(), m_assetType.size()); }
    [[nodiscard]] MStringView assetVariable()const{ return MStringView(m_assetVariable.data(), m_assetVariable.size()); }
    [[nodiscard]] const Value& asset()const;
    [[nodiscard]] Value& asset();

    [[nodiscard]] const Value* findVariable(MStringView name)const;

    [[nodiscard]] bool hasErrors()const{ return !m_errors.empty(); }
    [[nodiscard]] const ErrorList& errors()const{ return m_errors; }


private:
    Alloc::CustomArena& m_arena;
    MString m_assetType;
    MString m_assetVariable;
    VariableMap m_variables;
    ErrorList m_errors;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

