// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cpp_header_importer_internal.h"

#include "../ast/builtin_type_semantics.h"
#include "../ast/keyword_semantics.h"

#include <global/array.h>

#include <cstdio>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CppHeaderImportDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ScalarTypeMapEntry{
    AStringView cppName;
    AStringView adhdName;
};

inline constexpr ScalarTypeMapEntry s_DirectTypeMap[] = {
    { "void", "void" },
    { "bool", "b8" },
    { "b8", "b8" },
    { "i8", "i8" },
    { "u8", "u8" },
    { "i16", "i16" },
    { "u16", "u16" },
    { "i32", "i32" },
    { "u32", "u32" },
    { "i64", "i64" },
    { "u64", "u64" },
    { "isize", "isize" },
    { "usize", "usize" },
    { "f32", "f32" },
    { "f64", "f64" },
    { "int8_t", "i8" },
    { "uint8_t", "u8" },
    { "int16_t", "i16" },
    { "uint16_t", "u16" },
    { "int32_t", "i32" },
    { "uint32_t", "u32" },
    { "int64_t", "i64" },
    { "uint64_t", "u64" },
    { "size_t", "usize" },
    { "ptrdiff_t", "isize" },
    { "intptr_t", "isize" },
    { "uintptr_t", "usize" },
    { "AStringView", "AStringView" },
    { "WStringView", "WStringView" },
    { "TStringView", "TStringView" },
    { "StringView", "StringView" },
    { "AString", "AString" },
    { "WString", "WString" },
    { "TString", "TString" },
    { "ErrorCode", "ErrorCode" },
    { "Name", "Name" },
    { "Path", "Path" }
};

struct GenericContainerMapEntry{
    AStringView cppName;
    AStringView adhdName;
    u32 maxArgs;
};

inline constexpr GenericContainerMapEntry s_GenericContainerMap[] = {
    { "Vector", "Vector", 1u },
    { "ParallelVector", "ParallelVector", 1u },
    { "ForwardList", "ForwardList", 1u },
    { "List", "List", 1u },
    { "Deque", "Deque", 1u },
    { "Set", "Set", 1u },
    { "HashSet", "HashSet", 1u },
    { "ParallelHashSet", "ParallelHashSet", 1u },
    { "Map", "Map", 2u },
    { "HashMap", "HashMap", 2u },
    { "ParallelHashMap", "ParallelHashMap", 2u },
    { "Queue", "Queue", 1u },
    { "ParallelQueue", "ParallelQueue", 1u },
    { "ParallelBlockQueue", "ParallelBlockQueue", 2u },
    { "Array", "Array", 2u },
    { "FixedVector", "FixedVector", 2u },
    { "Span", "Span", 1u },
    { "Optional", "Optional", 1u },
    { "Atomic", "Atomic", 1u },
    { "Expected", "Expected", 2u },
    { "CompressedPair", "CompressedPair", 2u },
    { "FixedIndexMap", "FixedIndexMap", 1u },
    { "RefCountPtr", "RefCountPtr", 1u },
    { "UniquePtr", "UniquePtr", 1u },
    { "NotNullUniquePtr", "NotNullUniquePtr", 1u },
    { "AssetRef", "AssetRef", 1u },
    { "ComponentPool", "ComponentPool", 1u },
    { "TypedAsset", "TypedAsset", 1u },
    { "AssetCodec", "AssetCodec", 1u }
};

