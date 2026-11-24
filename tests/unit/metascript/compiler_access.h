// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/metascript/adhd/compiler.h>
#include <core/metascript/adhd/clang/cpp_header_importer.h>
#include <core/metascript/preprocessor_macro_expander.h>
#include <core/graphics/backend_selection/backend.h>
#include <core/graphics/runtime/runtime.h>
#include <core/metascript/parser.h>
#include <core/task/gpu/task_graph.h>


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

inline void ConfigureNativeImport(CompilerApi::CppHeaderImportOptions& options, MetaApi::MVector<MetaApi::MString>& defines, MetaApi::MetaArena& arena){
#if defined(FOUNDATION_DEBUG)
    defines.push_back(MetaApi::MString("FOUNDATION_DEBUG", arena));
#elif defined(FOUNDATION_OPTIMIZE)
    defines.push_back(MetaApi::MString("FOUNDATION_OPTIMIZE", arena));
#elif defined(FOUNDATION_FINAL)
    defines.push_back(MetaApi::MString("FOUNDATION_FINAL", arena));
#endif
#if defined(NWB_DEBUG)
    defines.push_back(MetaApi::MString("NWB_DEBUG", arena));
#elif defined(NWB_OPTIMIZE)
    defines.push_back(MetaApi::MString("NWB_OPTIMIZE", arena));
#elif defined(NWB_FINAL)
    defines.push_back(MetaApi::MString("NWB_FINAL", arena));
#endif
#if defined(_MSC_VER) && defined(_DEBUG)
    defines.push_back(MetaApi::MString("_DEBUG", arena));
#endif
    options.defines = Span<const MetaApi::MString>(defines.data(), defines.size());
}

[[nodiscard]] constexpr usize NativeDocumentSize()noexcept{ return sizeof(MetaApi::Document); }
[[nodiscard]] constexpr usize NativeDescriptorHeapSize()noexcept{ return sizeof(Core::GpuDescriptorHeap); }
[[nodiscard]] constexpr usize NativeTaskGraphSize()noexcept{ return sizeof(Core::GpuTaskGraph); }
[[nodiscard]] constexpr usize NativeGraphicsRuntimeSize()noexcept{ return sizeof(Core::GraphicsRuntime); }

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

