// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cpp_extern_trait_resolver.h"

#include "cpp_header_importer_internal.h"
#include "../ast/keyword_semantics.h"

#include <core/metascript/arena_names.h>
#include <global/algorithm.h>
#include <global/hash_utils.h>
#include <global/sync.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_cpp_extern_trait_resolver{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace CppHeaderImportDetail;


inline constexpr u32 s_ExternTraitCacheMagic = 0x4E574254u;
inline constexpr u32 s_ExternTraitCacheVersion = 9u;
inline constexpr usize s_MaxMemoryCacheEntries = 256u;

inline constexpr MStringView s_TraitProbeNamespace = "__nwb_adhd_trait_probe";
inline constexpr MStringView s_TraitProbeClassAliasPrefix = "__t_";
inline constexpr MStringView s_TraitProbeIfaceAliasPrefix = "__it_";
inline constexpr MStringView s_TraitProbeFlagVarPrefix = "__f_";

inline constexpr u32 s_ProbeFlagAbstract            = 1u << 0u;
inline constexpr u32 s_ProbeFlagPolymorphicBase     = 1u << 1u;
inline constexpr u32 s_ProbeFlagEmptyBase           = 1u << 2u;
inline constexpr u32 s_ProbeFlagCopyConstructible   = 1u << 3u;
inline constexpr u32 s_ProbeFlagCopyAssignable      = 1u << 4u;
inline constexpr u32 s_ProbeFlagMoveConstructible   = 1u << 5u;
inline constexpr u32 s_ProbeFlagMoveAssignable      = 1u << 6u;
inline constexpr u32 s_ProbeFlagZeroDefault         = 1u << 7u;
inline constexpr u32 s_ProbeFlagNothrowZeroDefault  = 1u << 8u;
inline constexpr u32 s_ProbeFlagArenaDefault        = 1u << 9u;
inline constexpr u32 s_ProbeFlagNothrowArenaDefault = 1u << 10u;
inline constexpr u32 s_ProbeFlagSmallValue          = 1u << 11u;
inline constexpr u32 s_ProbeFlagBinarySerializable  = 1u << 12u;
inline constexpr u32 s_ProbeFlagEqualitySwitch      = 1u << 13u;
inline constexpr u32 s_ProbeFlagResolvedComplete    = 1u << 14u;
inline constexpr u32 s_ProbeFlagResolvedIncomplete  = 1u << 15u;
inline constexpr u32 s_ProbeFlagLeadingArenaTag     = 1u << 16u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct CppParamInfo{
    b8 isConstRef = false;
    b8 isExactArenaBaseRef = false;
    b8 isDerivedArenaRef = false;
    b8 isTemplateArenaRef = false;
    b8 isTemplateParamUsedInReturn = false;
    b8 isBinaryByteView = false;
    b8 isPlatformTCharLiteralRef = false;
    b8 isRecordType = false;
};

struct CppCallableCandidate{
    CXCursor cursor = {};
    MStringView spelling;
    usize minArgs = 0u;
    usize maxArgs = 0u;
    b8 isConst = false;
    b8 isConstructor = false;
    b8 isOperator = false;
    b8 isNothrow = false;
    MVector<CppParamInfo> params;

    explicit CppCallableCandidate(MetaArena& arena)noexcept
        : params(arena)
    {}
};

struct CachedMethodOutcome{
    b8 allowsCppUnwind = true;
    u32 byteViewArgMask = 0u;
    u32 platformLiteralArgMask = 0u;
    MVector<u8> paramIsConstRef;

    explicit CachedMethodOutcome(MetaArena& arena)noexcept
        : paramIsConstRef(arena)
    {}
};

struct CachedFreeFunctionOutcome{
    b8 allowsCppUnwind = true;
    b8 injectsCallerArena = false;
    u32 byteViewArgMask = 0u;
    u32 platformLiteralArgMask = 0u;
};

struct CachedClassOutcome{
    b8 resolved = false;
    b8 isArenaOwner = false;
    u8 arenaCtorMode = ArenaCtorMode::Default;
    b8 isAbstract = false;
    b8 isPolymorphicBase = false;
    b8 isEmptyBase = false;
    b8 isCopyConstructible = true;
    b8 isMoveConstructible = true;
    b8 isCopyAssignable = true;
    b8 isMoveAssignable = true;
    b8 isSelfDefaulted = false;
    b8 isNothrowDefaultConstructible = false;
    b8 isSmallValue = false;
    b8 isBinarySerializable = false;
    b8 emitsBraceFieldInit = false;
    b8 isEqualitySwitch = false;
    MVector<CachedMethodOutcome> methods;

    explicit CachedClassOutcome(MetaArena& arena)noexcept
        : methods(arena)
    {}

    void resetIncomplete()noexcept{
        resolved = true;
        isAbstract = false;
        isPolymorphicBase = false;
        isEmptyBase = false;
        isCopyConstructible = false;
        isCopyAssignable = false;
        isMoveConstructible = false;
        isMoveAssignable = false;
        isSelfDefaulted = false;
        isNothrowDefaultConstructible = false;
        isSmallValue = false;
        isBinarySerializable = false;
        emitsBraceFieldInit = false;
        isEqualitySwitch = false;
    }
};

struct CachedInterfaceOutcome{
    MVector<CachedMethodOutcome> methods;

    explicit CachedInterfaceOutcome(MetaArena& arena)noexcept
        : methods(arena)
    {}
};

struct CachedTraitResolution{
    MetaArena arena;
    MString cacheKey;
    MVector<CachedFileStamp> watchedStamps;
    MVector<MString> projectIncludedFiles;
    MVector<CachedClassOutcome> classOutcomes;
    MVector<CachedInterfaceOutcome> ifaceOutcomes;
    MVector<CachedFreeFunctionOutcome> freeFnOutcomes;

    CachedTraitResolution()noexcept
        : arena(MetascriptArenaScope::s_ParserScratch)
        , cacheKey(arena)
        , watchedStamps(arena)
        , projectIncludedFiles(arena)
        , classOutcomes(arena)
        , ifaceOutcomes(arena)
        , freeFnOutcomes(arena)
    {}
};

struct TraitResolutionMemoryCache{
    Futex lock;
    MetaArena mapArena;
    MStringMap<GlobalUniquePtr<CachedTraitResolution>> entries;

    TraitResolutionMemoryCache()noexcept
        : mapArena(MetascriptArenaScope::s_ParserScratch)
        , entries(mapArena)
    {}
};

