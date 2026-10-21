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
    using StructMap = MStringMap<Value>;


public:
    struct Declaration{
        MString type;
        MString variable;


    public:
        explicit Declaration(MetaArena& arena)noexcept
            : type(arena)
            , variable(arena)
        {}
        Declaration(MStringView inType, MStringView inVariable, MetaArena& arena)
            : type(inType.data(), inType.size(), arena)
            , variable(inVariable.data(), inVariable.size(), arena)
        {}
    };


public:
    using DeclarationList = MVector<Declaration>;


public:
    explicit Document(MetaArena& arena);


public:
    [[nodiscard]] bool parse(MStringView source);
    [[nodiscard]] bool parse(IMetaReader& reader);

    [[nodiscard]] const Value* findVariable(MStringView name)const noexcept;
    [[nodiscard]] Value* findVariable(MStringView name)noexcept;
    [[nodiscard]] const Declaration* findDeclaration(MStringView name)const noexcept;
    [[nodiscard]] const DeclarationList& declarations()const noexcept{ return m_declarations; }
    [[nodiscard]] const StructMap& structs()const noexcept{ return m_structs; }

    [[nodiscard]] bool hasErrors()const noexcept{ return !m_errors.empty(); }
    [[nodiscard]] const ErrorList& errors()const noexcept{ return m_errors; }


private:
    MetaArena& m_arena;
    VariableMap m_variables;
    DeclarationList m_declarations;
    StructMap m_structs;
    ErrorList m_errors;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

