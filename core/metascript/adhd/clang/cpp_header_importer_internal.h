// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "cpp_header_importer.h"

#include "../ast/builtin_type_semantics.h"
#include "../ast/keyword_semantics.h"
#include "../ast/operator_semantics.h"
#include "../sema/type_utils.h"

#include <clang-c/Index.h>
#include <core/metascript/literal_decoder.h>
#include <global/array.h>
#include <global/hash_utils.h>
#include <global/numeric_text.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CppHeaderImportDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct CppDirectTypeMapEntry{
    MStringView cppName;
    MStringView adhdName;
};

struct CppContainerAliasEntry{
    MStringView suffixText;
    MStringView targetContainerName;
};

struct CppClangConfigGroupEntry{
    MStringView defaultFlag;
    MStringView optPrefix;
    MStringView dbgPrefix;
    MStringView finalPrefix;
};

class CppImporterSemantics final{
public:
#define NWB_ADHD_EXPORT_CPP_IMPORTER_SYMBOL(idName, textValue) \
    inline static constexpr MStringView s_##idName = textValue;
#include "export_cpp_importer.h"
#undef NWB_ADHD_EXPORT_CPP_IMPORTER_SYMBOL

    inline static constexpr auto s_DirectTypeMap = Array{
    #define NWB_ADHD_EXPORT_CPP_DIRECT_TYPE_MAP(cppName, adhdName) \
        CppDirectTypeMapEntry{ cppName, adhdName },
    #include "export_cpp_importer.h"
    #undef NWB_ADHD_EXPORT_CPP_DIRECT_TYPE_MAP
    };

    inline static constexpr auto s_StripPrefixes = Array{
    #define NWB_ADHD_EXPORT_CPP_STRIP_PREFIX(prefixText) \
        MStringView(prefixText),
    #include "export_cpp_importer.h"
    #undef NWB_ADHD_EXPORT_CPP_STRIP_PREFIX
    };

    inline static constexpr auto s_InternalPrefixes = Array{
    #define NWB_ADHD_EXPORT_CPP_INTERNAL_PREFIX(prefixText) \
        MStringView(prefixText),
    #include "export_cpp_importer.h"
    #undef NWB_ADHD_EXPORT_CPP_INTERNAL_PREFIX
    };

    inline static constexpr auto s_InternalSuffixes = Array{
    #define NWB_ADHD_EXPORT_CPP_INTERNAL_SUFFIX(suffixText) \
        MStringView(suffixText),
    #include "export_cpp_importer.h"
    #undef NWB_ADHD_EXPORT_CPP_INTERNAL_SUFFIX
    };

    inline static constexpr auto s_InternalExacts = Array{
    #define NWB_ADHD_EXPORT_CPP_INTERNAL_EXACT(nameText) \
        MStringView(nameText),
    #include "export_cpp_importer.h"
    #undef NWB_ADHD_EXPORT_CPP_INTERNAL_EXACT
    };

    inline static constexpr auto s_UnmappedKeywords = Array{
    #define NWB_ADHD_EXPORT_CPP_UNMAPPED_KEYWORD(keywordText) \
        MStringView(keywordText),
    #include "export_cpp_importer.h"
    #undef NWB_ADHD_EXPORT_CPP_UNMAPPED_KEYWORD
    };

    inline static constexpr auto s_DisallowedOperators = Array{
    #define NWB_ADHD_EXPORT_CPP_DISALLOWED_OPERATOR(opSymbol) \
        MStringView(opSymbol),
    #include "export_cpp_importer.h"
    #undef NWB_ADHD_EXPORT_CPP_DISALLOWED_OPERATOR
    };

    inline static constexpr auto s_ContainerSuffixAliases = Array{
    #define NWB_ADHD_EXPORT_CPP_CONTAINER_SUFFIX_ALIAS(suffixText, targetContainerName) \
        CppContainerAliasEntry{ suffixText, targetContainerName },
    #include "export_cpp_importer.h"
    #undef NWB_ADHD_EXPORT_CPP_CONTAINER_SUFFIX_ALIAS
    };

    inline static constexpr auto s_StringViewConstantTypes = Array{
    #define NWB_ADHD_EXPORT_CPP_STRING_VIEW_CONSTANT_TYPE(adhdTypeName) \
        MStringView(adhdTypeName),
    #include "export_cpp_importer.h"
    #undef NWB_ADHD_EXPORT_CPP_STRING_VIEW_CONSTANT_TYPE
    };

    inline static constexpr auto s_ClangDefaultArgs = Array{
    #define NWB_ADHD_EXPORT_CPP_CLANG_DEFAULT_ARG(argText) \
        MStringView(argText),
    #include "export_cpp_importer.h"
    #undef NWB_ADHD_EXPORT_CPP_CLANG_DEFAULT_ARG
    };

