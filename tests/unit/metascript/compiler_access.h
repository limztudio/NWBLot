// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/metascript/adhd/compiler.h>
#include <core/metascript/adhd/clang/cpp_header_importer.h>
#include <core/metascript/preprocessor_macro_expander.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN
namespace Tests::Metascript{

namespace CompilerApi = Core::Metascript::Adhd;
namespace MetaApi = Core::Metascript;

template<typename T>
using AstPointerList = MetaApi::MVector<T*>;

template<typename T>
[[nodiscard]] Optional<NotNull<const T*>> BorrowAstNode(const AstPointerList<T>& list, usize index)noexcept{
    const T* node = list[index];
    if(!node)
        return {};
    return MakeNotNull(node);
}

inline void SetCompilerResolver(CompilerApi::Compiler::CompileOptions& options, MetaApi::IIncludeResolver& resolver)noexcept{ options.includeResolver = &resolver; }
inline void SetExternOutput(CompilerApi::Compiler::CompileOptions& options, MetaApi::MString& output)noexcept{ options.outEmittedCppExterns = &output; }

inline void SetConstantType(CompilerApi::TemplateParamDecl& parameter, CompilerApi::TypeRef& type)noexcept{ parameter.constValueType = &type; }

[[nodiscard]] inline Optional<NotNull<const CompilerApi::TypeRef*>> BorrowSubstitutedType(const CompilerApi::SymbolTable& symbols, const CompilerApi::TypeRef& type, const CompilerApi::TemplateParamDecl& parameter, const CompilerApi::TypeArg& argument){
    const auto* substituted = symbols.substituteTypeParams(type, { &parameter, 1u }, { &argument, 1u });
    if(!substituted)
        return {};
    return MakeNotNull(substituted);
}

class MacroMap : public MetaApi::Preprocessor::MacroMap{
public:
    explicit MacroMap(MetaApi::MetaArena& arena) : MetaApi::Preprocessor::MacroMap(0u, TransparentStringHash<MetaApi::MString>(), TransparentStringEqual<MetaApi::MString>(), arena){}
    void store(MetaApi::MString& key, MetaApi::PreprocessorMacro& macro){ insert_or_assign(Move(key), Move(macro)); }
    [[nodiscard]] Optional<NotNull<MetaApi::PreprocessorMacro*>> borrow(MetaApi::MStringView name){
        auto entry = find(name);
        if(entry == end())
            return {};
        return MakeNotNull(&entry.value());
    }
};

};
NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