[[nodiscard]] MStringView StripConstAndRefPrefix(MStringView text)noexcept{
    for(;;){
        while(!text.empty() && text.front() == ' ')
            text.remove_prefix(1u);
        while(!text.empty() && text.back() == ' ')
            text.remove_suffix(1u);

        if(text.starts_with("const ")){
            text.remove_prefix(6u);
            continue;
        }
        if(text.starts_with("volatile ")){
            text.remove_prefix(9u);
            continue;
        }
        if(text.starts_with("class ")){
            text.remove_prefix(6u);
            continue;
        }
        if(text.starts_with("struct ")){
            text.remove_prefix(7u);
            continue;
        }
        if(text.starts_with("enum ")){
            text.remove_prefix(5u);
            continue;
        }
        break;
    }
    while(!text.empty() && (text.back() == '&' || text.back() == '*' || text.back() == ' '))
        text.remove_suffix(1u);
    if(text.ends_with(" const")){
        text.remove_suffix(6u);
        while(!text.empty() && text.back() == ' ')
            text.remove_suffix(1u);
    }
    return text;
}

[[nodiscard]] MStringView ExtractUnqualifiedLeaf(const MStringView text)noexcept{
    const usize anglePos = text.find('<');
    const MStringView base = (anglePos == MStringView::npos) ? text : text.substr(0u, anglePos);
    const usize scopePos = base.rfind("::");
    return (scopePos == MStringView::npos) ? base : base.substr(scopePos + 2u);
}

[[nodiscard]] MStringView ExtractNthTemplateArgText(const MStringView spelling, const usize targetIndex)noexcept{
    const usize ltPos = spelling.find('<');
    if(ltPos == MStringView::npos)
        return {};

    u32 angleDepth = 0u;
    u32 parenDepth = 0u;
    usize argIndex = 0u;
    usize argStart = ltPos + 1u;
    for(usize i = ltPos + 1u; i < spelling.size(); ++i){
        const MChar ch = spelling[i];
        if(ch == '<'){
            ++angleDepth;
        }
        else if(ch == '>'){
            if(angleDepth == 0u){
                if(argIndex == targetIndex){
                    MStringView slice = spelling.substr(argStart, i - argStart);
                    while(!slice.empty() && (slice.front() == ' ' || slice.front() == '\t'))
                        slice.remove_prefix(1u);
                    while(!slice.empty() && (slice.back() == ' ' || slice.back() == '\t'))
                        slice.remove_suffix(1u);
                    return slice;
                }
                return {};
            }
            --angleDepth;
        }
        else if(ch == '('){
            ++parenDepth;
        }
        else if(ch == ')' && parenDepth > 0u){
            --parenDepth;
        }
        else if(ch == ',' && angleDepth == 0u && parenDepth == 0u){
            if(argIndex == targetIndex){
                MStringView slice = spelling.substr(argStart, i - argStart);
                while(!slice.empty() && (slice.front() == ' ' || slice.front() == '\t'))
                    slice.remove_prefix(1u);
                while(!slice.empty() && (slice.back() == ' ' || slice.back() == '\t'))
                    slice.remove_suffix(1u);
                return slice;
            }
            ++argIndex;
            argStart = i + 1u;
        }
    }
    return {};
}

[[nodiscard]] MStringView ExtractPositiveConstantCapacityText(MetaArena& arena, const MStringView spelling){
    MStringView arg = ExtractNthTemplateArgText(spelling, 1u);
    while(!arg.empty() && (arg.back() == 'u' || arg.back() == 'U' || arg.back() == 'l' || arg.back() == 'L'))
        arg.remove_suffix(1u);
    if(arg.empty())
        return "1";
    b8 nonZero = false;
    for(const MChar ch : arg){
        if(ch < '0' || ch > '9')
            return "1";
        if(ch != '0')
            nonZero = true;
    }
    return nonZero ? InternArenaString(arena, arg) : MStringView("1");
}

[[nodiscard]] b8 IsScalarSpelling(const MStringView s)noexcept{
    return BuiltinTypeSemantics::IsScalarType(s) || s == "b8";
}

[[nodiscard]] MStringView MapDirectTypeName(const MStringView name)noexcept{
    for(const ScalarTypeMapEntry& entry : s_DirectTypeMap){
        if(entry.cppName == name)
            return MStringView(entry.adhdName.data(), entry.adhdName.size());
    }
    return {};
}