    inline static constexpr auto s_ClangWindowsArgs = Array{
    #define NWB_ADHD_EXPORT_CPP_CLANG_WINDOWS_ARG(argText) \
        MStringView(argText),
    #include "export_cpp_importer.h"
    #undef NWB_ADHD_EXPORT_CPP_CLANG_WINDOWS_ARG
    };

    inline static constexpr auto s_ClangConfigGroups = Array{
    #define NWB_ADHD_EXPORT_CPP_CLANG_CONFIG_GROUP(idName, defaultFlag, optPrefix, dbgPrefix, finalPrefix) \
        CppClangConfigGroupEntry{ defaultFlag, optPrefix, dbgPrefix, finalPrefix },
    #include "export_cpp_importer.h"
    #undef NWB_ADHD_EXPORT_CPP_CLANG_CONFIG_GROUP
    };

    inline static constexpr auto s_ThirdPartyIncludeSubdirs = Array{
    #define NWB_ADHD_EXPORT_CPP_THIRD_PARTY_INCLUDE_SUBDIR(relPath) \
        MStringView(relPath),
    #include "export_cpp_importer.h"
    #undef NWB_ADHD_EXPORT_CPP_THIRD_PARTY_INCLUDE_SUBDIR
    };


public:
    [[nodiscard]] static constexpr MStringView MapDirectTypeName(const MStringView name)noexcept{
        if(BuiltinTypeSemantics::IsVoidTypeName(name))
            return BuiltinTypeSemantics::VoidTypeName();
        if(const BuiltinScalarTypeInfo* scalar = BuiltinTypeSemantics::FindBuiltinScalarType(name))
            return scalar->adhdName;
        for(const CppDirectTypeMapEntry& entry : s_DirectTypeMap){
            if(entry.cppName == name)
                return entry.adhdName;
        }
        return {};
    }

    [[nodiscard]] static constexpr b8 IsInternalIdentifierName(const MStringView name)noexcept{
        if(name.empty())
            return true;
        for(const MStringView prefix : s_InternalPrefixes){
            if(name.starts_with(prefix))
                return true;
        }
        for(const MStringView suffix : s_InternalSuffixes){
            if(name.ends_with(suffix))
                return true;
        }
        for(const MStringView exact : s_InternalExacts){
            if(name == exact)
                return true;
        }
        return false;
    }

    [[nodiscard]] static constexpr b8 IsUnmappedCppKeyword(const MStringView name)noexcept{
        for(const MStringView kw : s_UnmappedKeywords){
            if(name == kw)
                return true;
        }
        return false;
    }

    [[nodiscard]] static constexpr b8 IsDisallowedOperatorSymbol(const MStringView op)noexcept{
        for(const MStringView disallowed : s_DisallowedOperators){
            if(op == disallowed)
                return true;
        }
        return false;
    }

    [[nodiscard]] static constexpr MStringView ResolveContainerSuffixAlias(const MStringView name)noexcept{
        for(const CppContainerAliasEntry& alias : s_ContainerSuffixAliases){
            if(name.size() > alias.suffixText.size() && name.ends_with(alias.suffixText))
                return alias.targetContainerName;
        }
        return {};
    }