[[nodiscard]] TraitResolutionMemoryCache& GetMemoryCache()noexcept{
    static TraitResolutionMemoryCache s_Cache;
    return s_Cache;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 HasDisallowedProbeSyntax(const MStringView text)noexcept{
    for(const MChar ch : text){
        if(ch == ';' || ch == '\n' || ch == '\r' || ch == '{' || ch == '}')
            return true;
    }
    return false;
}

[[nodiscard]] MString NormalizeCppQualifiedName(MetaArena& arena, const MStringView raw){
    MString out(arena);
    out.reserve(raw.size() + 2u);

    usize angleDepth = 0u;
    usize parenDepth = 0u;
    for(usize i = 0u; i < raw.size(); ++i){
        const MChar ch = raw[i];
        if(ch == '<'){
            ++angleDepth;
            continue;
        }
        if(ch == '>'){
            if(angleDepth > 0u)
                --angleDepth;
            continue;
        }
        if(angleDepth > 0u)
            continue;
        if(ch == '(')
            ++parenDepth;
        else if(ch == ')' && parenDepth > 0u)
            --parenDepth;
        if(parenDepth > 0u)
            continue;
        if(ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n')
            continue;
        out.push_back(ch);
    }

    if(!out.empty() && !out.starts_with(ContractSemantics::s_CppScopeSeparator)){
        MString prefixed(ContractSemantics::s_CppScopeSeparator.data(), ContractSemantics::s_CppScopeSeparator.size(), arena);
        prefixed.append(out.data(), out.size());
        return prefixed;
    }
    return out;
}

[[nodiscard]] MString BuildPrefixedName(MetaArena& arena, const MStringView scopePrefix, const MStringView leaf){
    MString out(arena);
    out.reserve(scopePrefix.size() + ContractSemantics::s_CppScopeSeparator.size() + leaf.size());
    out.append(scopePrefix.data(), scopePrefix.size());
    out.append(ContractSemantics::s_CppScopeSeparator.data(), ContractSemantics::s_CppScopeSeparator.size());
    out.append(leaf.data(), leaf.size());
    return out;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 IsIdentChar(const MChar ch)noexcept{
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_';
}

[[nodiscard]] b8 ContainsTemplateArgToken(const MStringView text, const MStringView token)noexcept{
    if(token.empty() || text.size() < token.size() + 2u)
        return false;
    usize angleDepth = 0u;
    for(usize i = 0u; i < text.size(); ++i){
        const MChar ch = text[i];
        if(ch == '<'){
            ++angleDepth;
            continue;
        }
        if(ch == '>'){
            if(angleDepth > 0u)
                --angleDepth;
            continue;
        }
        if(angleDepth > 0u && i + token.size() <= text.size() && text.substr(i, token.size()) == token){
            const b8 leftOk = (i == 0u) || !IsIdentChar(text[i - 1u]);
            const usize endPos = i + token.size();
            const b8 rightOk = (endPos >= text.size()) || !IsIdentChar(text[endPos]);
            if(leftOk && rightOk)
                return true;
        }
    }
    return false;
}

[[nodiscard]] MChar AsciiLowerChar(const MChar ch)noexcept{
    return (ch >= 'A' && ch <= 'Z') ? static_cast<MChar>(ch + ('a' - 'A')) : ch;
}

[[nodiscard]] b8 ContainsAsciiCaseInsensitive(const MStringView text, const MStringView lowerNeedle)noexcept{
    if(lowerNeedle.empty() || text.size() < lowerNeedle.size())
        return false;
    for(usize i = 0u; i + lowerNeedle.size() <= text.size(); ++i){
        b8 matched = true;
        for(usize j = 0u; j < lowerNeedle.size(); ++j){
            if(AsciiLowerChar(text[i + j]) != lowerNeedle[j]){
                matched = false;
                break;
            }
        }
        if(matched)
            return true;
    }
    return false;
}

[[nodiscard]] b8 UnqualifiedTypeSpellingEquals(MetaArena& arena, const CXType type, const MStringView targetName){
    const MString spell = ConsumeClangString(arena, ::clang_getTypeSpelling(::clang_getUnqualifiedType(type)));
    return MStringView(spell.data(), spell.size()) == targetName;
}

[[nodiscard]] CXCursor ResolveBaseSpecifierDecl(MetaArena& arena, CXCursor baseSpecCursor);

[[nodiscard]] b8 IsClangGlobalRecordType(MetaArena& arena, const CXType type, const MStringView expectedName){
    const CXType canon = ::clang_getCanonicalType(type);
    const CXCursor rec = ::clang_getTypeDeclaration(canon);
    if(::clang_Cursor_isNull(rec) || ::clang_getCursorKind(rec) == CXCursor_NoDeclFound)
        return false;
    const MString nameStr = ConsumeClangString(arena, ::clang_getCursorSpelling(rec));
    if(nameStr != expectedName)
        return false;
    const CXCursor parent = ::clang_getCursorSemanticParent(rec);
    const CXCursorKind pk = ::clang_getCursorKind(parent);
    return pk == CXCursor_TranslationUnit || pk == CXCursor_LinkageSpec;
}

[[nodiscard]] b8 IsClangTypeExactArenaBase(MetaArena& arena, const CXType type){
    return IsClangGlobalRecordType(arena, type, BuiltinTypeSemantics::ArenaBaseTypeName());
}

[[nodiscard]] b8 IsClangTypeDerivedFromArenaBase(MetaArena& arena, const CXType type, const u32 depth = 0u){
    if(depth >= 8u)
        return false;
    const CXType canon = ::clang_getCanonicalType(type);
    if(IsClangTypeExactArenaBase(arena, canon))
        return true;
    const CXCursor rec = ::clang_getTypeDeclaration(canon);
    if(::clang_Cursor_isNull(rec) || ::clang_getCursorKind(rec) == CXCursor_NoDeclFound)
        return false;
    const CXCursor def = ::clang_getCursorDefinition(rec);
    const CXCursor inspectCursor = ::clang_Cursor_isNull(def) ? rec : def;
    MVector<CXCursor> children(arena);
    VisitDirectChildren(inspectCursor, children);
    for(const CXCursor ch : children){
        if(::clang_getCursorKind(ch) != CXCursor_CXXBaseSpecifier)
            continue;
        if(IsClangTypeDerivedFromArenaBase(arena, ::clang_getCursorType(ch), depth + 1u))
            return true;
        const CXCursor baseDecl = ResolveBaseSpecifierDecl(arena, ch);
        if(!::clang_Cursor_isNull(baseDecl) && IsClangTypeDerivedFromArenaBase(arena, ::clang_getCursorType(baseDecl), depth + 1u))
            return true;
    }
    return false;
}

void CollectEnclosingTemplateTypeParams(MetaArena& arena, CXCursor cursor, MVector<MString>& outNames){
    outNames.clear();
    for(u32 depth = 0u; depth < 8u && !::clang_Cursor_isNull(cursor); ++depth){
        const CXCursorKind ck = ::clang_getCursorKind(cursor);
        if(ck == CXCursor_TranslationUnit || ck == CXCursor_Namespace)
            break;
        if(ck == CXCursor_FunctionTemplate || ck == CXCursor_ClassTemplate){
            MVector<CXCursor> children(arena);
            VisitDirectChildren(cursor, children);
            for(const CXCursor ch : children){
                if(::clang_getCursorKind(ch) != CXCursor_TemplateTypeParameter)
                    continue;
                MString nameStr = ConsumeClangString(arena, ::clang_getCursorSpelling(ch));
                if(!nameStr.empty())
                    outNames.push_back(Move(nameStr));
            }
        }
        cursor = ::clang_getCursorSemanticParent(cursor);
    }
}

void PopulateCallableParams(MetaArena& arena, const CXCursor cursor, const b8 isTemplate, CppCallableCandidate& outCand){
    MVector<CXCursor> paramCursors(arena);
    CollectCallableParamCursors(arena, cursor, isTemplate, paramCursors);

    MVector<MString> tplTypeNames(arena);
    CollectEnclosingTemplateTypeParams(arena, cursor, tplTypeNames);

    MString resultSpelling(arena);
    if(!tplTypeNames.empty()){
        const CXType resType = ::clang_getCursorResultType(cursor);
        resultSpelling = ConsumeClangString(arena, ::clang_getTypeSpelling(resType));
    }
    const MStringView resultSpellingView(resultSpelling.data(), resultSpelling.size());

    b8 isUnboundedVariadic = (::clang_Cursor_isVariadic(cursor) != 0u);
    usize minArgs = 0u;
    b8 seenDefault = false;
    outCand.params.reserve(paramCursors.size());

    for(usize idx = 0u; idx < paramCursors.size(); ++idx){
        const CXCursor parm = paramCursors[idx];
        const CXType rawType = ::clang_getCursorType(parm);
        const CXType canonType = ::clang_getCanonicalType(rawType);
        const MString spelling = ConsumeClangString(arena, ::clang_getTypeSpelling(rawType));
        if(MStringView(spelling.data(), spelling.size()).find(CppImporterSemantics::s_VariadicPackToken) != MStringView::npos){
            isUnboundedVariadic = true;
            break;
        }

        CppParamInfo pinfo{};
        pinfo.isConstRef = IsClangTypeConstLValueRef(rawType);

        if(canonType.kind == CXType_LValueReference){
            const CXType pointeeCanon = ::clang_getPointeeType(canonType);
            const CXType rawPointee = (rawType.kind == CXType_LValueReference) ? ::clang_getPointeeType(rawType) : pointeeCanon;
            if(::clang_isConstQualifiedType(pointeeCanon) != 0u
                && (rawPointee.kind == CXType_ConstantArray || rawPointee.kind == CXType_DependentSizedArray
                    || pointeeCanon.kind == CXType_ConstantArray || pointeeCanon.kind == CXType_DependentSizedArray)
            ){
                const CXType elemSource = (rawPointee.kind == CXType_ConstantArray || rawPointee.kind == CXType_DependentSizedArray) ? rawPointee : pointeeCanon;
                const CXType elemType = ::clang_getArrayElementType(elemSource);
                const CXCursor elemDecl = ::clang_getTypeDeclaration(elemType);
                const MString elemName = !::clang_Cursor_isNull(elemDecl)
                    ? ConsumeClangString(arena, ::clang_getCursorSpelling(elemDecl))
                    : ConsumeClangString(arena, ::clang_getTypeSpelling(::clang_getUnqualifiedType(elemType)));
                if(elemName == "tchar")
                    pinfo.isPlatformTCharLiteralRef = true;
            }
            if(::clang_isConstQualifiedType(pointeeCanon) == 0u){
                if(IsClangTypeExactArenaBase(arena, pointeeCanon)){
                    pinfo.isExactArenaBaseRef = true;
                }
                else if(IsClangTypeDerivedFromArenaBase(arena, pointeeCanon)){
                    pinfo.isDerivedArenaRef = true;
                }
                else if(!tplTypeNames.empty()){
                    const MString rawPointeeSpell = ConsumeClangString(arena, ::clang_getTypeSpelling(::clang_getUnqualifiedType(rawPointee)));
                    const MStringView basePointee(rawPointeeSpell.data(), rawPointeeSpell.size());
                    for(const MString& tplName : tplTypeNames){
                        const MStringView tplView(tplName.data(), tplName.size());
                        if(basePointee == tplView){
                            const b8 usedAsReturnTplArg = !resultSpellingView.empty() && ContainsTemplateArgToken(resultSpellingView, tplView);
                            if(usedAsReturnTplArg)
                                pinfo.isTemplateParamUsedInReturn = true;
                            if(usedAsReturnTplArg || ContainsAsciiCaseInsensitive(tplView, "arena"))
                                pinfo.isTemplateArenaRef = true;
                            break;
                        }
                    }
                }
            }
        }

        CXType valueCanon = canonType;
        if(canonType.kind == CXType_LValueReference || canonType.kind == CXType_RValueReference)
            valueCanon = ::clang_getCanonicalType(::clang_getPointeeType(canonType));
        if(IsClangGlobalRecordType(arena, valueCanon, BuiltinTypeSemantics::BinaryByteViewTypeName()))
            pinfo.isBinaryByteView = true;
        else if(valueCanon.kind == CXType_Record)
            pinfo.isRecordType = true;

        outCand.params.push_back(pinfo);

        const b8 hasDefault = HasDefaultInitializerExpr(parm);
        if(!hasDefault && !seenDefault)
            minArgs = idx + 1u;
        else if(hasDefault)
            seenDefault = true;
    }

    outCand.minArgs = minArgs;
    outCand.maxArgs = isUnboundedVariadic ? Limit<usize>::s_Max : outCand.params.size();
}

[[nodiscard]] CppCallableCandidate BuildCandidateFromCursor(MetaArena& arena, const CXCursor cursor, const MStringView forcedSpelling = {}){
    const CXCursorKind kind = ::clang_getCursorKind(cursor);
    const b8 isTemplate = (kind == CXCursor_FunctionTemplate);
    const CXCursorKind effectiveKind = isTemplate ? ::clang_getTemplateCursorKind(cursor) : kind;

    CppCallableCandidate cand(arena);
    cand.cursor = cursor;
    if(!forcedSpelling.empty()){
        cand.spelling = forcedSpelling;
    }
    else{
        const MString rawSpelling = ConsumeClangString(arena, ::clang_getCursorSpelling(cursor));
        cand.spelling = InternArenaString(arena, MStringView(rawSpelling.data(), rawSpelling.size()));
    }
    cand.isConstructor = (effectiveKind == CXCursor_Constructor);
    cand.isOperator = !cand.isConstructor && cand.spelling.starts_with(KeywordSemantics::s_Operator);
    if(cand.isOperator)
        cand.spelling = MapCppOperatorSpelling(cand.spelling);
    cand.isConst = (::clang_CXXMethod_isConst(cursor) != 0u);
    cand.isNothrow = IsClangCursorNothrow(cursor);
    PopulateCallableParams(arena, cursor, isTemplate, cand);
    return cand;
}

struct MethodHideKey{
    MStringView name;
    b8 isConst = false;
    b8 isOperator = false;
    usize paramCount = 0u;

    [[nodiscard]] b8 operator==(const MethodHideKey& rhs)const noexcept{
        return name == rhs.name && isConst == rhs.isConst && isOperator == rhs.isOperator && paramCount == rhs.paramCount;
    }
};

struct MethodHideKeyHash{
    [[nodiscard]] usize operator()(const MethodHideKey& key)const noexcept{
        u64 h = UpdateFnv64TextExact(s_Fnv64OffsetBasis, key.name);
        Fnv64AppendValue(h, static_cast<u8>((key.isConst ? 1u : 0u) | (key.isOperator ? 2u : 0u)));
        Fnv64AppendValue(h, static_cast<u64>(key.paramCount));
        return static_cast<usize>(h);
    }
};

[[nodiscard]] CXCursor UnwrapToRecordOrClassTemplate(MetaArena& arena, CXCursor cursor);

[[nodiscard]] CXCursor ResolveBaseSpecifierDecl(MetaArena& arena, const CXCursor baseSpecCursor){
    CXCursor baseDecl = ::clang_getTypeDeclaration(::clang_getCursorType(baseSpecCursor));
    if(::clang_Cursor_isNull(baseDecl) || ::clang_getCursorKind(baseDecl) == CXCursor_NoDeclFound)
        baseDecl = ::clang_getCursorReferenced(baseSpecCursor);

    if(::clang_Cursor_isNull(baseDecl) || ::clang_getCursorKind(baseDecl) == CXCursor_NoDeclFound){
        MVector<CXCursor> children(arena);
        VisitDirectChildren(baseSpecCursor, children);
        for(const CXCursor child : children){
            const CXCursorKind ck = ::clang_getCursorKind(child);
            if(ck != CXCursor_TemplateRef && ck != CXCursor_TypeRef)
                continue;
            const CXCursor ref = ::clang_getCursorReferenced(child);
            if(!::clang_Cursor_isNull(ref) && ::clang_getCursorKind(ref) != CXCursor_NoDeclFound){
                baseDecl = ref;
                break;
            }
        }
    }

    if(::clang_Cursor_isNull(baseDecl) || ::clang_getCursorKind(baseDecl) == CXCursor_NoDeclFound)
        return {};

    const CXCursor unwrapped = UnwrapToRecordOrClassTemplate(arena, baseDecl);
    return ::clang_Cursor_isNull(unwrapped) ? baseDecl : unwrapped;
}

void CollectRecordCallableHierarchy(
    MetaArena& arena,
    const CXCursor recordCursor,
    const b8 isBasePass,
    HashSet<CursorKey, MetaArena, CursorKeyHash>& visitedBases,
    HashSet<MethodHideKey, MetaArena, MethodHideKeyHash>& derivedMethods,
    MVector<CppCallableCandidate>& outMethods,
    MVector<CppCallableCandidate>& outConstructors
){
    if(::clang_Cursor_isNull(recordCursor))
        return;

    CXCursor defCursor = ::clang_getCursorDefinition(recordCursor);
    if(::clang_Cursor_isNull(defCursor))
        defCursor = recordCursor;

    if(isBasePass && !visitedBases.insert(MakeCursorKey(defCursor)).second)
        return;

    MVector<CXCursor> children(arena);
    VisitDirectChildren(defCursor, children);

    MVector<CXCursor> publicBases(arena);
    MVector<MethodHideKey> currentLevelKeys(arena);

    for(const CXCursor child : children){
        const CXCursorKind ck = ::clang_getCursorKind(child);
        const CX_CXXAccessSpecifier acc = ::clang_getCXXAccessSpecifier(child);

        if(ck == CXCursor_CXXBaseSpecifier){
            if(acc == CX_CXXPublic){
                const CXCursor baseRef = ResolveBaseSpecifierDecl(arena, child);
                if(!::clang_Cursor_isNull(baseRef))
                    publicBases.push_back(baseRef);
            }
            continue;
        }

        if(acc != CX_CXXPublic)
            continue;

        if(ck == CXCursor_Destructor || ck == CXCursor_ConversionFunction)
            continue;

        if(::clang_CXXMethod_isDeleted(child))
            continue;

        const b8 isTemplate = (ck == CXCursor_FunctionTemplate);
        const CXCursorKind effectiveKind = isTemplate ? ::clang_getTemplateCursorKind(child) : ck;

        if(effectiveKind == CXCursor_Constructor){
            if(!isBasePass && !::clang_CXXConstructor_isCopyConstructor(child) && !::clang_CXXConstructor_isMoveConstructor(child))
                outConstructors.push_back(BuildCandidateFromCursor(arena, child));
            continue;
        }

        if(effectiveKind == CXCursor_CXXMethod){
            if(::clang_CXXMethod_isCopyAssignmentOperator(child) || ::clang_CXXMethod_isMoveAssignmentOperator(child))
                continue;
            CppCallableCandidate cand = BuildCandidateFromCursor(arena, child);
            if(cand.spelling.empty())
                continue;
            const MethodHideKey key{ cand.spelling, cand.isConst, cand.isOperator, cand.params.size() };
            if(isBasePass && derivedMethods.find(key) != derivedMethods.end())
                continue;
            currentLevelKeys.push_back(key);
            outMethods.push_back(Move(cand));
        }
    }

    for(const MethodHideKey& key : currentLevelKeys)
        derivedMethods.insert(key);

    for(const CXCursor baseCursor : publicBases)
        CollectRecordCallableHierarchy(arena, baseCursor, true, visitedBases, derivedMethods, outMethods, outConstructors);
}

[[nodiscard]] b8 IsCandidateTypeCompatible(const CppCallableCandidate& cand, const MethodDecl& adhdMethod, const usize offset)noexcept{
    for(usize pi = 0u; pi < adhdMethod.params.size() && (offset + pi) < cand.params.size(); ++pi){
        const ParamDecl& ap = adhdMethod.params[pi];
        const CppParamInfo& cp = cand.params[offset + pi];
        const TypeRef* pt = ap.type;
        if(cp.isExactArenaBaseRef || cp.isDerivedArenaRef || cp.isTemplateArenaRef){
            if(ap.mode != ParamPassingMode::Mut)
                return false;
            if(pt && !pt->isAuto){
                if(BuiltinTypeSemantics::IsScalarType(pt->name)
                    || FindStaticBuiltinObjectType(pt->name) != nullptr
                    || FindBuiltinGenericType(pt->name) != nullptr)
                    return false;
            }
        }
        if(!pt || pt->isAuto)
            continue;
        if(BuiltinTypeSemantics::IsScalarType(pt->name) && cp.isRecordType)
            return false;
    }
    return true;
}

void SelectArityMatchedCandidates(
    const Span<const CppCallableCandidate* const> candidates,
    const MethodDecl& adhdMethod,
    const usize offset,
    MVector<const CppCallableCandidate*>& outMatched
){
    outMatched.clear();
    if(!adhdMethod.isVariadic){
        const usize expectedArgs = adhdMethod.params.size() + offset;
        for(const CppCallableCandidate* cand : candidates){
            if(expectedArgs >= cand->minArgs && expectedArgs <= cand->maxArgs && IsCandidateTypeCompatible(*cand, adhdMethod, offset))
                outMatched.push_back(cand);
        }
    }
    if(outMatched.empty())
        outMatched.assign(candidates.begin(), candidates.end());
}

[[nodiscard]] b8 AnyCandidateAllowsUnwind(const Span<const CppCallableCandidate* const> candidates)noexcept{
    for(const CppCallableCandidate* cand : candidates){
        if(!cand->isNothrow)
            return true;
    }
    return false;
}

void CollectCallableParamLoweringMasks(
    const Span<const CppCallableCandidate* const> candidates,
    const MethodDecl& adhdMethod,
    const usize offset,
    u32& outByteViewMask,
    u32& outPlatformLiteralMask
)noexcept{
    outByteViewMask = 0u;
    outPlatformLiteralMask = 0u;
    if(adhdMethod.isVariadic)
        return;
    for(usize pi = 0u; pi < adhdMethod.params.size() && pi < 32u; ++pi){
        const TypeRef* pt = adhdMethod.params[pi].type;
        const b8 alreadyByteView = pt && !pt->isAuto && pt->name == BuiltinTypeSemantics::BinaryByteViewTypeName();
        b8 hasParamCandidate = false;
        b8 allByteView = !alreadyByteView;
        b8 allPlatformLiteral = true;
        for(const CppCallableCandidate* cand : candidates){
            if(offset + pi >= cand->params.size())
                continue;
            hasParamCandidate = true;
            const CppParamInfo& cp = cand->params[offset + pi];
            if(!cp.isBinaryByteView)
                allByteView = false;
            if(!cp.isPlatformTCharLiteralRef)
                allPlatformLiteral = false;
        }
        if(hasParamCandidate && allByteView)
            outByteViewMask |= (1u << pi);
        if(hasParamCandidate && allPlatformLiteral)
            outPlatformLiteralMask |= (1u << pi);
    }
}

void ApplyMethodTraitMatches(
    const MVector<CppCallableCandidate>& methods,
    const MVector<CppCallableCandidate>& constructors,
    const MethodDecl& adhdMethod,
    CachedMethodOutcome& outOutcome,
    MetaArena& scratchArena
){
    outOutcome.allowsCppUnwind = true;
    outOutcome.byteViewArgMask = 0u;
    outOutcome.platformLiteralArgMask = 0u;
    outOutcome.paramIsConstRef.assign(adhdMethod.params.size(), 0u);

    const MVector<CppCallableCandidate>& pool = adhdMethod.isConstructor ? constructors : methods;
    if(pool.empty())
        return;

    MVector<const CppCallableCandidate*> nameMatches(scratchArena);
    for(const CppCallableCandidate& cand : pool){
        if(adhdMethod.isConstructor){
            nameMatches.push_back(&cand);
        }
        else{
            if(cand.isOperator != adhdMethod.isOperator || cand.spelling != adhdMethod.name)
                continue;
            nameMatches.push_back(&cand);
        }
    }
    if(nameMatches.empty())
        return;

    MVector<const CppCallableCandidate*> constMatches(scratchArena);
    for(const CppCallableCandidate* cand : nameMatches){
        if(cand->isConst == adhdMethod.isConst)
            constMatches.push_back(cand);
    }
    if(constMatches.empty() && !adhdMethod.isConst)
        constMatches = Move(nameMatches);
    if(constMatches.empty())
        return;

    MVector<const CppCallableCandidate*> finalMatches(scratchArena);
    SelectArityMatchedCandidates(Span<const CppCallableCandidate* const>(constMatches.data(), constMatches.size()), adhdMethod, 0u, finalMatches);
    const Span<const CppCallableCandidate* const> finalSpan(finalMatches.data(), finalMatches.size());
    outOutcome.allowsCppUnwind = AnyCandidateAllowsUnwind(finalSpan);
    CollectCallableParamLoweringMasks(finalSpan, adhdMethod, 0u, outOutcome.byteViewArgMask, outOutcome.platformLiteralArgMask);

    if(!adhdMethod.isVariadic){
        for(const CppCallableCandidate* cand : finalMatches){
            for(usize pi = 0u; pi < adhdMethod.params.size() && pi < cand->params.size(); ++pi){
                if(adhdMethod.params[pi].mode == ParamPassingMode::In && cand->params[pi].isConstRef)
                    outOutcome.paramIsConstRef[pi] = 1u;
            }
        }
    }
}

void CollectCandidatePointers(const Span<const CppCallableCandidate> candidates, MVector<const CppCallableCandidate*>& outPtrs){
    outPtrs.clear();
    outPtrs.reserve(candidates.size());
    for(const CppCallableCandidate& c : candidates)
        outPtrs.push_back(&c);
}

void ResolveFreeFunctionOutcome(
    MetaArena& scratchArena,
    const Span<const CppCallableCandidate> candidates,
    const MethodDecl& fn,
    CachedFreeFunctionOutcome& outOutcome
){
    outOutcome.allowsCppUnwind = true;
    outOutcome.injectsCallerArena = false;
    outOutcome.byteViewArgMask = 0u;
    outOutcome.platformLiteralArgMask = 0u;

    if(candidates.empty())
        return;

    if(fn.isVariadic){
        MVector<const CppCallableCandidate*> allCands(scratchArena);
        CollectCandidatePointers(candidates, allCands);
        outOutcome.allowsCppUnwind = AnyCandidateAllowsUnwind(Span<const CppCallableCandidate* const>(allCands.data(), allCands.size()));
        return;
    }

    MVector<const CppCallableCandidate*> directMatches(scratchArena);
    MVector<const CppCallableCandidate*> arenaMatches(scratchArena);
    const usize expectedDirect = fn.params.size();
    const usize expectedArena = fn.params.size() + 1u;
    b8 anyPreferredArenaMatch = false;

    for(const CppCallableCandidate& cand : candidates){
        if(expectedDirect >= cand.minArgs && expectedDirect <= cand.maxArgs && IsCandidateTypeCompatible(cand, fn, 0u))
            directMatches.push_back(&cand);
        if(!cand.params.empty()
            && (cand.params[0].isExactArenaBaseRef || cand.params[0].isTemplateArenaRef)
            && expectedArena >= cand.minArgs
            && expectedArena <= cand.maxArgs
            && IsCandidateTypeCompatible(cand, fn, 1u)
        ){
            arenaMatches.push_back(&cand);
            if(cand.params[0].isExactArenaBaseRef || cand.params[0].isTemplateParamUsedInReturn)
                anyPreferredArenaMatch = true;
        }
    }

    usize offset = 0u;
    MVector<const CppCallableCandidate*> chosen(scratchArena);
    if(!arenaMatches.empty() && (directMatches.empty() || anyPreferredArenaMatch)){
        outOutcome.injectsCallerArena = true;
        offset = 1u;
        chosen = Move(arenaMatches);
    }
    else if(!directMatches.empty()){
        chosen = Move(directMatches);
    }
    else{
        CollectCandidatePointers(candidates, chosen);
    }

    const Span<const CppCallableCandidate* const> chosenSpan(chosen.data(), chosen.size());
    outOutcome.allowsCppUnwind = AnyCandidateAllowsUnwind(chosenSpan);
    CollectCallableParamLoweringMasks(chosenSpan, fn, offset, outOutcome.byteViewArgMask, outOutcome.platformLiteralArgMask);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct SymbolIndex{
    MStringMap<CXCursor> classTemplates;
    MStringMap<MVector<CppCallableCandidate>> freeFunctions;
    MStringMap<CXCursor> typeRecords;

    explicit SymbolIndex(MetaArena& arena)noexcept
        : classTemplates(arena)
        , freeFunctions(arena)
        , typeRecords(arena)
    {}
};

void IndexScopeSymbols(MetaArena& arena, const CXCursor parentCursor, const MStringView currentPrefix, const b8 isRecordScope, SymbolIndex& index){
    MVector<CXCursor> children(arena);
    VisitDirectChildren(parentCursor, children);
    for(const CXCursor c : children){
        if(!isRecordScope && ::clang_Location_isInSystemHeader(::clang_getCursorLocation(c)))
            continue;
        if(isRecordScope && ::clang_getCXXAccessSpecifier(c) != CX_CXXPublic)
            continue;

        const CXCursorKind ck = ::clang_getCursorKind(c);
        if(!isRecordScope && ck == CXCursor_Namespace){
            const MString nsStr = ConsumeClangString(arena, ::clang_getCursorSpelling(c));
            const MStringView nsView(nsStr.data(), nsStr.size());
            if(nsView == s_TraitProbeNamespace)
                continue;
            if(nsView.empty()){
                IndexScopeSymbols(arena, c, currentPrefix, false, index);
            }
            else{
                const MString nextPrefix = BuildPrefixedName(arena, currentPrefix, nsView);
                IndexScopeSymbols(arena, c, MStringView(nextPrefix.data(), nextPrefix.size()), false, index);
            }
            continue;
        }
        if(!isRecordScope && (ck == CXCursor_LinkageSpec || ck == CXCursor_UnexposedDecl)){
            IndexScopeSymbols(arena, c, currentPrefix, false, index);
            continue;
        }
        if(ck == CXCursor_StructDecl || ck == CXCursor_ClassDecl || ck == CXCursor_ClassTemplate){
            const MString nameStr = ConsumeClangString(arena, ::clang_getCursorSpelling(c));
            const MStringView nameView(nameStr.data(), nameStr.size());
            if(nameView.empty() || ::clang_Cursor_isAnonymous(c))
                continue;
            const MString qual = BuildPrefixedName(arena, currentPrefix, nameView);
            const MStringView qualView = InternArenaString(arena, qual);
            if(ck == CXCursor_ClassTemplate){
                const b8 isDef = ::clang_isCursorDefinition(c) != 0u;
                if(isRecordScope || isDef || index.classTemplates.find(qualView) == index.classTemplates.end())
                    index.classTemplates.insert_or_assign(MString(qualView.data(), qualView.size(), arena), c);
            }
            if(::clang_isCursorDefinition(c) || index.typeRecords.find(qualView) == index.typeRecords.end())
                index.typeRecords.insert_or_assign(MString(qualView.data(), qualView.size(), arena), c);
            if(isRecordScope || ::clang_isCursorDefinition(c) || ck == CXCursor_ClassTemplate)
                IndexScopeSymbols(arena, c, qualView, true, index);
            continue;
        }
        if(ck == CXCursor_TypeAliasTemplateDecl){
            const MString nameStr = ConsumeClangString(arena, ::clang_getCursorSpelling(c));
            const MStringView nameView(nameStr.data(), nameStr.size());
            if(!nameView.empty())
                index.classTemplates.insert_or_assign(BuildPrefixedName(arena, currentPrefix, nameView), c);
            continue;
        }
        if(ck == CXCursor_TypeAliasDecl || ck == CXCursor_TypedefDecl){
            const MString nameStr = ConsumeClangString(arena, ::clang_getCursorSpelling(c));
            const MStringView nameView(nameStr.data(), nameStr.size());
            if(!nameView.empty())
                index.typeRecords.insert_or_assign(BuildPrefixedName(arena, currentPrefix, nameView), c);
            continue;
        }
        if(!isRecordScope && (ck == CXCursor_FunctionDecl || (ck == CXCursor_FunctionTemplate && ::clang_getTemplateCursorKind(c) == CXCursor_FunctionDecl))){
            const MString nameStr = ConsumeClangString(arena, ::clang_getCursorSpelling(c));
            const MStringView nameView(nameStr.data(), nameStr.size());
            if(nameView.empty())
                continue;
            const MString qual = BuildPrefixedName(arena, currentPrefix, nameView);
            auto [it, inserted] = index.freeFunctions.emplace(Move(qual), MVector<CppCallableCandidate>(arena));
            it.value().push_back(BuildCandidateFromCursor(arena, c));
            continue;
        }
        if(isRecordScope && (ck == CXCursor_CXXMethod || (ck == CXCursor_FunctionTemplate && ::clang_getTemplateCursorKind(c) == CXCursor_CXXMethod))){
            if(::clang_CXXMethod_isDeleted(c) || !::clang_CXXMethod_isStatic(c))
                continue;
            const MString rawName = ConsumeClangString(arena, ::clang_getCursorSpelling(c));
            const MStringView fnView(rawName.data(), rawName.size());
            if(fnView.empty())
                continue;
            const MString fnQual = BuildPrefixedName(arena, currentPrefix, fnView);
            auto [it, inserted] = index.freeFunctions.emplace(Move(fnQual), MVector<CppCallableCandidate>(arena));
            it.value().push_back(BuildCandidateFromCursor(arena, c));
        }
    }
}

[[nodiscard]] CXCursor UnwrapToRecordOrClassTemplate(MetaArena& arena, CXCursor cursor){
    for(u32 depth = 0u; depth < 8u && !::clang_Cursor_isNull(cursor); ++depth){
        const CXCursorKind ck = ::clang_getCursorKind(cursor);
        if(ck == CXCursor_StructDecl || ck == CXCursor_ClassDecl || ck == CXCursor_ClassTemplate){
            const CXCursor def = ::clang_getCursorDefinition(cursor);
            return ::clang_Cursor_isNull(def) ? cursor : def;
        }
        if(ck == CXCursor_TypeAliasDecl || ck == CXCursor_TypedefDecl){
            const CXType underlying = ::clang_getCanonicalType(::clang_getTypedefDeclUnderlyingType(cursor));
            CXCursor target = ::clang_getTypeDeclaration(underlying);
            if(!::clang_Cursor_isNull(target)){
                cursor = target;
                continue;
            }
            CXCursor templateRef{};
            ::clang_visitChildren(cursor, [](CXCursor ch, CXCursor, CXClientData data) -> CXChildVisitResult{
                if(::clang_getCursorKind(ch) == CXCursor_TemplateRef){
                    *static_cast<CXCursor*>(data) = ::clang_getCursorReferenced(ch);
                    return CXChildVisit_Break;
                }
                return CXChildVisit_Continue;
            }, &templateRef);
            if(!::clang_Cursor_isNull(templateRef)){
                cursor = templateRef;
                continue;
            }
            break;
        }
        if(ck == CXCursor_TypeAliasTemplateDecl){
            MVector<CXCursor> children(arena);
            VisitDirectChildren(cursor, children);
            CXCursor nextCursor{};
            for(const CXCursor child : children){
                if(::clang_getCursorKind(child) == CXCursor_TypeAliasDecl){
                    nextCursor = child;
                    break;
                }
                if(::clang_getCursorKind(child) == CXCursor_TemplateRef){
                    nextCursor = ::clang_getCursorReferenced(child);
                    break;
                }
            }
            if(!::clang_Cursor_isNull(nextCursor)){
                cursor = nextCursor;
                continue;
            }
            break;
        }
        break;
    }
    return {};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct TemplateClassTraits : CppRecordSpecialMemberTraits{
    b8 hasAccessibleArenaArgCtor = false;
    b8 arenaArgCtorNothrow = false;
    b8 hasReferenceOrNoDefaultField = false;
    b8 isEqualitySwitch = false;
};

void CollectSubtreeTemplateAndParamRefs(
    MetaArena& arena,
    const CXCursor cursor,
    const MStringView paramName,
    b8& hasTemplateRef,
    b8& hasParamTypeRef,
    const u32 depth = 0u
){
    if(depth >= 6u || (hasTemplateRef && hasParamTypeRef))
        return;
    MVector<CXCursor> children(arena);
    VisitDirectChildren(cursor, children);
    for(const CXCursor ch : children){
        const CXCursorKind ck = ::clang_getCursorKind(ch);
        if(ck == CXCursor_TemplateRef)
            hasTemplateRef = true;
        else if(ck == CXCursor_TypeRef){
            const MString refSpell = ConsumeClangString(arena, ::clang_getCursorSpelling(ch));
            if(MStringView(refSpell.data(), refSpell.size()) == paramName)
                hasParamTypeRef = true;
        }
        if(hasTemplateRef && hasParamTypeRef)
            return;
        CollectSubtreeTemplateAndParamRefs(arena, ch, paramName, hasTemplateRef, hasParamTypeRef, depth + 1u);
    }
}

[[nodiscard]] b8 SubtreeHasTemplateInstantiationUsingParam(MetaArena& arena, const CXCursor cursor, const MStringView paramName){
    const MString typeSpell = ConsumeClangString(arena, ::clang_getTypeSpelling(::clang_getCursorType(cursor)));
    if(ContainsTemplateArgToken(MStringView(typeSpell.data(), typeSpell.size()), paramName))
        return true;
    b8 hasTemplateRef = false;
    b8 hasParamTypeRef = false;
    CollectSubtreeTemplateAndParamRefs(arena, cursor, paramName, hasTemplateRef, hasParamTypeRef);
    return hasTemplateRef && hasParamTypeRef;
}

[[nodiscard]] b8 IsSingleRequiredArenaClassTemplate(MetaArena& arena, const CXCursor tplCursor){
    MVector<CXCursor> children(arena);
    VisitDirectChildren(tplCursor, children);
    usize totalParams = 0u;
    b8 firstIsTypeParam = false;
    b8 hasUnusableParam = false;
    MString param0Name(arena);

    for(const CXCursor ch : children){
        const CXCursorKind ck = ::clang_getCursorKind(ch);
        if(ck != CXCursor_TemplateTypeParameter && ck != CXCursor_NonTypeTemplateParameter && ck != CXCursor_TemplateTemplateParameter)
            continue;
        if(totalParams == 0u){
            if(ck == CXCursor_TemplateTypeParameter){
                param0Name = ConsumeClangString(arena, ::clang_getCursorSpelling(ch));
                firstIsTypeParam = !param0Name.empty();
            }
            if(!firstIsTypeParam)
                hasUnusableParam = true;
        }
        else{
            MVector<CXCursor> defaultChildren(arena);
            VisitDirectChildren(ch, defaultChildren);
            if(defaultChildren.empty())
                hasUnusableParam = true;
        }
        ++totalParams;
    }
    if(totalParams == 0u || !firstIsTypeParam || hasUnusableParam)
        return false;

    const MStringView param0View(param0Name.data(), param0Name.size());
    b8 usesParamAsArena = false;

    for(const CXCursor ch : children){
        const CXCursorKind ck = ::clang_getCursorKind(ch);
        if(ck == CXCursor_FieldDecl){
            const CXType ftype = ::clang_getCursorType(ch);
            if(ftype.kind != CXType_LValueReference
                && ftype.kind != CXType_RValueReference
                && ftype.kind != CXType_Pointer
                && UnqualifiedTypeSpellingEquals(arena, ftype, param0View)
            ){
                return false;
            }
            if(SubtreeHasTemplateInstantiationUsingParam(arena, ch, param0View))
                usesParamAsArena = true;
        }
        else if(ck == CXCursor_CXXBaseSpecifier){
            if(SubtreeHasTemplateInstantiationUsingParam(arena, ch, param0View))
                usesParamAsArena = true;
        }
        else if(ck == CXCursor_TypeAliasDecl || ck == CXCursor_TypedefDecl){
            const MString aliasName = ConsumeClangString(arena, ::clang_getCursorSpelling(ch));
            if(aliasName == "uses_leading_arena_t" && UnqualifiedTypeSpellingEquals(arena, ::clang_getTypedefDeclUnderlyingType(ch), param0View))
                usesParamAsArena = true;
        }
        else if((ck == CXCursor_Constructor || (ck == CXCursor_FunctionTemplate && ::clang_getTemplateCursorKind(ch) == CXCursor_Constructor))
            && ::clang_getCXXAccessSpecifier(ch) == CX_CXXPublic
            && ::clang_CXXMethod_isDeleted(ch) == 0u
            && !::clang_CXXConstructor_isCopyConstructor(ch)
            && !::clang_CXXConstructor_isMoveConstructor(ch)
        ){
            MVector<CXCursor> ctorChildren(arena);
            VisitDirectChildren(ch, ctorChildren);
            for(const CXCursor pch : ctorChildren){
                if(::clang_getCursorKind(pch) != CXCursor_ParmDecl)
                    continue;
                const CXType ptype = ::clang_getCursorType(pch);
                if(ptype.kind == CXType_LValueReference){
                    const CXType pointee = ::clang_getPointeeType(ptype);
                    if(::clang_isConstQualifiedType(pointee) == 0u && UnqualifiedTypeSpellingEquals(arena, pointee, param0View))
                        usesParamAsArena = true;
                }
                if(SubtreeHasTemplateInstantiationUsingParam(arena, pch, param0View))
                    usesParamAsArena = true;
            }
        }
    }

    return usesParamAsArena;
}

[[nodiscard]] b8 HasPointerOrReferenceFieldRecursive(MetaArena& arena, const CXCursor recCursor, HashSet<CursorKey, MetaArena, CursorKeyHash>& activeRecursion){
    if(::clang_Cursor_isNull(recCursor))
        return false;
    const CXCursor def = ::clang_getCursorDefinition(recCursor);
    const CXCursor inspectCursor = ::clang_Cursor_isNull(def) ? recCursor : def;
    if(::clang_isCursorDefinition(inspectCursor) == 0u)
        return true;

    const CursorKey defKey = MakeCursorKey(inspectCursor);
    if(!activeRecursion.insert(defKey).second)
        return false;

    MVector<CXCursor> children(arena);
    VisitDirectChildren(inspectCursor, children);
    b8 foundPtr = false;
    for(const CXCursor ch : children){
        const CXCursorKind ck = ::clang_getCursorKind(ch);
        if(ck == CXCursor_CXXBaseSpecifier){
            const CXCursor baseDecl = ResolveBaseSpecifierDecl(arena, ch);
            if(!::clang_Cursor_isNull(baseDecl) && HasPointerOrReferenceFieldRecursive(arena, baseDecl, activeRecursion)){
                foundPtr = true;
                break;
            }
            continue;
        }
        if(ck == CXCursor_FieldDecl){
            CXType ftype = ::clang_getCanonicalType(::clang_getCursorType(ch));
            while(ftype.kind == CXType_ConstantArray || ftype.kind == CXType_IncompleteArray)
                ftype = ::clang_getCanonicalType(::clang_getArrayElementType(ftype));
            if(ftype.kind == CXType_Pointer
                || ftype.kind == CXType_LValueReference
                || ftype.kind == CXType_RValueReference
                || ftype.kind == CXType_MemberPointer
            ){
                foundPtr = true;
                break;
            }
            if(ftype.kind == CXType_Record){
                const CXCursor fieldRec = ::clang_getTypeDeclaration(ftype);
                if(HasPointerOrReferenceFieldRecursive(arena, fieldRec, activeRecursion)){
                    foundPtr = true;
                    break;
                }
            }
        }
    }

    activeRecursion.erase(defKey);
    return foundPtr;
}

[[nodiscard]] u8 DeduceRecordArenaCtorMode(MetaArena& arena, const CXCursor recCursor, const b8 isArenaTemplated){
    if(::clang_Cursor_isNull(recCursor))
        return isArenaTemplated ? ArenaCtorMode::Omit : ArenaCtorMode::Default;

    CXCursor inspectCursor = ::clang_getCursorDefinition(recCursor);
    if(::clang_Cursor_isNull(inspectCursor))
        inspectCursor = recCursor;

    b8 hasArenaFirst = false;
    b8 hasArenaLast = false;

    const auto scanCursorCtorArena = [&](const CXCursor targetCursor) -> b8{
        MVector<CXCursor> children(arena);
        VisitDirectChildren(targetCursor, children);
        for(const CXCursor ch : children){
            const CXCursorKind ck = ::clang_getCursorKind(ch);
            if(ck == CXCursor_TypeAliasDecl || ck == CXCursor_TypedefDecl){
                const MString aliasName = ConsumeClangString(arena, ::clang_getCursorSpelling(ch));
                if(aliasName == "uses_leading_arena_t"){
                    if(isArenaTemplated || IsClangTypeExactArenaBase(arena, ::clang_getTypedefDeclUnderlyingType(ch)))
                        return true;
                }
                continue;
            }
            const b8 isCtor = (ck == CXCursor_Constructor) || (ck == CXCursor_FunctionTemplate && ::clang_getTemplateCursorKind(ch) == CXCursor_Constructor);
            if(!isCtor || ::clang_getCXXAccessSpecifier(ch) != CX_CXXPublic || ::clang_CXXMethod_isDeleted(ch) != 0u)
                continue;
            if(::clang_CXXConstructor_isCopyConstructor(ch) || ::clang_CXXConstructor_isMoveConstructor(ch))
                continue;

            const CppCallableCandidate ctorCand = BuildCandidateFromCursor(arena, ch);
            if(ctorCand.params.empty())
                continue;
            const auto isArenaParam = [isArenaTemplated](const CppParamInfo& p)noexcept{
                return p.isExactArenaBaseRef || (isArenaTemplated && p.isTemplateArenaRef);
            };
            if(isArenaParam(ctorCand.params.front()))
                hasArenaFirst = true;
            else if(isArenaParam(ctorCand.params.back()))
                hasArenaLast = true;
        }
        return false;
    };

    if(scanCursorCtorArena(inspectCursor))
        return ArenaCtorMode::First;
    if(isArenaTemplated && !hasArenaFirst && !hasArenaLast){
        const CXCursor specializedTpl = ::clang_getSpecializedCursorTemplate(inspectCursor);
        if(!::clang_Cursor_isNull(specializedTpl) && scanCursorCtorArena(specializedTpl))
            return ArenaCtorMode::First;
    }

    if(hasArenaFirst)
        return ArenaCtorMode::First;
    if(isArenaTemplated && !hasArenaLast)
        return ArenaCtorMode::Omit;
    return ArenaCtorMode::Default;
}

struct PureVirtualSlot{
    CursorKey canonicalKey;
    MString name;
    b8 isDestructor = false;
    b8 isConst = false;
    u32 argCount = 0u;
    b8 hasDependentParam = false;
    MVector<MString> paramCanonicalTypes;

    explicit PureVirtualSlot(MetaArena& arena)noexcept
        : name(arena)
        , paramCanonicalTypes(arena)
    {}
};

void CollectMethodSignatureParams(
    MetaArena& arena,
    const CXCursor cursor,
    MString& outName,
    b8& outIsDestructor,
    b8& outIsConst,
    u32& outArgCount,
    b8& outHasDependentParam,
    MVector<MString>& outParamCanonicalTypes
){
    const CXCursorKind ck = ::clang_getCursorKind(cursor);
    if(ck == CXCursor_Destructor){
        outName.clear();
        outIsDestructor = true;
        outIsConst = false;
        outArgCount = 0u;
        outHasDependentParam = false;
        outParamCanonicalTypes.clear();
        return;
    }

    outName = ConsumeClangString(arena, ::clang_getCursorSpelling(cursor));
    outIsDestructor = false;
    outIsConst = (::clang_CXXMethod_isConst(cursor) != 0u);
    outHasDependentParam = false;
    outParamCanonicalTypes.clear();

    MVector<CXCursor> paramCursors(arena);
    CollectCallableParamCursors(arena, cursor, false, paramCursors);

    outArgCount = static_cast<u32>(paramCursors.size());
    outParamCanonicalTypes.reserve(paramCursors.size());
    for(const CXCursor parm : paramCursors){
        const CXType canonType = ::clang_getCanonicalType(::clang_getCursorType(parm));
        MString typeStr = ConsumeClangString(arena, ::clang_getTypeSpelling(canonType));
        if(canonType.kind == CXType_Invalid
            || canonType.kind == CXType_Unexposed
            || canonType.kind == CXType_Dependent
            || typeStr.empty()
        ){
            outHasDependentParam = true;
        }
        outParamCanonicalTypes.push_back(Move(typeStr));
    }
}

[[nodiscard]] PureVirtualSlot BuildPureVirtualSlot(MetaArena& arena, const CXCursor cursor){
    PureVirtualSlot slot(arena);
    const CXCursor canon = ::clang_getCanonicalCursor(cursor);
    slot.canonicalKey = MakeCursorKey(::clang_Cursor_isNull(canon) ? cursor : canon);
    CollectMethodSignatureParams(
        arena,
        cursor,
        slot.name,
        slot.isDestructor,
        slot.isConst,
        slot.argCount,
        slot.hasDependentParam,
        slot.paramCanonicalTypes
    );
    return slot;
}

void GatherOverriddenCursorKeys(const CXCursor cursor, MVector<CursorKey>& outKeys){
    outKeys.clear();
    CXCursor* overridden = nullptr;
    unsigned numOverridden = 0u;
    ::clang_getOverriddenCursors(cursor, &overridden, &numOverridden);
    if(overridden){
        outKeys.reserve(static_cast<usize>(numOverridden));
        for(unsigned i = 0u; i < numOverridden; ++i){
            const CXCursor canon = ::clang_getCanonicalCursor(overridden[i]);
            outKeys.push_back(MakeCursorKey(::clang_Cursor_isNull(canon) ? overridden[i] : canon));
        }
        ::clang_disposeOverriddenCursors(overridden);
    }
}

[[nodiscard]] b8 DoesMethodOverridePureVirtual(
    MetaArena& arena,
    const CXCursor cursor,
    const MVector<CursorKey>& overriddenKeys,
    const PureVirtualSlot& slot
){
    for(const CursorKey& key : overriddenKeys){
        if(key == slot.canonicalKey)
            return true;
    }

    if(::clang_getCursorKind(cursor) == CXCursor_Destructor)
        return slot.isDestructor;

    if(slot.isDestructor)
        return false;

    MString methodName(arena);
    b8 methodIsDestructor = false;
    b8 methodIsConst = false;
    u32 methodArgCount = 0u;
    b8 methodHasDependentParam = false;
    MVector<MString> methodParamTypes(arena);
    CollectMethodSignatureParams(
        arena,
        cursor,
        methodName,
        methodIsDestructor,
        methodIsConst,
        methodArgCount,
        methodHasDependentParam,
        methodParamTypes
    );

    if(methodIsDestructor || methodName != slot.name || methodIsConst != slot.isConst || methodArgCount != slot.argCount)
        return false;

    if(slot.hasDependentParam || methodHasDependentParam)
        return true;

    if(methodParamTypes.size() != slot.paramCanonicalTypes.size())
        return false;

    for(usize i = 0u; i < methodParamTypes.size(); ++i){
        if(methodParamTypes[i] != slot.paramCanonicalTypes[i])
            return false;
    }
    return true;
}

void AppendUniquePureVirtualSlot(MVector<PureVirtualSlot>& dst, PureVirtualSlot&& slot){
    for(const PureVirtualSlot& existing : dst){
        if(existing.canonicalKey == slot.canonicalKey)
            return;
    }
    dst.push_back(Move(slot));
}

template<typename Pred>
b8 EraseMatchingPureVirtualSlots(MVector<PureVirtualSlot>& slots, Pred&& pred){
    b8 anyErased = false;
    for(usize i = slots.size(); i > 0u; --i){
        if(pred(slots[i - 1u])){
            slots.erase(slots.begin() + static_cast<isize>(i - 1u));
            anyErased = true;
        }
    }
    return anyErased;
}

void InspectClassTemplateRecursive(
    MetaArena& arena,
    const CXCursor classCursor,
    const b8 isBasePass,
    HashSet<CursorKey, MetaArena, CursorKeyHash>& activeRecursion,
    TemplateClassTraits& traits,
    MVector<PureVirtualSlot>& outPureVirtuals
){
    if(::clang_Cursor_isNull(classCursor))
        return;

    CXCursor def = UnwrapToRecordOrClassTemplate(arena, classCursor);
    if(::clang_Cursor_isNull(def))
        def = classCursor;

    const CursorKey defKey = MakeCursorKey(def);
    if(!activeRecursion.insert(defKey).second)
        return;

    MVector<CXCursor> children(arena);
    VisitDirectChildren(def, children);

    MVector<PureVirtualSlot> inheritedPureVirtuals(arena);
    MVector<PureVirtualSlot> ownPureVirtuals(arena);

    for(const CXCursor child : children){
        if(::clang_getCursorKind(child) != CXCursor_CXXBaseSpecifier)
            continue;

        const CXCursor baseDecl = ResolveBaseSpecifierDecl(arena, child);
        if(::clang_Cursor_isNull(baseDecl) || ::clang_getCursorKind(baseDecl) == CXCursor_NoDeclFound)
            continue;

        if(::clang_isCursorDefinition(baseDecl) == 0u){
            const MString baseNameStr = ConsumeClangString(arena, ::clang_getCursorSpelling(baseDecl));
            if(ApplyFallbackBaseCopyMoveTraits(MStringView(baseNameStr.data(), baseNameStr.size()), traits))
                continue;
        }

        MVector<PureVirtualSlot> basePureVirtuals(arena);
        InspectClassTemplateRecursive(arena, baseDecl, true, activeRecursion, traits, basePureVirtuals);
        for(PureVirtualSlot& slot : basePureVirtuals)
            AppendUniquePureVirtualSlot(inheritedPureVirtuals, Move(slot));
    }

    MVector<CursorKey> overriddenKeys(arena);
    for(const CXCursor child : children){
        const CXCursorKind ck = ::clang_getCursorKind(child);
        const CX_CXXAccessSpecifier acc = ::clang_getCXXAccessSpecifier(child);

        if(ck == CXCursor_CXXBaseSpecifier)
            continue;

        if(ck == CXCursor_CXXFinalAttr){
            if(!isBasePass)
                traits.isFinal = true;
            continue;
        }

        if(ck == CXCursor_Constructor){
            if(!isBasePass)
                traits.hasAnyUserCtor = true;
            const b8 accessible = (isBasePass ? (acc != CX_CXXPrivate) : (acc == CX_CXXPublic))
                && (::clang_CXXMethod_isDeleted(child) == 0u);
            if(::clang_CXXConstructor_isCopyConstructor(child)){
                if(!isBasePass)
                    traits.hasUserDeclaredCopyCtor = true;
                if(!accessible)
                    traits.isCopyConstructible = false;
                continue;
            }
            if(::clang_CXXConstructor_isMoveConstructor(child)){
                if(!isBasePass)
                    traits.hasUserDeclaredMoveCtor = true;
                if(!accessible)
                    traits.isMoveConstructible = false;
                continue;
            }
            if(!isBasePass && accessible){
                const CppCallableCandidate ctorCand = BuildCandidateFromCursor(arena, child);
                if(ctorCand.minArgs == 0u){
                    traits.hasAccessibleZeroArgCtor = true;
                    traits.zeroArgCtorNothrow = ctorCand.isNothrow;
                }
                if(ctorCand.minArgs <= 1u
                    && 1u <= ctorCand.maxArgs
                    && !ctorCand.params.empty()
                    && (ctorCand.params[0].isExactArenaBaseRef || ctorCand.params[0].isTemplateArenaRef)
                ){
                    traits.hasAccessibleArenaArgCtor = true;
                    traits.arenaArgCtorNothrow = ctorCand.isNothrow;
                }
            }
            continue;
        }

        if(ck == CXCursor_FieldDecl){
            traits.hasNonStaticField = true;
            if(!isBasePass){
                const CXType ftype = ::clang_getCursorType(child);
                if(ftype.kind == CXType_LValueReference || ftype.kind == CXType_RValueReference){
                    if(!HasDefaultInitializerExpr(child))
                        traits.hasReferenceOrNoDefaultField = true;
                }
            }
            continue;
        }

        if(ck == CXCursor_FriendDecl){
            MVector<CXCursor> friendChildren(arena);
            VisitDirectChildren(child, friendChildren);
            for(const CXCursor fc : friendChildren){
                const CXCursorKind fck = ::clang_getCursorKind(fc);
                if(fck == CXCursor_FunctionDecl || (fck == CXCursor_FunctionTemplate && ::clang_getTemplateCursorKind(fc) == CXCursor_FunctionDecl)){
                    const MString fname = ConsumeClangString(arena, ::clang_getCursorSpelling(fc));
                    if(fname == "operator==" && ::clang_CXXMethod_isDeleted(fc) == 0u)
                        traits.isEqualitySwitch = true;
                }
            }
            continue;
        }

        if(ck == CXCursor_CXXMethod){
            if(::clang_CXXMethod_isVirtual(child))
                traits.hasVirtualMethod = true;

            if(::clang_CXXMethod_isPureVirtual(child)){
                traits.hasVirtualMethod = true;
                GatherOverriddenCursorKeys(child, overriddenKeys);
                const CXCursor canon = ::clang_getCanonicalCursor(child);
                const CursorKey ownKey = MakeCursorKey(::clang_Cursor_isNull(canon) ? child : canon);
                EraseMatchingPureVirtualSlots(inheritedPureVirtuals, [&](const PureVirtualSlot& slot){
                    if(slot.canonicalKey == ownKey)
                        return true;
                    for(const CursorKey& okKey : overriddenKeys){
                        if(okKey == slot.canonicalKey)
                            return true;
                    }
                    return false;
                });
                AppendUniquePureVirtualSlot(ownPureVirtuals, BuildPureVirtualSlot(arena, child));
            }
            else{
                GatherOverriddenCursorKeys(child, overriddenKeys);
                if(!overriddenKeys.empty())
                    traits.hasVirtualMethod = true;
                if(EraseMatchingPureVirtualSlots(inheritedPureVirtuals, [&](const PureVirtualSlot& slot){
                    return DoesMethodOverridePureVirtual(arena, child, overriddenKeys, slot);
                })){
                    traits.hasVirtualMethod = true;
                }
            }

            const b8 accessible = (isBasePass ? (acc != CX_CXXPrivate) : (acc == CX_CXXPublic))
                && (::clang_CXXMethod_isDeleted(child) == 0u);
            if(accessible){
                const MString mname = ConsumeClangString(arena, ::clang_getCursorSpelling(child));
                if(mname == "operator==")
                    traits.isEqualitySwitch = true;
            }
            if(::clang_CXXMethod_isCopyAssignmentOperator(child)){
                if(!isBasePass)
                    traits.hasUserDeclaredCopyAssign = true;
                if(!accessible)
                    traits.isCopyAssignable = false;
            }
            if(::clang_CXXMethod_isMoveAssignmentOperator(child)){
                if(!isBasePass)
                    traits.hasUserDeclaredMoveAssign = true;
                if(!accessible)
                    traits.isMoveAssignable = false;
            }
            continue;
        }

        if(ck == CXCursor_Destructor){
            if(::clang_CXXMethod_isVirtual(child))
                traits.hasVirtualMethod = true;
            EraseMatchingPureVirtualSlots(inheritedPureVirtuals, [](const PureVirtualSlot& slot)noexcept{
                return slot.isDestructor;
            });
            if(::clang_CXXMethod_isPureVirtual(child)){
                traits.hasVirtualMethod = true;
                AppendUniquePureVirtualSlot(ownPureVirtuals, BuildPureVirtualSlot(arena, child));
            }
        }
    }

    for(PureVirtualSlot& slot : inheritedPureVirtuals)
        AppendUniquePureVirtualSlot(outPureVirtuals, Move(slot));
    for(PureVirtualSlot& slot : ownPureVirtuals)
        AppendUniquePureVirtualSlot(outPureVirtuals, Move(slot));

    activeRecursion.erase(defKey);
}

void DeduceClassTemplateTraits(MetaArena& arena, const CXCursor templateCursor, CachedClassOutcome& outOutcome){
    TemplateClassTraits traits;
    HashSet<CursorKey, MetaArena, CursorKeyHash> activeRecursion(arena);
    MVector<PureVirtualSlot> remainingPureVirtuals(arena);
    InspectClassTemplateRecursive(arena, templateCursor, false, activeRecursion, traits, remainingPureVirtuals);
    traits.isAbstract = !remainingPureVirtuals.empty();

    FinalizeRecordSpecialMemberTraits(traits);

    outOutcome.resolved = true;
    outOutcome.arenaCtorMode = DeduceRecordArenaCtorMode(arena, templateCursor, false);
    const b8 usesArenaCtor = (outOutcome.arenaCtorMode == ArenaCtorMode::First);
    outOutcome.isAbstract = traits.isAbstract;
    outOutcome.isPolymorphicBase = traits.hasVirtualMethod && !traits.isFinal;
    outOutcome.isEmptyBase = !traits.isAbstract && !outOutcome.isPolymorphicBase && !traits.isFinal && !traits.hasNonStaticField;
    outOutcome.isCopyConstructible = !traits.isAbstract && traits.isCopyConstructible;
    outOutcome.isCopyAssignable = !traits.isAbstract && traits.isCopyAssignable;
    outOutcome.isMoveConstructible = !traits.isAbstract && traits.isMoveConstructible;
    outOutcome.isMoveAssignable = !traits.isAbstract && traits.isMoveAssignable;
    outOutcome.isEqualitySwitch = traits.isEqualitySwitch;

    const b8 zeroDefault = !traits.isAbstract && (traits.hasAccessibleZeroArgCtor || (!traits.hasAnyUserCtor && !traits.hasReferenceOrNoDefaultField));
    const b8 zeroDefaultNothrow = traits.hasAccessibleZeroArgCtor ? traits.zeroArgCtorNothrow : true;
    outOutcome.emitsBraceFieldInit = zeroDefault && !usesArenaCtor;

    if(!traits.isAbstract){
        if(usesArenaCtor && traits.hasAccessibleArenaArgCtor){
            outOutcome.isSelfDefaulted = true;
            outOutcome.isNothrowDefaultConstructible = traits.arenaArgCtorNothrow;
        }
        else if(zeroDefault){
            outOutcome.isSelfDefaulted = true;
            outOutcome.isNothrowDefaultConstructible = zeroDefaultNothrow;
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] ::Path<MetaArena> ResolveExternTraitCachePath(MetaArena& arena, const u64 keyHash){
    const MString fileName = StringFormat(arena, "nwb_adhd_exttraits_{:016x}_v{}.bin", keyHash, s_ExternTraitCacheVersion);
    return CacheDetail::ResolveTempCachePath(arena, MStringView(fileName.data(), fileName.size()));
}

[[nodiscard]] u64 ComputeSchemaSignature()noexcept{
    u64 sig = s_Fnv64OffsetBasis;
    Fnv64AppendValue(sig, s_ExternTraitCacheMagic);
    Fnv64AppendValue(sig, s_ExternTraitCacheVersion);
    Fnv64AppendValue(sig, sizeof(CachedFileStamp));
    Fnv64AppendValue(sig, sizeof(usize));
#if defined(NWB_PROJECT_SOURCE_DIR)
    sig = UpdateFnv64TextExact(sig, MStringView(NWB_PROJECT_SOURCE_DIR));
#endif
    return sig;
}

void AppendLengthPrefixed(MString& key, const MStringView text){
    Array<char, 24> lenBuf = {};
    const AStringView digits = FormatDecimal(text.size(), lenBuf);
    key.append(digits.data(), digits.size());
    key.push_back(':');
    key.append(text.data(), text.size());
    key.push_back('|');
}

void AppendMethodQueryKey(MString& key, const MethodDecl& method){
    AppendLengthPrefixed(key, method.name);
    key.push_back(method.isConstructor ? '1' : '0');
    key.push_back(method.isOperator ? '1' : '0');
    key.push_back(method.isConst ? '1' : '0');
    key.push_back(method.isStatic ? '1' : '0');
    key.push_back(method.isVariadic ? '1' : '0');
    AppendUnsignedDecimal(key, method.params.size());
    key.push_back('[');
    for(const ParamDecl& p : method.params){
        key.push_back(p.mode == ParamPassingMode::In ? 'I' : (p.mode == ParamPassingMode::Mut ? 'M' : 'N'));
        key.push_back(':');
        if(p.type)
            AppendLengthPrefixed(key, p.type->isAuto ? MStringView("*") : p.type->name);
        else
            AppendLengthPrefixed(key, {});
    }
    key.push_back(']');
}

[[nodiscard]] MString BuildTraitCacheKey(
    MetaArena& arena,
    const Span<const MString> argStorage,
    const Span<const MString> uniqueHeaders,
    const Span<ClassDecl* const> classes,
    const Span<InterfaceDecl* const> ifaces,
    const Span<MethodDecl* const> freeFunctions
){
    MString key(arena);
    key.reserve(512u + argStorage.size() * 32u + uniqueHeaders.size() * 48u);
    AppendUnsignedDecimal(key, s_ExternTraitCacheVersion);
    key.push_back('#');
    for(const MString& arg : argStorage)
        AppendLengthPrefixed(key, MStringView(arg.data(), arg.size()));
    key.push_back('#');
    for(const MString& hdr : uniqueHeaders)
        AppendLengthPrefixed(key, MStringView(hdr.data(), hdr.size()));
    key.push_back('#');
    for(const ClassDecl* cls : classes){
        AppendLengthPrefixed(key, !cls->cppExportName.empty() ? cls->cppExportName : cls->name);
        AppendUnsignedDecimal(key, cls->templateParams.size());
        key.push_back(':');
        AppendUnsignedDecimal(key, cls->methods.size());
        key.push_back('{');
        for(const MethodDecl* m : cls->methods){
            if(m)
                AppendMethodQueryKey(key, *m);
        }
        key.push_back('}');
    }
    key.push_back('#');
    for(const InterfaceDecl* iface : ifaces){
        AppendLengthPrefixed(key, !iface->cppExportName.empty() ? iface->cppExportName : iface->name);
        AppendUnsignedDecimal(key, iface->templateParams.size());
        key.push_back(':');
        AppendUnsignedDecimal(key, iface->methods.size());
        key.push_back('{');
        for(const MethodDecl* m : iface->methods){
            if(m)
                AppendMethodQueryKey(key, *m);
        }
        key.push_back('}');
    }
    key.push_back('#');
    for(const MethodDecl* fn : freeFunctions)
        AppendMethodQueryKey(key, *fn);
    return key;
}

[[nodiscard]] b8 TryLoadDiskCache(
    const u64 keyHash,
    const MStringView expectedKey,
    const usize expectedClassCount,
    const usize expectedIfaceCount,
    const usize expectedFreeFnCount,
    MetaArena& scratchArena,
    CachedTraitResolution& outCached
){
    const ::Path<MetaArena> cachePath = ResolveExternTraitCachePath(scratchArena, keyHash);
    Vector<u8, MetaArena> bytes(scratchArena);
    if(!ReadBinaryFile(cachePath, bytes) || bytes.size() < 24u)
        return false;

    CacheDetail::BinaryBufferReader reader(bytes.data(), bytes.size());
    u32 magic = 0u;
    u32 version = 0u;
    u64 schemaSig = 0u;
    u64 storedKeyHash = 0u;
    if(!reader.readValue(magic) || !reader.readValue(version) || !reader.readValue(schemaSig) || !reader.readValue(storedKeyHash))
        return false;
    if(magic != s_ExternTraitCacheMagic || version != s_ExternTraitCacheVersion || schemaSig != ComputeSchemaSignature() || storedKeyHash != keyHash)
        return false;

    if(!reader.readString(outCached.cacheKey))
        return false;
    if(MStringView(outCached.cacheKey.data(), outCached.cacheKey.size()) != expectedKey)
        return false;

    u32 watchedCount = 0u;
    if(!reader.readValue(watchedCount) || watchedCount > 16384u)
        return false;
    outCached.watchedStamps.reserve(watchedCount);
    for(u32 wi = 0u; wi < watchedCount; ++wi){
        CachedFileStamp stamp(outCached.arena);
        if(!reader.readString(stamp.path)
            || !reader.readValue(stamp.fileSize)
            || !reader.readValue(stamp.mtimeLow)
            || !reader.readValue(stamp.mtimeHigh)
        )
            return false;
        outCached.watchedStamps.push_back(Move(stamp));
    }
    if(!VerifyCachedFileStamps(scratchArena, Span<const CachedFileStamp>(outCached.watchedStamps.data(), outCached.watchedStamps.size())))
        return false;

    u32 incCount = 0u;
    if(!reader.readValue(incCount) || incCount > 16384u)
        return false;
    outCached.projectIncludedFiles.reserve(incCount);
    for(u32 ii = 0u; ii < incCount; ++ii){
        MString inc(outCached.arena);
        if(!reader.readString(inc))
            return false;
        outCached.projectIncludedFiles.push_back(Move(inc));
    }

    const auto readMethodList = [&](const u32 count, MVector<CachedMethodOutcome>& dst) -> b8{
        dst.reserve(count);
        for(u32 mi = 0u; mi < count; ++mi){
            CachedMethodOutcome mo(outCached.arena);
            u32 paramCount = 0u;
            if(!reader.readBool(mo.allowsCppUnwind)
                || !reader.readValue(mo.byteViewArgMask)
                || !reader.readValue(mo.platformLiteralArgMask)
                || !reader.readValue(paramCount)
                || paramCount > 1024u
            )
                return false;
            mo.paramIsConstRef.reserve(paramCount);
            for(u32 pi = 0u; pi < paramCount; ++pi){
                u8 flag = 0u;
                if(!reader.readValue(flag))
                    return false;
                mo.paramIsConstRef.push_back(flag);
            }
            dst.push_back(Move(mo));
        }
        return true;
    };

    u32 classCount = 0u;
    if(!reader.readValue(classCount) || classCount != expectedClassCount)
        return false;
    outCached.classOutcomes.reserve(classCount);
    for(u32 ci = 0u; ci < classCount; ++ci){
        CachedClassOutcome co(outCached.arena);
        u32 methodCount = 0u;
        if(!reader.readBool(co.resolved)
            || !reader.readBool(co.isArenaOwner)
            || !reader.readValue(co.arenaCtorMode)
            || !reader.readBool(co.isAbstract)
            || !reader.readBool(co.isPolymorphicBase)
            || !reader.readBool(co.isEmptyBase)
            || !reader.readBool(co.isCopyConstructible)
            || !reader.readBool(co.isMoveConstructible)
            || !reader.readBool(co.isCopyAssignable)
            || !reader.readBool(co.isMoveAssignable)
            || !reader.readBool(co.isSelfDefaulted)
            || !reader.readBool(co.isNothrowDefaultConstructible)
            || !reader.readBool(co.isSmallValue)
            || !reader.readBool(co.isBinarySerializable)
            || !reader.readBool(co.emitsBraceFieldInit)
            || !reader.readBool(co.isEqualitySwitch)
            || !reader.readValue(methodCount)
            || methodCount > 4096u
        )
            return false;
        if(!readMethodList(methodCount, co.methods))
            return false;
        outCached.classOutcomes.push_back(Move(co));
    }

    u32 ifaceCount = 0u;
    if(!reader.readValue(ifaceCount) || ifaceCount != expectedIfaceCount)
        return false;
    outCached.ifaceOutcomes.reserve(ifaceCount);
    for(u32 ii = 0u; ii < ifaceCount; ++ii){
        CachedInterfaceOutcome io(outCached.arena);
        u32 methodCount = 0u;
        if(!reader.readValue(methodCount) || methodCount > 4096u)
            return false;
        if(!readMethodList(methodCount, io.methods))
            return false;
        outCached.ifaceOutcomes.push_back(Move(io));
    }

    u32 fnCount = 0u;
    if(!reader.readValue(fnCount) || fnCount != expectedFreeFnCount)
        return false;
    outCached.freeFnOutcomes.reserve(fnCount);
    for(u32 fi = 0u; fi < fnCount; ++fi){
        CachedFreeFunctionOutcome fo{};
        if(!reader.readBool(fo.allowsCppUnwind)
            || !reader.readBool(fo.injectsCallerArena)
            || !reader.readValue(fo.byteViewArgMask)
            || !reader.readValue(fo.platformLiteralArgMask)
        )
            return false;
        outCached.freeFnOutcomes.push_back(fo);
    }

    return reader.atEnd();
}

void SaveDiskCache(const u64 keyHash, const CachedTraitResolution& cached, MetaArena& scratchArena){
    CacheDetail::BinaryBufferWriter writer(scratchArena);
    writer.writeValue(s_ExternTraitCacheMagic);
    writer.writeValue(s_ExternTraitCacheVersion);
    writer.writeValue(ComputeSchemaSignature());
    writer.writeValue(keyHash);
    writer.writeRawString(MStringView(cached.cacheKey.data(), cached.cacheKey.size()));

    writer.writeValue(static_cast<u32>(cached.watchedStamps.size()));
    for(const CachedFileStamp& stamp : cached.watchedStamps){
        writer.writeRawString(MStringView(stamp.path.data(), stamp.path.size()));
        writer.writeValue(stamp.fileSize);
        writer.writeValue(stamp.mtimeLow);
        writer.writeValue(stamp.mtimeHigh);
    }

    writer.writeValue(static_cast<u32>(cached.projectIncludedFiles.size()));
    for(const MString& inc : cached.projectIncludedFiles)
        writer.writeRawString(MStringView(inc.data(), inc.size()));

    const auto writeMethodList = [&](const Span<const CachedMethodOutcome> list){
        writer.writeValue(static_cast<u32>(list.size()));
        for(const CachedMethodOutcome& mo : list){
            writer.writeBool(mo.allowsCppUnwind);
            writer.writeValue(mo.byteViewArgMask);
            writer.writeValue(mo.platformLiteralArgMask);
            writer.writeValue(static_cast<u32>(mo.paramIsConstRef.size()));
            for(const u8 flag : mo.paramIsConstRef)
                writer.writeValue(flag);
        }
    };

    writer.writeValue(static_cast<u32>(cached.classOutcomes.size()));
    for(const CachedClassOutcome& co : cached.classOutcomes){
        writer.writeBool(co.resolved);
        writer.writeBool(co.isArenaOwner);
        writer.writeValue(co.arenaCtorMode);
        writer.writeBool(co.isAbstract);
        writer.writeBool(co.isPolymorphicBase);
        writer.writeBool(co.isEmptyBase);
        writer.writeBool(co.isCopyConstructible);
        writer.writeBool(co.isMoveConstructible);
        writer.writeBool(co.isCopyAssignable);
        writer.writeBool(co.isMoveAssignable);
        writer.writeBool(co.isSelfDefaulted);
        writer.writeBool(co.isNothrowDefaultConstructible);
        writer.writeBool(co.isSmallValue);
        writer.writeBool(co.isBinarySerializable);
        writer.writeBool(co.emitsBraceFieldInit);
        writer.writeBool(co.isEqualitySwitch);
        writeMethodList(Span<const CachedMethodOutcome>(co.methods.data(), co.methods.size()));
    }

    writer.writeValue(static_cast<u32>(cached.ifaceOutcomes.size()));
    for(const CachedInterfaceOutcome& io : cached.ifaceOutcomes)
        writeMethodList(Span<const CachedMethodOutcome>(io.methods.data(), io.methods.size()));

    writer.writeValue(static_cast<u32>(cached.freeFnOutcomes.size()));
    for(const CachedFreeFunctionOutcome& fo : cached.freeFnOutcomes){
        writer.writeBool(fo.allowsCppUnwind);
        writer.writeBool(fo.injectsCallerArena);
        writer.writeValue(fo.byteViewArgMask);
        writer.writeValue(fo.platformLiteralArgMask);
    }

    const ::Path<MetaArena> cachePath = ResolveExternTraitCachePath(scratchArena, keyHash);
    CacheDetail::WriteBinaryFileAtomic(scratchArena, cachePath, writer.bytes());
}

void ApplyMethodOutcomes(const Span<MethodDecl* const> methods, const Span<const CachedMethodOutcome> outcomes){
    usize moIdx = 0u;
    for(MethodDecl* m : methods){
        if(!m || !m->isExtern)
            continue;
        if(moIdx >= outcomes.size())
            break;
        const CachedMethodOutcome& mo = outcomes[moIdx++];
        m->allowsCppUnwind = mo.allowsCppUnwind;
        m->byteViewArgMask = mo.byteViewArgMask;
        m->platformLiteralArgMask = mo.platformLiteralArgMask;
        for(usize pi = 0u; pi < m->params.size() && pi < mo.paramIsConstRef.size(); ++pi){
            if(mo.paramIsConstRef[pi] != 0u)
                m->params[pi].cppConstReference = true;
        }
    }
}

void InitMethodOutcomes(MetaArena& arena, const Span<MethodDecl* const> methods, MVector<CachedMethodOutcome>& outOutcomes){
    for(const MethodDecl* m : methods){
        if(!m || !m->isExtern)
            continue;
        CachedMethodOutcome mo(arena);
        mo.allowsCppUnwind = true;
        mo.paramIsConstRef.assign(m->params.size(), 0u);
        outOutcomes.push_back(Move(mo));
    }
}

void ResolveRecordMethodOutcomes(
    MetaArena& arena,
    const CXCursor recCursor,
    const Span<MethodDecl* const> adhdMethods,
    MVector<CachedMethodOutcome>& outOutcomes
){
    if(::clang_Cursor_isNull(recCursor) || outOutcomes.empty())
        return;

    HashSet<CursorKey, MetaArena, CursorKeyHash> visitedBases(arena);
    HashSet<MethodHideKey, MetaArena, MethodHideKeyHash> derivedMethods(arena);
    MVector<CppCallableCandidate> methods(arena);
    MVector<CppCallableCandidate> constructors(arena);
    CollectRecordCallableHierarchy(arena, recCursor, false, visitedBases, derivedMethods, methods, constructors);

    usize moIdx = 0u;
    for(const MethodDecl* m : adhdMethods){
        if(!m || !m->isExtern)
            continue;
        if(moIdx >= outOutcomes.size())
            break;
        ApplyMethodTraitMatches(methods, constructors, *m, outOutcomes[moIdx++], arena);
    }
}

void ApplyOutcomeToTranslationUnit(
    const CachedTraitResolution& outcome,
    const Span<ClassDecl* const> classes,
    const Span<InterfaceDecl* const> ifaces,
    const Span<MethodDecl* const> freeFunctions,
    MVector<MString>* const outProjectIncludedFiles,
    MetaArena& dstArena
){
    for(usize ci = 0u; ci < classes.size() && ci < outcome.classOutcomes.size(); ++ci){
        ClassDecl* cls = classes[ci];
        const CachedClassOutcome& co = outcome.classOutcomes[ci];
        if(co.resolved){
            cls->isArenaOwner = co.isArenaOwner;
            cls->arenaCtorMode = co.arenaCtorMode;
            cls->isAbstract = co.isAbstract;
            cls->isPolymorphicBase = co.isPolymorphicBase;
            cls->isEmptyBase = co.isEmptyBase;
            cls->isCopyConstructible = co.isCopyConstructible;
            cls->isMoveConstructible = co.isMoveConstructible;
            cls->isCopyAssignable = co.isCopyAssignable;
            cls->isMoveAssignable = co.isMoveAssignable;
            cls->isSelfDefaulted = co.isSelfDefaulted;
            cls->isNothrowDefaultConstructible = co.isNothrowDefaultConstructible;
            cls->isSmallValue = co.isSmallValue;
            cls->isBinarySerializable = co.isBinarySerializable;
            cls->emitsBraceFieldInit = co.emitsBraceFieldInit;
            cls->isEqualitySwitch = co.isEqualitySwitch;
        }
        ApplyMethodOutcomes(
            Span<MethodDecl* const>(cls->methods.data(), cls->methods.size()),
            Span<const CachedMethodOutcome>(co.methods.data(), co.methods.size())
        );
    }

    for(usize ii = 0u; ii < ifaces.size() && ii < outcome.ifaceOutcomes.size(); ++ii){
        InterfaceDecl* iface = ifaces[ii];
        const CachedInterfaceOutcome& io = outcome.ifaceOutcomes[ii];
        ApplyMethodOutcomes(
            Span<MethodDecl* const>(iface->methods.data(), iface->methods.size()),
            Span<const CachedMethodOutcome>(io.methods.data(), io.methods.size())
        );
    }

    for(usize fi = 0u; fi < freeFunctions.size() && fi < outcome.freeFnOutcomes.size(); ++fi){
        const CachedFreeFunctionOutcome& fo = outcome.freeFnOutcomes[fi];
        freeFunctions[fi]->allowsCppUnwind = fo.allowsCppUnwind;
        freeFunctions[fi]->injectsCallerArena = fo.injectsCallerArena;
        freeFunctions[fi]->byteViewArgMask = fo.byteViewArgMask;
        freeFunctions[fi]->platformLiteralArgMask = fo.platformLiteralArgMask;
    }

    if(outProjectIncludedFiles){
        outProjectIncludedFiles->reserve(outProjectIncludedFiles->size() + outcome.projectIncludedFiles.size());
        for(const MString& file : outcome.projectIncludedFiles)
            outProjectIncludedFiles->emplace_back(file.data(), file.size(), dstArena);
    }
}

void RunClangTraitProbe(
    const Span<const MString> uniqueHeaders,
    const Span<const char*> argPtrs,
    const Span<ClassDecl* const> classes,
    const Span<InterfaceDecl* const> ifaces,
    const Span<MethodDecl* const> freeFunctions,
    CachedTraitResolution& outResult
){
    MetaArena& arena = outResult.arena;

    MString syntheticPreamble(arena);
    syntheticPreamble.reserve(4096u + uniqueHeaders.size() * 64u);

    for(const MString& hdr : uniqueHeaders){
        syntheticPreamble.append("#include <");
        syntheticPreamble.append(hdr.data(), hdr.size());
        syntheticPreamble.append(">\n");
    }

    syntheticPreamble.append("class ArenaBase;\nnamespace ");
    syntheticPreamble.append(s_TraitProbeNamespace.data(), s_TraitProbeNamespace.size());
    syntheticPreamble.append("{\n");

    const auto appendProbeConst = [&](const MStringView name, const u32 value){
        syntheticPreamble.append("inline constexpr unsigned ");
        syntheticPreamble.append(name.data(), name.size());
        syntheticPreamble.append(" = ");
        AppendUnsignedDecimal(syntheticPreamble, value);
        syntheticPreamble.append("u;\n");
    };
    appendProbeConst("k_flag_abstract",             s_ProbeFlagAbstract);
    appendProbeConst("k_flag_polymorphic",          s_ProbeFlagPolymorphicBase);
    appendProbeConst("k_flag_empty_base",           s_ProbeFlagEmptyBase);
    appendProbeConst("k_flag_copy_constructible",   s_ProbeFlagCopyConstructible);
    appendProbeConst("k_flag_copy_assignable",      s_ProbeFlagCopyAssignable);
    appendProbeConst("k_flag_move_constructible",   s_ProbeFlagMoveConstructible);
    appendProbeConst("k_flag_move_assignable",      s_ProbeFlagMoveAssignable);
    appendProbeConst("k_flag_zero_default",         s_ProbeFlagZeroDefault);
    appendProbeConst("k_flag_nothrow_zero_default", s_ProbeFlagNothrowZeroDefault);
    appendProbeConst("k_flag_arena_default",        s_ProbeFlagArenaDefault);
    appendProbeConst("k_flag_nothrow_arena_default", s_ProbeFlagNothrowArenaDefault);
    appendProbeConst("k_flag_small_value",          s_ProbeFlagSmallValue);
    appendProbeConst("k_flag_binary_serializable",  s_ProbeFlagBinarySerializable);
    appendProbeConst("k_flag_equality_switch",      s_ProbeFlagEqualitySwitch);
    appendProbeConst("k_flag_resolved_complete",    s_ProbeFlagResolvedComplete);
    appendProbeConst("k_flag_resolved_incomplete",  s_ProbeFlagResolvedIncomplete);
    appendProbeConst("k_flag_leading_arena_tag",    s_ProbeFlagLeadingArenaTag);

    syntheticPreamble.append(
        "template<typename T, bool Complete = requires { sizeof(T); }>\n"
        "struct Probe{\n"
        "    static constexpr unsigned k_flags = k_flag_resolved_incomplete;\n"
        "};\n"
        "template<typename T>\n"
        "struct Probe<T, true>{\n"
        "    static constexpr bool k_abstract = __is_abstract(T);\n"
        "    static constexpr bool k_polymorphic = __is_polymorphic(T) && !__is_final(T);\n"
        "    static constexpr bool k_empty_base = !k_abstract && !k_polymorphic && !__is_final(T) && __is_empty(T);\n"
        "    static constexpr bool k_copy_constructible = !k_abstract && __is_constructible(T, const T&);\n"
        "    static constexpr bool k_copy_assignable = !k_abstract && __is_assignable(T&, const T&);\n"
        "    static constexpr bool k_move_constructible = !k_abstract && __is_constructible(T, T&&);\n"
        "    static constexpr bool k_move_assignable = !k_abstract && __is_assignable(T&, T&&);\n"
        "    static constexpr bool k_zero_default = !k_abstract && __is_constructible(T);\n"
        "    static constexpr bool k_nothrow_zero_default = k_zero_default && __is_nothrow_constructible(T);\n"
        "    static constexpr bool k_arena_default = !k_abstract && __is_constructible(T, ::ArenaBase&);\n"
        "    static constexpr bool k_nothrow_arena_default = k_arena_default && __is_nothrow_constructible(T, ::ArenaBase&);\n"
        "    static constexpr bool k_small_value = !k_abstract && !k_polymorphic && k_copy_constructible && sizeof(T) <= 16u && __is_trivially_copyable(T);\n"
        "    static constexpr bool k_binary_serializable = !k_abstract && !k_polymorphic && __is_trivially_copyable(T) && __is_standard_layout(T);\n"
        "    static constexpr bool k_equality_switch = requires(const T& a, const T& b){ static_cast<bool>(a == b); };\n"
        "    static constexpr bool k_leading_arena_tag = requires{ typename T::uses_leading_arena_t; requires __is_same(typename T::uses_leading_arena_t, ::ArenaBase); };\n"
        "    static constexpr unsigned k_flags = k_flag_resolved_complete\n"
        "        | (k_abstract ? k_flag_abstract : 0u)\n"
        "        | (k_polymorphic ? k_flag_polymorphic : 0u)\n"
        "        | (k_empty_base ? k_flag_empty_base : 0u)\n"
        "        | (k_copy_constructible ? k_flag_copy_constructible : 0u)\n"
        "        | (k_copy_assignable ? k_flag_copy_assignable : 0u)\n"
        "        | (k_move_constructible ? k_flag_move_constructible : 0u)\n"
        "        | (k_move_assignable ? k_flag_move_assignable : 0u)\n"
        "        | (k_zero_default ? k_flag_zero_default : 0u)\n"
        "        | (k_nothrow_zero_default ? k_flag_nothrow_zero_default : 0u)\n"
        "        | (k_arena_default ? k_flag_arena_default : 0u)\n"
        "        | (k_nothrow_arena_default ? k_flag_nothrow_arena_default : 0u)\n"
        "        | (k_small_value ? k_flag_small_value : 0u)\n"
        "        | (k_binary_serializable ? k_flag_binary_serializable : 0u)\n"
        "        | (k_equality_switch ? k_flag_equality_switch : 0u)\n"
        "        | (k_leading_arena_tag ? k_flag_leading_arena_tag : 0u);\n"
        "};\n"
    );

    const auto resolveProbeCppName = [](const MStringView cppExport, const MStringView name)noexcept -> MStringView{
        return !cppExport.empty() ? cppExport : name;
    };
    const auto isUnparameterizedConcreteClass = [&](const ClassDecl& cls)noexcept -> MStringView{
        if(!cls.templateParams.empty())
            return {};
        const MStringView rawCpp = resolveProbeCppName(cls.cppExportName, cls.name);
        if(rawCpp.empty() || HasDisallowedProbeSyntax(rawCpp) || rawCpp.find('<') != MStringView::npos)
            return {};
        return rawCpp;
    };

    b8 needsTemplateWalk = !freeFunctions.empty();
    b8 hasConcreteTemplateCandidateCheck = false;
    for(const ClassDecl* cls : classes){
        if(!cls->templateParams.empty())
            needsTemplateWalk = true;
        else if(!isUnparameterizedConcreteClass(*cls).empty())
            hasConcreteTemplateCandidateCheck = true;
    }
    for(const InterfaceDecl* iface : ifaces){
        if(!iface->templateParams.empty())
            needsTemplateWalk = true;
    }

    const auto buildSyntheticSource = [&](const Span<const u8> arenaTemplatedClasses) -> MString{
        MString src(syntheticPreamble.data(), syntheticPreamble.size(), arena);
        src.reserve(syntheticPreamble.size() + (classes.size() + ifaces.size()) * 128u + 16u);
        for(usize ci = 0u; ci < classes.size(); ++ci){
            const ClassDecl* cls = classes[ci];
            if(!cls->templateParams.empty())
                continue;
            const MStringView rawCpp = resolveProbeCppName(cls->cppExportName, cls->name);
            if(rawCpp.empty() || HasDisallowedProbeSyntax(rawCpp))
                continue;
            src.append("using ");
            src.append(s_TraitProbeClassAliasPrefix.data(), s_TraitProbeClassAliasPrefix.size());
            AppendUnsignedDecimal(src, ci);
            src.append(" = ");
            src.append(rawCpp.data(), rawCpp.size());
            if(arenaTemplatedClasses[ci] != 0u && rawCpp.find('<') == MStringView::npos)
                src.append("<::ArenaBase>");
            src.append(";\ninline constexpr unsigned ");
            src.append(s_TraitProbeFlagVarPrefix.data(), s_TraitProbeFlagVarPrefix.size());
            AppendUnsignedDecimal(src, ci);
            src.append(" = Probe<");
            src.append(s_TraitProbeClassAliasPrefix.data(), s_TraitProbeClassAliasPrefix.size());
            AppendUnsignedDecimal(src, ci);
            src.append(">::k_flags;\n");
        }
        for(usize ii = 0u; ii < ifaces.size(); ++ii){
            const InterfaceDecl* iface = ifaces[ii];
            if(!iface->templateParams.empty())
                continue;
            const MStringView rawCpp = resolveProbeCppName(iface->cppExportName, iface->name);
            if(rawCpp.empty() || HasDisallowedProbeSyntax(rawCpp))
                continue;
            src.append("using ");
            src.append(s_TraitProbeIfaceAliasPrefix.data(), s_TraitProbeIfaceAliasPrefix.size());
            AppendUnsignedDecimal(src, ii);
            src.append(" = ");
            src.append(rawCpp.data(), rawCpp.size());
            src.append(";\n");
        }
        src.append("}\n");
        return src;
    };

    MVector<u8> arenaTemplatedClasses(arena);
    arenaTemplatedClasses.assign(classes.size(), 0u);
    MString syntheticSource = buildSyntheticSource(Span<const u8>(arenaTemplatedClasses.data(), arenaTemplatedClasses.size()));

    const MString virtualTuPath = BuildVirtualTuPath(arena);

    CXUnsavedFile unsavedFile{};
    unsavedFile.Filename = virtualTuPath.c_str();
    unsavedFile.Contents = syntheticSource.c_str();
    unsavedFile.Length = static_cast<unsigned long>(syntheticSource.size());

    outResult.classOutcomes.reserve(classes.size());
    for(usize ci = 0u; ci < classes.size(); ++ci){
        CachedClassOutcome co(arena);
        InitMethodOutcomes(arena, Span<MethodDecl* const>(classes[ci]->methods.data(), classes[ci]->methods.size()), co.methods);
        outResult.classOutcomes.push_back(Move(co));
    }
    outResult.ifaceOutcomes.reserve(ifaces.size());
    for(usize ii = 0u; ii < ifaces.size(); ++ii){
        CachedInterfaceOutcome io(arena);
        InitMethodOutcomes(arena, Span<MethodDecl* const>(ifaces[ii]->methods.data(), ifaces[ii]->methods.size()), io.methods);
        outResult.ifaceOutcomes.push_back(Move(io));
    }
    outResult.freeFnOutcomes.resize(freeFunctions.size());

    const CXIndex index = ::clang_createIndex(0, 0);
    if(!index)
        return;

    CXTranslationUnit tu = nullptr;
    const unsigned parseFlags = CXTranslationUnit_PrecompiledPreamble
        | CXTranslationUnit_SkipFunctionBodies
        | CXTranslationUnit_IgnoreNonErrorsFromIncludedFiles
        | CXTranslationUnit_KeepGoing;
    const auto parseVirtualTu = [&]() -> b8{
        const CXErrorCode res = ::clang_parseTranslationUnit2(
            index,
            virtualTuPath.c_str(),
            argPtrs.data(),
            static_cast<int>(argPtrs.size()),
            &unsavedFile,
            1u,
            parseFlags,
            &tu
        );
        return res == CXError_Success && tu != nullptr;
    };
    if(!parseVirtualTu()){
        ::clang_disposeIndex(index);
        return;
    }

    const auto scanProbeNamespace = [&](
        const CXCursor root,
        MVector<CXCursor>& outClassRecords,
        MVector<CXCursor>& outIfaceRecords,
        MVector<u8>& outClassEvaluated,
        MVector<u64>& outClassFlags
    ){
        outClassRecords.assign(classes.size(), CXCursor{});
        outIfaceRecords.assign(ifaces.size(), CXCursor{});
        outClassEvaluated.assign(classes.size(), 0u);
        outClassFlags.assign(classes.size(), 0u);

        const auto tryResolveProbeAliasTarget = [](const CXCursor aliasCursor, const MStringView name, const MStringView prefix, MVector<CXCursor>& outTargets) -> b8{
            if(!name.starts_with(prefix))
                return false;
            u64 idx = 0u;
            if(LiteralDecoder::TryParseUnsignedIntegerLiteral(name.substr(prefix.size()), idx) && idx < outTargets.size()){
                const CXType canonical = ::clang_getCanonicalType(::clang_getTypedefDeclUnderlyingType(aliasCursor));
                const CXCursor rec = ::clang_getTypeDeclaration(canonical);
                const CXCursor def = ::clang_getCursorDefinition(rec);
                outTargets[static_cast<usize>(idx)] = ::clang_Cursor_isNull(def) ? rec : def;
            }
            return true;
        };

        MVector<CXCursor> topLevel(arena);
        VisitDirectChildren(root, topLevel);
        for(const CXCursor c : topLevel){
            if(::clang_getCursorKind(c) != CXCursor_Namespace)
                continue;
            const MString nsName = ConsumeClangString(arena, ::clang_getCursorSpelling(c));
            if(nsName != s_TraitProbeNamespace)
                continue;

            MVector<CXCursor> probeDecls(arena);
            VisitDirectChildren(c, probeDecls);
            for(const CXCursor pd : probeDecls){
                const CXCursorKind pk = ::clang_getCursorKind(pd);
                const MString pdNameStr = ConsumeClangString(arena, ::clang_getCursorSpelling(pd));
                const MStringView pdName(pdNameStr.data(), pdNameStr.size());

                if(pk == CXCursor_TypeAliasDecl){
                    if(tryResolveProbeAliasTarget(pd, pdName, s_TraitProbeClassAliasPrefix, outClassRecords))
                        continue;
                    if(tryResolveProbeAliasTarget(pd, pdName, s_TraitProbeIfaceAliasPrefix, outIfaceRecords))
                        continue;
                }
                if(pk == CXCursor_VarDecl && pdName.starts_with(s_TraitProbeFlagVarPrefix)){
                    u64 idx = 0u;
                    if(!LiteralDecoder::TryParseUnsignedIntegerLiteral(pdName.substr(s_TraitProbeFlagVarPrefix.size()), idx) || idx >= classes.size())
                        continue;
                    CXEvalResult eval = ::clang_Cursor_Evaluate(pd);
                    if(!eval)
                        continue;
                    if(::clang_EvalResult_getKind(eval) == CXEval_Int){
                        outClassEvaluated[static_cast<usize>(idx)] = 1u;
                        outClassFlags[static_cast<usize>(idx)] = static_cast<u64>(::clang_EvalResult_getAsUnsigned(eval));
                    }
                    ::clang_EvalResult_dispose(eval);
                }
            }
            break;
        }
    };

    CXCursor rootCursor = ::clang_getTranslationUnitCursor(tu);
    MVector<CXCursor> concreteClassRecords(arena);
    MVector<CXCursor> concreteIfaceRecords(arena);
    MVector<u8> classEvaluated(arena);
    MVector<u64> classFlags(arena);
    scanProbeNamespace(rootCursor, concreteClassRecords, concreteIfaceRecords, classEvaluated, classFlags);

    SymbolIndex symIndex(arena);
    b8 symIndexPopulated = false;

    if(hasConcreteTemplateCandidateCheck){
        b8 anyUnresolvedConcrete = false;
        for(usize ci = 0u; ci < classes.size(); ++ci){
            if(classEvaluated[ci] == 0u && !isUnparameterizedConcreteClass(*classes[ci]).empty()){
                anyUnresolvedConcrete = true;
                break;
            }
        }
        if(anyUnresolvedConcrete){
            IndexScopeSymbols(arena, rootCursor, {}, false, symIndex);
            symIndexPopulated = true;
            b8 reparseNeeded = false;
            for(usize ci = 0u; ci < classes.size(); ++ci){
                if(classEvaluated[ci] != 0u)
                    continue;
                const MStringView rawCpp = isUnparameterizedConcreteClass(*classes[ci]);
                if(rawCpp.empty())
                    continue;
                const MString normCpp = NormalizeCppQualifiedName(arena, rawCpp);
                const auto tplIt = symIndex.classTemplates.find(MStringView(normCpp.data(), normCpp.size()));
                if(tplIt != symIndex.classTemplates.end() && IsSingleRequiredArenaClassTemplate(arena, tplIt->second)){
                    arenaTemplatedClasses[ci] = 1u;
                    outResult.classOutcomes[ci].isArenaOwner = true;
                    reparseNeeded = true;
                }
            }
            if(reparseNeeded){
                symIndex.classTemplates.clear();
                symIndex.freeFunctions.clear();
                symIndex.typeRecords.clear();
                symIndexPopulated = false;
                syntheticSource = buildSyntheticSource(Span<const u8>(arenaTemplatedClasses.data(), arenaTemplatedClasses.size()));
                unsavedFile.Contents = syntheticSource.c_str();
                unsavedFile.Length = static_cast<unsigned long>(syntheticSource.size());
                if(::clang_reparseTranslationUnit(tu, 1u, &unsavedFile, ::clang_defaultReparseOptions(tu)) != 0){
                    ::clang_disposeTranslationUnit(tu);
                    tu = nullptr;
                    if(!parseVirtualTu()){
                        ::clang_disposeIndex(index);
                        return;
                    }
                }
                rootCursor = ::clang_getTranslationUnitCursor(tu);
                scanProbeNamespace(rootCursor, concreteClassRecords, concreteIfaceRecords, classEvaluated, classFlags);
            }
        }
    }

    MString projectRootNorm(arena);
#if defined(NWB_PROJECT_SOURCE_DIR)
    projectRootNorm = NormalizeSlashes(arena, MStringView(NWB_PROJECT_SOURCE_DIR));
#endif
    const MStringView projectRootView(projectRootNorm.data(), projectRootNorm.size());

    MVector<MString> allIncludedFiles(arena);
    CollectTranslationUnitInclusions(arena, tu, projectRootView, true, allIncludedFiles, &outResult.projectIncludedFiles);

    outResult.watchedStamps.reserve(allIncludedFiles.size());
    for(const MString& file : allIncludedFiles){
        CachedFileStamp stamp(arena);
        if(QueryFileStamp(arena, MStringView(file.data(), file.size()), stamp))
            outResult.watchedStamps.push_back(Move(stamp));
    }

    if(needsTemplateWalk && !symIndexPopulated)
        IndexScopeSymbols(arena, rootCursor, {}, false, symIndex);

    HashSet<CursorKey, MetaArena, CursorKeyHash> fieldVisited(arena);
    for(usize ci = 0u; ci < classes.size(); ++ci){
        const ClassDecl* cls = classes[ci];
        CachedClassOutcome& co = outResult.classOutcomes[ci];

        CXCursor recCursor = concreteClassRecords[ci];
        if(!cls->templateParams.empty()){
            const MStringView rawCpp = !cls->cppExportName.empty() ? cls->cppExportName : cls->name;
            const MString normCpp = NormalizeCppQualifiedName(arena, rawCpp);
            const auto tplIt = symIndex.classTemplates.find(MStringView(normCpp.data(), normCpp.size()));
            if(tplIt != symIndex.classTemplates.end()){
                const CXCursor unwrapped = UnwrapToRecordOrClassTemplate(arena, tplIt->second);
                if(!::clang_Cursor_isNull(unwrapped)){
                    recCursor = unwrapped;
                    DeduceClassTemplateTraits(arena, unwrapped, co);
                }
            }
        }
        else if(classEvaluated[ci] != 0u){
            const u64 flags = classFlags[ci];
            if((flags & s_ProbeFlagResolvedComplete) != 0u){
                co.resolved = true;
                co.isAbstract = (flags & s_ProbeFlagAbstract) != 0u;
                co.isPolymorphicBase = (flags & s_ProbeFlagPolymorphicBase) != 0u;
                co.isEmptyBase = (flags & s_ProbeFlagEmptyBase) != 0u;
                co.isCopyConstructible = (flags & s_ProbeFlagCopyConstructible) != 0u;
                co.isCopyAssignable = (flags & s_ProbeFlagCopyAssignable) != 0u;
                co.isMoveConstructible = (flags & s_ProbeFlagMoveConstructible) != 0u;
                co.isMoveAssignable = (flags & s_ProbeFlagMoveAssignable) != 0u;
                co.isSmallValue = (flags & s_ProbeFlagSmallValue) != 0u;
                co.isEqualitySwitch = (flags & s_ProbeFlagEqualitySwitch) != 0u;

                co.arenaCtorMode = ((flags & s_ProbeFlagLeadingArenaTag) != 0u)
                    ? ArenaCtorMode::First
                    : DeduceRecordArenaCtorMode(arena, recCursor, co.isArenaOwner);
                const b8 usesArenaCtor = (co.isArenaOwner && co.arenaCtorMode != ArenaCtorMode::Omit) || co.arenaCtorMode == ArenaCtorMode::First;
                const b8 hasZeroDefault = (flags & s_ProbeFlagZeroDefault) != 0u;
                const b8 hasNothrowZeroDefault = (flags & s_ProbeFlagNothrowZeroDefault) != 0u;
                const b8 hasArenaDefault = (flags & s_ProbeFlagArenaDefault) != 0u;
                const b8 hasNothrowArenaDefault = (flags & s_ProbeFlagNothrowArenaDefault) != 0u;

                co.isSelfDefaulted = !co.isAbstract && ((usesArenaCtor && hasArenaDefault) || hasZeroDefault);
                co.isNothrowDefaultConstructible = !co.isAbstract && ((usesArenaCtor && hasArenaDefault)
                    ? hasNothrowArenaDefault
                    : (hasZeroDefault && hasNothrowZeroDefault));
                co.emitsBraceFieldInit = !co.isAbstract && hasZeroDefault && !usesArenaCtor;

                if((flags & s_ProbeFlagBinarySerializable) != 0u){
                    fieldVisited.clear();
                    co.isBinarySerializable = !HasPointerOrReferenceFieldRecursive(arena, recCursor, fieldVisited);
                }
                else{
                    co.isBinarySerializable = false;
                }
            }
            else if((flags & s_ProbeFlagResolvedIncomplete) != 0u){
                co.resetIncomplete();
            }
        }

        ResolveRecordMethodOutcomes(
            arena,
            recCursor,
            Span<MethodDecl* const>(cls->methods.data(), cls->methods.size()),
            co.methods
        );
    }

    for(usize ii = 0u; ii < ifaces.size(); ++ii){
        const InterfaceDecl* iface = ifaces[ii];
        ResolveRecordMethodOutcomes(
            arena,
            concreteIfaceRecords[ii],
            Span<MethodDecl* const>(iface->methods.data(), iface->methods.size()),
            outResult.ifaceOutcomes[ii].methods
        );
    }

    for(usize fi = 0u; fi < freeFunctions.size(); ++fi){
        const MethodDecl* fn = freeFunctions[fi];
        const MStringView rawCpp = !fn->cppExportName.empty() ? fn->cppExportName : fn->name;
        const MString normCpp = NormalizeCppQualifiedName(arena, rawCpp);
        const MStringView normView(normCpp.data(), normCpp.size());

        const MVector<CppCallableCandidate>* cands = nullptr;
        MVector<CppCallableCandidate> ctorCands(arena);
        if(const auto fnIt = symIndex.freeFunctions.find(normView); fnIt != symIndex.freeFunctions.end()){
            cands = &fnIt->second;
        }
        else if(const auto trIt = symIndex.typeRecords.find(normView); trIt != symIndex.typeRecords.end()){
            const CXCursor rec = UnwrapToRecordOrClassTemplate(arena, trIt->second);
            if(!::clang_Cursor_isNull(rec)){
                MVector<CXCursor> recChildren(arena);
                VisitDirectChildren(rec, recChildren);
                for(const CXCursor child : recChildren){
                    const CXCursorKind ck = ::clang_getCursorKind(child);
                    const b8 isTplCtor = (ck == CXCursor_FunctionTemplate && ::clang_getTemplateCursorKind(child) == CXCursor_Constructor);
                    if((ck == CXCursor_Constructor || isTplCtor)
                        && ::clang_getCXXAccessSpecifier(child) == CX_CXXPublic
                        && ::clang_CXXMethod_isDeleted(child) == 0u
                    ){
                        ctorCands.push_back(BuildCandidateFromCursor(arena, child));
                    }
                }
                if(!ctorCands.empty())
                    cands = &ctorCands;
            }
        }

        if(!cands || cands->empty()){
            outResult.freeFnOutcomes[fi] = CachedFreeFunctionOutcome{};
            continue;
        }

        ResolveFreeFunctionOutcome(arena, Span<const CppCallableCandidate>(cands->data(), cands->size()), *fn, outResult.freeFnOutcomes[fi]);
    }

    ::clang_disposeTranslationUnit(tu);
    ::clang_disposeIndex(index);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 HasAnyExistingIncludeHeader(
    MetaArena& arena,
    const Span<const MString> uniqueHeaders,
    const Span<const MString> searchRoots,
    const Span<const MString> compilerArgs
){
    if(uniqueHeaders.empty())
        return false;

    MVector<MString> dirs(arena);
    MStringMap<u8> seenDirs(arena);
    const auto addDir = [&](const MStringView rawDir){
        if(rawDir.empty())
            return;
        const MString norm = NormalizeSlashes(arena, rawDir);
        AddExistingUnique(dirs, seenDirs, norm);
    };

    for(const MString& root : searchRoots)
        addDir(MStringView(root.data(), root.size()));

    for(usize ai = 0u; ai < compilerArgs.size(); ++ai){
        const MStringView arg(compilerArgs[ai].data(), compilerArgs[ai].size());
        if(arg == CppImporterSemantics::s_IncludeFlagPrefix || arg == CppImporterSemantics::s_SystemIncludeFlagPrefix){
            if(ai + 1u < compilerArgs.size()){
                const MString& nextArg = compilerArgs[++ai];
                addDir(MStringView(nextArg.data(), nextArg.size()));
            }
        }
        else if(arg.starts_with(CppImporterSemantics::s_IncludeFlagPrefix)){
            addDir(arg.substr(CppImporterSemantics::s_IncludeFlagPrefix.size()));
        }
        else if(arg.starts_with(CppImporterSemantics::s_SystemIncludeFlagPrefix)){
            addDir(arg.substr(CppImporterSemantics::s_SystemIncludeFlagPrefix.size()));
        }
    }

#if defined(NWB_PROJECT_SOURCE_DIR)
    addDir(MStringView(NWB_PROJECT_SOURCE_DIR));
#endif

    const auto fileExistsView = [&](const MStringView pathView) -> b8{
        const TString<MetaArena> nativePath = ConvertString(arena, pathView);
        const ::Path<MetaArena> pathObj(arena, TStringView(nativePath.data(), nativePath.size()));
        const auto exists = FileExists(pathObj);
        return exists && *exists;
    };

    for(const MString& hdr : uniqueHeaders){
        const MStringView hdrView(hdr.data(), hdr.size());
        if(hdrView.empty())
            continue;
        const b8 isAbs = hdrView.starts_with("/")
            || (hdrView.size() >= 2u && hdrView[1u] == ':');
        if(isAbs){
            if(fileExistsView(hdrView))
                return true;
            continue;
        }
        for(const MString& dir : dirs){
            const MString candidate = JoinWithSingleSlash(arena, MStringView(dir.data(), dir.size()), hdrView);
            if(fileExistsView(MStringView(candidate.data(), candidate.size())))
                return true;
        }
    }
    return false;
}

void StoreInMemoryCache(TraitResolutionMemoryCache& memCache, const MStringView cacheKey, GlobalUniquePtr<CachedTraitResolution>&& entry){
    FutexGuard lock(memCache.lock);
    if(memCache.entries.size() >= s_MaxMemoryCacheEntries)
        memCache.entries.clear();
    MString mapKey(cacheKey.data(), cacheKey.size(), memCache.mapArena);
    memCache.entries.emplace(Move(mapKey), Move(entry));
}

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


u32 CppExternTraitResolver::SchemaVersion()noexcept{
    return __hidden_cpp_extern_trait_resolver::s_ExternTraitCacheVersion;
}

void CppExternTraitResolver::ResolveTranslationUnitTraits(
    const CppExternTraitResolveOptions& options,
    MetaArena& arena,
    TranslationUnit& unit
){
    using namespace __hidden_cpp_extern_trait_resolver;

    MVector<ClassDecl*> externClasses(arena);
    MVector<InterfaceDecl*> externIfaces(arena);
    MVector<MethodDecl*> externFreeFns(arena);

    for(ClassDecl* cls : unit.classes){
        if(cls && cls->isExtern)
            externClasses.push_back(cls);
    }
    for(InterfaceDecl* iface : unit.interfaces){
        if(iface && iface->isExtern)
            externIfaces.push_back(iface);
    }
    for(MethodDecl* fn : unit.freeFunctions){
        if(fn && fn->isExtern)
            externFreeFns.push_back(fn);
    }

    if(externClasses.empty() && externIfaces.empty() && externFreeFns.empty())
        return;

    MVector<MString> orderedHeaders(arena);
    MStringMap<u8> seenHeaders(arena);
    const auto normalizeHeader = [&](const MStringView rawHeader, MString& outNorm) -> b8{
        const MStringView inner = StripHeaderDelimiters(rawHeader);
        if(inner.empty())
            return false;
        outNorm = NormalizeSlashes(arena, inner);
        return true;
    };

    for(const MString& h : options.extraHeaders){
        MString norm(arena);
        if(normalizeHeader(MStringView(h.data(), h.size()), norm))
            AddExistingUnique(orderedHeaders, seenHeaders, norm);
    }

    MVector<MString> declHeaders(arena);
    MStringMap<u8> seenDeclHeaders(arena);
    const auto addDeclHeader = [&](const MStringView rawHeader){
        MString norm(arena);
        if(!normalizeHeader(rawHeader, norm))
            return;
        if(seenHeaders.find(MStringView(norm.data(), norm.size())) != seenHeaders.end())
            return;
        AddExistingUnique(declHeaders, seenDeclHeaders, norm);
    };

    for(const ClassDecl* cls : externClasses){
        addDeclHeader(cls->requiredHeader);
        for(const MethodDecl* m : cls->methods){
            if(m && m->isExtern)
                addDeclHeader(m->requiredHeader);
        }
    }
    for(const InterfaceDecl* iface : externIfaces){
        addDeclHeader(iface->requiredHeader);
        for(const MethodDecl* m : iface->methods){
            if(m && m->isExtern)
                addDeclHeader(m->requiredHeader);
        }
    }
    for(const MethodDecl* fn : externFreeFns)
        addDeclHeader(fn->requiredHeader);

    Sort(declHeaders.begin(), declHeaders.end(), [](const MString& a, const MString& b)noexcept{
        return MStringView(a.data(), a.size()) < MStringView(b.data(), b.size());
    });
    for(const MString& h : declHeaders)
        AddExistingUnique(orderedHeaders, seenHeaders, h);

    if(orderedHeaders.empty())
        return;

    MVector<MString> effectiveDefines(arena);
    MStringMap<u8> seenDefines(arena);
    for(const MString& def : options.defines){
        if(!def.empty())
            AddExistingUnique(effectiveDefines, seenDefines, def);
    }

    CppHeaderImportOptions clangOpts;
    clangOpts.includeDirectories = options.includeDirectories;
    clangOpts.defines = Span<const MString>(effectiveDefines.data(), effectiveDefines.size());
    clangOpts.compilerArgs = options.compilerArgs;
    clangOpts.enableAutoProjectIncludes = options.enableAutoProjectIncludes;

    MVector<MString> argStorage(arena);
    MVector<MString> normalizedRoots(arena);
    MVector<const char*> argPtrs(arena);
    BuildClangArguments(arena, clangOpts, argStorage, normalizedRoots, argPtrs);

    if(!HasAnyExistingIncludeHeader(
        arena,
        Span<const MString>(orderedHeaders.data(), orderedHeaders.size()),
        Span<const MString>(normalizedRoots.data(), normalizedRoots.size()),
        options.compilerArgs
    ))
        return;

    const MString cacheKey = BuildTraitCacheKey(
        arena,
        Span<const MString>(argStorage.data(), argStorage.size()),
        Span<const MString>(orderedHeaders.data(), orderedHeaders.size()),
        Span<ClassDecl* const>(externClasses.data(), externClasses.size()),
        Span<InterfaceDecl* const>(externIfaces.data(), externIfaces.size()),
        Span<MethodDecl* const>(externFreeFns.data(), externFreeFns.size())
    );
    const MStringView cacheKeyView(cacheKey.data(), cacheKey.size());
    const u64 keyHash = UpdateFnv64TextExact(s_Fnv64OffsetBasis, cacheKeyView);

    const auto applyOutcome = [&](const CachedTraitResolution& outcome){
        ApplyOutcomeToTranslationUnit(
            outcome,
            Span<ClassDecl* const>(externClasses.data(), externClasses.size()),
            Span<InterfaceDecl* const>(externIfaces.data(), externIfaces.size()),
            Span<MethodDecl* const>(externFreeFns.data(), externFreeFns.size()),
            options.outProjectIncludedFiles,
            arena
        );
    };

    if(options.useDiskCache){
        TraitResolutionMemoryCache& memCache = GetMemoryCache();
        GlobalUniquePtr<CachedTraitResolution> diskLoaded;
        {
            FutexGuard lock(memCache.lock);
            if(const auto it = memCache.entries.find(cacheKeyView); it != memCache.entries.end() && it->second){
                const auto& stamps = it->second->watchedStamps;
                if(VerifyCachedFileStamps(arena, Span<const CachedFileStamp>(stamps.data(), stamps.size()))){
                    applyOutcome(*it->second);
                    return;
                }
                memCache.entries.erase(it);
            }
            diskLoaded = MakeGlobalUnique<CachedTraitResolution>(memCache.mapArena);
        }

        if(diskLoaded && TryLoadDiskCache(
            keyHash,
            cacheKeyView,
            externClasses.size(),
            externIfaces.size(),
            externFreeFns.size(),
            arena,
            *diskLoaded
        )){
            applyOutcome(*diskLoaded);
            StoreInMemoryCache(memCache, cacheKeyView, Move(diskLoaded));
            return;
        }
        {
            FutexGuard lock(memCache.lock);
            diskLoaded.reset();
        }
    }

    GlobalUniquePtr<CachedTraitResolution> resolved;
    if(options.useDiskCache){
        TraitResolutionMemoryCache& memCache = GetMemoryCache();
        FutexGuard lock(memCache.lock);
        resolved = MakeGlobalUnique<CachedTraitResolution>(memCache.mapArena);
    }
    else{
        resolved = MakeGlobalUnique<CachedTraitResolution>(arena);
    }
    if(!resolved)
        return;

    resolved->cacheKey.assign(cacheKey.data(), cacheKey.size());
    RunClangTraitProbe(
        Span<const MString>(orderedHeaders.data(), orderedHeaders.size()),
        Span<const char*>(argPtrs.data(), argPtrs.size()),
        Span<ClassDecl* const>(externClasses.data(), externClasses.size()),
        Span<InterfaceDecl* const>(externIfaces.data(), externIfaces.size()),
        Span<MethodDecl* const>(externFreeFns.data(), externFreeFns.size()),
        *resolved
    );

    applyOutcome(*resolved);

    if(options.useDiskCache && !resolved->watchedStamps.empty()){
        SaveDiskCache(keyHash, *resolved, arena);
        StoreInMemoryCache(GetMemoryCache(), cacheKeyView, Move(resolved));
    }
    else if(options.useDiskCache){
        TraitResolutionMemoryCache& memCache = GetMemoryCache();
        FutexGuard lock(memCache.lock);
        resolved.reset();
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