[[nodiscard]] const GenericContainerMapEntry* FindGenericContainer(const MStringView name)noexcept{
    for(const GenericContainerMapEntry& entry : s_GenericContainerMap){
        if(entry.cppName == name)
            return &entry;
    }
    if(name.ends_with("Vector") && name.size() > 6u){
        // Custom arena vector aliases like GraphicsVector<T> map cleanly to Vector<T>.
        return &s_GenericContainerMap[0];
    }
    return nullptr;
}

[[nodiscard]] MStringView MapBuiltinScalarKind(const CXTypeKind kind)noexcept{
    switch(kind){
    case CXType_Void: return "void";
    case CXType_Bool: return "b8";
    case CXType_Char_S:
    case CXType_SChar: return "i8";
    case CXType_Char_U:
    case CXType_UChar: return "u8";
    case CXType_Short: return "i16";
    case CXType_UShort: return "u16";
    case CXType_Int: return "i32";
    case CXType_UInt: return "u32";
#if defined(FOUNDATION_PLATFORM_WINDOWS)
    case CXType_Long: return "i32";
    case CXType_ULong: return "u32";
#else
    case CXType_Long: return "isize";
    case CXType_ULong: return "usize";
#endif
    case CXType_LongLong: return "i64";
    case CXType_ULongLong: return "u64";
    case CXType_Float: return "f32";
    case CXType_Double: return "f64";
    default: return {};
    }
}