    [[nodiscard]] static constexpr b8 IsStringViewConstantTypeName(const MStringView name)noexcept{
        for(const MStringView tv : s_StringViewConstantTypes){
            if(name == tv)
                return true;
        }
        return false;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] MString NormalizeSlashes(MetaArena& arena, MStringView raw);
[[nodiscard]] b8 StartsWithCaseInsensitivePrefix(MStringView fullPath, MStringView prefix)noexcept;
[[nodiscard]] Expr* AllocateSignedIntegerLiteral(MetaArena& arena, long long val);
[[nodiscard]] MString ConsumeClangString(MetaArena& arena, CXString cxStr);
[[nodiscard]] TypeRef* CloneTypeRefDeep(MetaArena& arena, const TypeRef* src);
[[nodiscard]] TypeRef* RewriteTemplateTypeRefName(MetaArena& arena, TypeRef* ref, MStringView oldName, MStringView newName);
[[nodiscard]] MStringView MapRawBuiltinScalarKind(CXTypeKind kind)noexcept;
[[nodiscard]] b8 FormatNormalizedAdhdDeclName(
    MetaArena& arena,
    CXCursor declCursor,
    MStringView& outName,
    MStringView& outNamespace,
    MStringView& outCppExportName,
    b8& outIsDirectEnum,
    b8& outIsFlag
);

void AppendExternAttributes(MString& out, MStringView requiredHeader, MStringView cppExportName, MStringView indent);
void AppendTypeRefExternText(MString& out, const TypeRef& type, b8 includeRefMode);
void AppendMethodExternSignature(MString& out, const MethodDecl& m, MStringView indent, b8 includeHeader);
[[nodiscard]] MString BuildFreeFunctionSigKey(MetaArena& arena, const MethodDecl& decl);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct CursorKey{
    i32 kind = 0;
    i32 xdata = 0;
    const void* data0 = nullptr;
    const void* data1 = nullptr;

    [[nodiscard]] b8 operator==(const CursorKey& rhs)const noexcept{
        return kind == rhs.kind && xdata == rhs.xdata && data0 == rhs.data0 && data1 == rhs.data1;
    }
};

struct CursorKeyHash{
    [[nodiscard]] usize operator()(const CursorKey& key)const noexcept{
        u64 h = MixMurmur64Finalizer((static_cast<u64>(static_cast<u32>(key.kind)) << 32u) ^ static_cast<u64>(static_cast<u32>(key.xdata)));
        h = MixMurmur64Finalizer(h ^ reinterpret_cast<u64>(key.data0));
        h = MixMurmur64Finalizer(h ^ reinterpret_cast<u64>(key.data1));
        return static_cast<usize>(h);
    }
};

[[nodiscard]] inline CursorKey MakeCursorKey(const CXCursor cursor)noexcept{
    return CursorKey{ static_cast<i32>(cursor.kind), static_cast<i32>(cursor.xdata), cursor.data[0], cursor.data[1] };
}

struct TypeCacheKey{
    i32 kind = 0;
    u8 isParamContext = 0u;
    const void* data0 = nullptr;
    const void* data1 = nullptr;

