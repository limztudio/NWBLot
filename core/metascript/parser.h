// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "value.h"
#include "lexer.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


interface IIncludeResolver;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ParseError{
    u32 line = 0;
    u32 column = 0;
    MString message;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Document final{
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
    using ErrorList = MVector<ParseError>;
    using VariableMap = MStringMap<Value>;
    using StructMap = MStringMap<Value>;
    using DeclarationList = MVector<Declaration>;


public:
    explicit Document(MetaArena& arena)
        : m_arena(arena)
        , m_variables(0, TransparentStringHash<MString>(), TransparentStringEqual<MString>(), arena)
        , m_declarations(arena)
        , m_structs(0, TransparentStringHash<MString>(), TransparentStringEqual<MString>(), arena)
        , m_errors(arena)
    {}


public:
    [[nodiscard]] b8 parse(MStringView source, IIncludeResolver* includeResolver = nullptr);
    [[nodiscard]] b8 parse(IMetaReader& reader, IIncludeResolver* includeResolver = nullptr);

    [[nodiscard]] const Value* findVariable(MStringView name)const noexcept;
    [[nodiscard]] Value* findVariable(MStringView name)noexcept;
    [[nodiscard]] b8 hasVariable(MStringView name)const noexcept{ return findVariable(name) != nullptr; }
    template<typename Visitor>
    b8 visitVariable(MStringView name, Visitor&& visitor){
        Value* const value = findVariable(name);
        return value && Value::InvokeBoolVisitor(visitor, *value);
    }
    template<typename Visitor>
    b8 visitVariable(MStringView name, Visitor&& visitor)const{
        return Value::VisitOptionalValue(findVariable(name), visitor);
    }
    [[nodiscard]] Optional<NotNull<const Value*>> borrowVariable(const MStringView name)const noexcept{
        if(const Value* value = findVariable(name))
            return MakeNotNull(value);
        return {};
    }
    [[nodiscard]] const Declaration* findDeclaration(MStringView name)const noexcept;
    [[nodiscard]] const DeclarationList& declarations()const noexcept{ return m_declarations; }
    [[nodiscard]] const StructMap& structs()const noexcept{ return m_structs; }

    [[nodiscard]] Optional<MStringView> declarationType(const MStringView name)const noexcept{
        if(const Declaration* declaration = findDeclaration(name))
            return MStringView(declaration->type);
        return {};
    }

    [[nodiscard]] usize structCount()const noexcept{ return m_structs.size(); }
    [[nodiscard]] usize declarationCount()const noexcept{ return m_declarations.size(); }
    template<typename Visitor>
    b8 forEachStruct(Visitor&& visitor)const{
        for(const auto& [name, value] : m_structs){
            if(!visitor(MStringView(name), value))
                return false;
        }
        return true;
    }
    template<typename Visitor>
    b8 forEachDeclaration(Visitor&& visitor)const{
        for(const Declaration& declaration : m_declarations){
            if(!visitor(MStringView(declaration.type), MStringView(declaration.variable)))
                return false;
        }
        return true;
    }

    [[nodiscard]] b8 hasErrors()const noexcept{ return !m_errors.empty(); }

    [[nodiscard]] const ErrorList& errors()const noexcept{ return m_errors; }


private:
    void clearParsedState()noexcept;
    [[nodiscard]] b8 parseSourceInternal(MStringView source, IIncludeResolver* includeResolver);


private:
    MetaArena& m_arena;
    VariableMap m_variables;
    DeclarationList m_declarations;
    StructMap m_structs;
    ErrorList m_errors;
};

class DocumentWorkspace final : NoCopy{
public:
    explicit DocumentWorkspace(const Name scope) : m_arena(scope), document(m_arena){}

private:
    MetaArena m_arena;
public:
    Document document;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