[[nodiscard]] constexpr b8 IsUnsupportedCppTypeKind(const CXTypeKind kind)noexcept{
    switch(kind){
    case CXType_Pointer:
    case CXType_MemberPointer:
    case CXType_BlockPointer:
    case CXType_FunctionProto:
    case CXType_FunctionNoProto:
    case CXType_ConstantArray:
    case CXType_IncompleteArray:
    case CXType_VariableArray:
    case CXType_DependentSizedArray:
    case CXType_NullPtr:
    case CXType_Auto:
        return true;
    default:
        return false;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


MString NormalizeSlashes(MetaArena& arena, const MStringView raw){
    MString out(raw.data(), raw.size(), arena);
    for(MChar& ch : out){
        if(ch == '\\')
            ch = '/';
    }
    return out;
}

MStringView TrimOuterQuotesAndAngles(MStringView text)noexcept{
    while(!text.empty() && (text.front() == ' ' || text.front() == '\t'))
        text.remove_prefix(1u);
    while(!text.empty() && (text.back() == ' ' || text.back() == '\t'))
        text.remove_suffix(1u);
    if(text.size() >= 2u){
        const MChar first = text.front();
        const MChar last = text.back();
        if((first == '<' && last == '>') || (first == '"' && last == '"'))
            return text.substr(1u, text.size() - 2u);
    }
    return text;
}

b8 StartsWithCaseInsensitivePrefix(const MStringView fullPath, const MStringView prefix)noexcept{
    if(fullPath.size() < prefix.size())
        return false;
#if defined(FOUNDATION_PLATFORM_WINDOWS)
    for(usize i = 0u; i < prefix.size(); ++i){
        char a = fullPath[i];
        char b = prefix[i];
        if(a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
        if(b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
        if(a != b)
            return false;
    }
    return true;
#else
    return fullPath.substr(0u, prefix.size()) == prefix;
#endif
}

b8 IsInternalIdentifier(const MStringView name)noexcept{
    if(name.empty())
        return true;
    if(name.starts_with("__") || name.starts_with("detail") || name.starts_with("Detail"))
        return true;
    if(name.ends_with("Detail"))
        return true;
    return name == "std";
}

b8 IsValidAdhdTypeIdentifier(const MStringView name)noexcept{
    if(name.empty())
        return false;
    const MChar first = name.front();
    if(!((first >= 'A' && first <= 'Z') || (first >= 'a' && first <= 'z') || first == '_'))
        return false;
    for(usize i = 1u; i < name.size(); ++i){
        const MChar ch = name[i];
        const b8 ok = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_';
        if(!ok)
            return false;
    }
    return true;
}

Expr* AllocateSignedIntegerLiteral(MetaArena& arena, const long long val){
    Array<char, 32> valBuf = {};
    const unsigned long long absVal = (val < 0)
        ? (0ull - static_cast<unsigned long long>(val))
        : static_cast<unsigned long long>(val);
    const int len = std::snprintf(valBuf.data(), valBuf.size(), "%llu", absVal);
    if(len <= 0 || static_cast<usize>(len) >= valBuf.size())
        return nullptr;

    Expr* lit = AllocateAstNode<Expr>(arena);
    lit->kind = ExprKind::IntegerLiteral;
    lit->text = InternArenaString(arena, MStringView(valBuf.data(), static_cast<usize>(len)));
    if(val >= 0)
        return lit;

    Expr* neg = AllocateAstNode<Expr>(arena);
    neg->kind = ExprKind::Unary;
    neg->op = TokenType::Minus;
    neg->rhs = lit;
    return neg;
}

MString ConsumeClangString(MetaArena& arena, CXString cxStr){
    MString result{arena};
    if(const char* cstr = ::clang_getCString(cxStr))
        result.assign(cstr);
    ::clang_disposeString(cxStr);
    return result;
}

void RewriteTemplateTypeRefName(TypeRef* const ref, const MStringView oldName, const MStringView newName)noexcept{
    if(!ref)
        return;
    if(ref->name == oldName)
        ref->name = newName;
    for(TypeArg& targ : ref->templateArgs)
        RewriteTemplateTypeRefName(targ.type, oldName, newName);
    RewriteTemplateTypeRefName(ref->funcReturnType, oldName, newName);
    for(FuncParamType& fp : ref->funcParams)
        RewriteTemplateTypeRefName(fp.type, oldName, newName);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


MStringView ImporterContext::mapOperatorToken(MStringView spelling)noexcept{
    if(!spelling.starts_with("operator"))
        return {};
    spelling.remove_prefix(8u);
    while(!spelling.empty() && spelling.front() == ' ')
        spelling.remove_prefix(1u);

    if(spelling == "[]")
        return "[]";
    if(spelling == "=" || spelling == "->" || spelling == "()" || spelling == "," || spelling == "new" || spelling == "delete")
        return {};

    static constexpr AStringView s_SupportedOps[] = {
        "+", "-", "*", "/", "%",
        "==", "!=", "<", "<=", ">", ">=",
        "&", "|", "^", "~", "!", "<<", ">>",
        "++", "--", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<=", ">>="
    };
    for(const AStringView op : s_SupportedOps){
        if(spelling == MStringView(op.data(), op.size()))
            return MStringView(op.data(), op.size());
    }
    return {};
}

b8 ImporterContext::hasDefaultInitializerToken(CXCursor parmCursor)const{
    b8 found = false;
    ::clang_visitChildren(parmCursor, [](CXCursor child, CXCursor, CXClientData clientData) -> CXChildVisitResult{
        auto* outFound = static_cast<b8*>(clientData);
        const CXCursorKind k = ::clang_getCursorKind(child);
        if(k >= CXCursor_FirstExpr && k <= CXCursor_LastExpr){
            *outFound = true;
            return CXChildVisit_Break;
        }
        return CXChildVisit_Continue;
    }, &found);
    return found;
}

TypeRef* ImporterContext::mapGenericContainerType(
    CXType type,
    CXType canonical,
    const MStringView rawSpelling,
    const MStringView sugaredLeaf,
    const ParamPassingMode::Enum refMode,
    const u32 depth,
    b8& outWasGenericContainer
){
    outWasGenericContainer = false;
    const GenericContainerMapEntry* genericEntry = FindGenericContainer(sugaredLeaf);
    if(!genericEntry)
        return nullptr;

    outWasGenericContainer = true;

    CXType specType = type;
    int numTArgs = ::clang_Type_getNumTemplateArguments(specType);
    if(numTArgs <= 0 && specType.kind == CXType_Elaborated){
        const CXType named = ::clang_Type_getNamedType(specType);
        if(named.kind != CXType_Invalid){
            specType = named;
            numTArgs = ::clang_Type_getNumTemplateArguments(specType);
        }
    }
    if(numTArgs <= 0 && canonical.kind != CXType_Invalid){
        const int canonicalNum = ::clang_Type_getNumTemplateArguments(canonical);
        if(canonicalNum > 0){
            const MString canonSpelling = ConsumeClangString(m_arena, ::clang_getTypeSpelling(canonical));
            const MStringView canonStripped = StripConstAndRefPrefix(MStringView(canonSpelling.data(), canonSpelling.size()));
            const MStringView canonLeaf = ExtractUnqualifiedLeaf(canonStripped);
            if(FindGenericContainer(canonLeaf) != nullptr || rawSpelling.find('<') != MStringView::npos){
                specType = canonical;
                numTArgs = canonicalNum;
            }
        }
    }
    if(numTArgs <= 0)
        return nullptr;

    const MStringView adhdName(genericEntry->adhdName.data(), genericEntry->adhdName.size());
    const b8 isBoundedOrBlockQueue = (adhdName == "Array"
        || adhdName == "FixedVector"
        || adhdName == "ParallelBlockQueue");

    TypeRef* ref = AllocateAstNode<TypeRef>(m_arena);
    ref->name = adhdName;
    ref->refMode = refMode;

    if(isBoundedOrBlockQueue){
        const CXType arg0 = ::clang_Type_getTemplateArgumentAsType(specType, 0u);
        if(arg0.kind == CXType_Invalid)
            return nullptr;
        TypeRef* mapped0 = mapType(arg0, false, depth + 1u);
        if(!mapped0)
            return nullptr;
        TypeArg t0;
        t0.type = mapped0;
        ref->templateArgs.push_back(t0);

        TypeArg t1;
        t1.constantText = (adhdName == "ParallelBlockQueue")
            ? MStringView("1")
            : ExtractPositiveConstantCapacityText(m_arena, rawSpelling);
        ref->templateArgs.push_back(t1);
        return ref;
    }

    const u32 requiredMinArgs = (adhdName == "Expected") ? 1u : genericEntry->maxArgs;
    if(static_cast<u32>(numTArgs) < requiredMinArgs)
        return nullptr;

    const u32 count = static_cast<u32>(numTArgs) < genericEntry->maxArgs ? static_cast<u32>(numTArgs) : genericEntry->maxArgs;
    for(u32 i = 0u; i < count; ++i){
        const CXType argType = ::clang_Type_getTemplateArgumentAsType(specType, i);
        if(argType.kind == CXType_Invalid)
            return nullptr;
        TypeRef* mappedArg = mapType(argType, false, depth + 1u);
        if(!mappedArg)
            return nullptr;
        TypeArg targ;
        targ.type = mappedArg;
        ref->templateArgs.push_back(targ);
    }

    if(ref->templateArgs.size() < requiredMinArgs)
        return nullptr;

    return ref;
}

TypeRef* ImporterContext::mapType(CXType type, const b8 isParamContext, const u32 depth){
    if(type.kind == CXType_Invalid || depth > 12u)
        return nullptr;

    const MString initialSpelling = ConsumeClangString(m_arena, ::clang_getTypeSpelling(type));
    if(MStringView(initialSpelling.data(), initialSpelling.size()).find("...") != MStringView::npos)
        return nullptr;

    ParamPassingMode::Enum refMode = ParamPassingMode::None;
    if(type.kind == CXType_LValueReference){
        const CXType pointee = ::clang_getNonReferenceType(type);
        const b8 isConstPointee = ::clang_isConstQualifiedType(pointee) != 0u;
        refMode = isConstPointee ? ParamPassingMode::In : ParamPassingMode::Mut;
        type = pointee;
    }
    else if(type.kind == CXType_RValueReference){
        const CXType pointee = ::clang_getNonReferenceType(type);
        refMode = isParamContext ? ParamPassingMode::In : ParamPassingMode::None;
        type = pointee;
    }

    const CXType canonical = ::clang_getCanonicalType(type);
    if(IsUnsupportedCppTypeKind(type.kind) || IsUnsupportedCppTypeKind(canonical.kind))
        return nullptr;

    const MString sugaredSpelling = ConsumeClangString(m_arena, ::clang_getTypeSpelling(type));
    const MStringView sugaredView(sugaredSpelling.data(), sugaredSpelling.size());
    const MStringView strippedSugared = StripConstAndRefPrefix(sugaredView);
    const MStringView sugaredLeaf = ExtractUnqualifiedLeaf(strippedSugared);

    // Preserve sugared NWB typedefs before canonicalization collapses fixed-width types.
    if(const MStringView direct = MapDirectTypeName(sugaredLeaf); !direct.empty()){
        if(direct == "void" && refMode != ParamPassingMode::None)
            return nullptr;
        TypeRef* ref = AllocateAstNode<TypeRef>(m_arena);
        ref->name = direct;
        ref->refMode = (IsScalarSpelling(direct) && refMode == ParamPassingMode::In) ? ParamPassingMode::None : refMode;
        return ref;
    }

    b8 wasGenericContainer = false;
    if(TypeRef* genericRef = mapGenericContainerType(type, canonical, sugaredView, sugaredLeaf, refMode, depth, wasGenericContainer))
        return genericRef;
    if(wasGenericContainer)
        return nullptr;

    if(const MStringView builtin = MapBuiltinScalarKind(canonical.kind); !builtin.empty()){
        if(builtin == "void" && refMode != ParamPassingMode::None)
            return nullptr;
        TypeRef* ref = AllocateAstNode<TypeRef>(m_arena);
        ref->name = builtin;
        ref->refMode = (refMode == ParamPassingMode::In) ? ParamPassingMode::None : refMode;
        return ref;
    }

    CXCursor declCursor = ::clang_getTypeDeclaration(type);
    if(::clang_Cursor_isNull(declCursor) || ::clang_getCursorKind(declCursor) == CXCursor_NoDeclFound)
        declCursor = ::clang_getTypeDeclaration(canonical);

    const CXCursorKind dk = ::clang_getCursorKind(declCursor);
    if(dk == CXCursor_StructDecl || dk == CXCursor_ClassDecl || dk == CXCursor_EnumDecl){
        if(sugaredView.find('<') != MStringView::npos)
            return nullptr;
        MStringView adhdName;
        MStringView dummyNs;
        MStringView dummyExport;
        b8 dummyDirect = false;
        b8 dummyFlag = false;
        if(FormatNormalizedAdhdDeclName(m_arena, declCursor, adhdName, dummyNs, dummyExport, dummyDirect, dummyFlag)){
            TypeRef* ref = AllocateAstNode<TypeRef>(m_arena);
            ref->name = adhdName;
            ref->refMode = (dk == CXCursor_EnumDecl && refMode == ParamPassingMode::In) ? ParamPassingMode::None : refMode;
            return ref;
        }
    }

    if(sugaredView.find('<') == MStringView::npos
        && IsValidAdhdTypeIdentifier(sugaredLeaf)
        && !IsInternalIdentifier(sugaredLeaf)
        && !FindGenericContainer(sugaredLeaf)
        && sugaredLeaf != "auto"
        && sugaredLeaf != "decltype"
        && sugaredLeaf != "nullptr_t"
        && KeywordSemantics::FindProhibitedTypeKeywordError(sugaredLeaf).empty()
    ){
        TypeRef* ref = AllocateAstNode<TypeRef>(m_arena);
        ref->name = InternArenaString(m_arena, sugaredLeaf);
        ref->refMode = refMode;
        return ref;
    }

    return nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