    [[nodiscard]] b8 operator==(const TypeCacheKey& rhs)const noexcept{
        return kind == rhs.kind && isParamContext == rhs.isParamContext && data0 == rhs.data0 && data1 == rhs.data1;
    }
};

struct TypeCacheKeyHash{
    [[nodiscard]] usize operator()(const TypeCacheKey& key)const noexcept{
        u64 h = MixMurmur64Finalizer((static_cast<u64>(static_cast<u32>(key.kind)) << 8u) ^ static_cast<u64>(key.isParamContext));
        h = MixMurmur64Finalizer(h ^ reinterpret_cast<u64>(key.data0));
        h = MixMurmur64Finalizer(h ^ reinterpret_cast<u64>(key.data1));
        return static_cast<usize>(h);
    }
};

struct VoidPtrHash{
    [[nodiscard]] usize operator()(const void* ptr)const noexcept{
        return static_cast<usize>(MixMurmur64Finalizer(reinterpret_cast<u64>(ptr)));
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ImporterContext final{
public:
    ImporterContext(MetaArena& arena, TranslationUnit& unit)noexcept
        : m_arena(arena)
        , m_unit(unit)
        , m_includeRoots(arena)
        , m_headerByFile(arena)
        , m_headerCache(arena)
        , m_declNameCache(arena)
        , m_typeCache(arena)
        , m_classByExportName(arena)
        , m_classByName(arena)
        , m_enumByExportName(arena)
        , m_enumByName(arena)
        , m_aliasByName(arena)
        , m_constantByName(arena)
        , m_seenFreeFunctions(arena)
        , m_seenConstants(arena)
    {}


public:
    void run(CXTranslationUnit tu, Span<const MString> normalizedIncludeRoots);


private:
    struct CachedDeclName{
        MStringView name;
        MStringView cppNamespace;
        MStringView cppExportName;
        b8 isDirectEnum = false;
        b8 isFlag = false;
        b8 valid = false;
    };

    struct ClassTraits{
        b8 isCopyConstructible = true;
        b8 isMoveConstructible = true;
        b8 isCopyAssignable = true;
        b8 isMoveAssignable = true;
        b8 hasExplicitDestructor = false;
        b8 hasUserDeclaredCopyCtor = false;
        b8 hasUserDeclaredCopyAssign = false;
        b8 hasUserDeclaredMoveCtor = false;
        b8 hasUserDeclaredMoveAssign = false;
        MVector<CXCursor> publicBaseRecords;

        explicit ClassTraits(MetaArena& arena)noexcept : publicBaseRecords(arena){}
    };

    struct ClassImportDedupe{
        HashSet<MStringView, MetaArena, MStringViewHash> seenFields;
        HashMap<MStringView, MVector<const MethodDecl*>, MetaArena, MStringViewHash> methodsByName;
        HashSet<CursorKey, MetaArena, CursorKeyHash> visitedBaseRecords;

        ClassImportDedupe(MetaArena& arena, const ClassDecl& cls)
            : seenFields(arena)
            , methodsByName(arena)
            , visitedBaseRecords(arena)
        {
            for(const FieldDecl* field : cls.fields){
                if(field && !field->name.empty())
                    seenFields.insert(field->name);
            }
            for(const MethodDecl* method : cls.methods){
                if(!method)
                    continue;
                auto [it, inserted] = methodsByName.emplace(method->name, MVector<const MethodDecl*>(arena));
                it.value().push_back(method);
            }
        }
    };


private:
    [[nodiscard]] b8 formatDeclName(CXCursor declCursor, MStringView& outName, MStringView& outNamespace, MStringView& outCppExportName, b8& outIsDirectEnum, b8& outIsFlag);
    [[nodiscard]] MStringView resolveRequiredHeader(CXCursor cursor);
    [[nodiscard]] MStringView mapOperatorToken(MStringView spelling)noexcept;
    [[nodiscard]] b8 hasDefaultInitializerToken(CXCursor parmCursor)const;
    [[nodiscard]] TypeRef* mapGenericContainerType(CXType type, CXType canonical, MStringView rawSpelling, MStringView sugaredLeaf, ParamPassingMode::Enum refMode, u32 depth, b8& outWasGenericContainer);
    [[nodiscard]] TypeRef* mapTypeUncached(CXType type, b8 isParamContext, u32 depth);
    [[nodiscard]] TypeRef* mapType(CXType type, b8 isParamContext, u32 depth = 0u);
    [[nodiscard]] b8 evaluateIntegralConstant(CXCursor cursor, MStringView& outText);
    [[nodiscard]] MethodDecl* buildMethodDecl(CXCursor cursor, MStringView ownerClassName, b8 isTemplate, const ClassDecl* ownerClass = nullptr);
    void visitChildrenList(CXCursor parent, MVector<CXCursor>& outChildren)const;
    void inspectBaseAndSpecialMembers(CXCursor classCursor, ClassTraits& traits, HashSet<CursorKey, MetaArena, CursorKeyHash>& visitedTraitBases, b8 isBasePass);
    void importClassMethodsFromCursor(CXCursor classCursor, ClassDecl& targetClass, b8 isBasePass, ClassImportDedupe& dedupe);
    void extractEnum(CXCursor enumCursor);
    void extractTypeAlias(CXCursor aliasCursor);
    void extractClass(CXCursor classCursor);
    void extractGlobalConstant(CXCursor varCursor);
    void extractFreeFunction(CXCursor fnCursor, b8 isTemplate);
    void visitScopeCursor(CXCursor parentCursor);

    template<typename DeclT>
    [[nodiscard]] static DeclT* FindExistingDeclForMerge(
        const MStringMap<DeclT*>& byExportName,
        const MStringMap<DeclT*>& byName,
        const MStringView adhdName,
        const MStringView cppExportName
    )noexcept{
        if(!cppExportName.empty()){
            if(const auto it = byExportName.find(cppExportName); it != byExportName.end())
                return it->second;
        }
        if(!adhdName.empty()){
            if(const auto it = byName.find(adhdName); it != byName.end())
                return it->second;
        }
        return nullptr;
    }
    [[nodiscard]] static b8 MethodSignaturesEqual(const MethodDecl& a, const MethodDecl& b)noexcept;
    void tryAddClassField(ClassDecl& targetClass, CXCursor child, CXType recordType, b8 isStatic, ClassImportDedupe& dedupe);
    void addUniqueMethod(ClassDecl& target, MethodDecl* method, ClassImportDedupe& dedupe);


private:
    MetaArena& m_arena;
    TranslationUnit& m_unit;
    MVector<MString> m_includeRoots;
    HashMap<const void*, MStringView, MetaArena, VoidPtrHash> m_headerByFile;
    MStringMap<MStringView> m_headerCache;
    HashMap<CursorKey, CachedDeclName, MetaArena, CursorKeyHash> m_declNameCache;
    HashMap<TypeCacheKey, TypeRef*, MetaArena, TypeCacheKeyHash> m_typeCache;
    MStringMap<ClassDecl*> m_classByExportName;
    MStringMap<ClassDecl*> m_classByName;
    MStringMap<EnumDecl*> m_enumByExportName;
    MStringMap<EnumDecl*> m_enumByName;
    MStringMap<TypeAliasDecl*> m_aliasByName;
    MStringMap<Stmt*> m_constantByName;
    MStringMap<u8> m_seenFreeFunctions;
    MStringMap<u8> m_seenConstants;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

