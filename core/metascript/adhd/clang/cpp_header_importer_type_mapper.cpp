// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cpp_header_importer_internal.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CppHeaderImportDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_cpp_header_importer_type_mapper{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] MStringView StripConstAndRefPrefix(MStringView text)noexcept{
    for(;;){
        while(!text.empty() && text.front() == ' ')
            text.remove_prefix(1u);
        while(!text.empty() && text.back() == ' ')
            text.remove_suffix(1u);

        b8 stripped = false;
        for(const MStringView prefix : CppImporterSemantics::s_StripPrefixes){
            if(text.starts_with(prefix)){
                text.remove_prefix(prefix.size());
                stripped = true;
                break;
            }
        }
        if(stripped)
            continue;
        break;
    }
    while(!text.empty() && (text.back() == '&' || text.back() == '*' || text.back() == ' '))
        text.remove_suffix(1u);
    if(text.ends_with(CppImporterSemantics::s_TrailingConstSuffix)){
        text.remove_suffix(CppImporterSemantics::s_TrailingConstSuffix.size());
        while(!text.empty() && text.back() == ' ')
            text.remove_suffix(1u);
    }
    return text;
}

[[nodiscard]] MStringView ExtractUnqualifiedLeaf(const MStringView text)noexcept{
    const usize anglePos = text.find('<');
    const MStringView base = (anglePos == MStringView::npos) ? text : text.substr(0u, anglePos);
    const usize scopePos = base.rfind(ContractSemantics::s_CppScopeSeparator);
    return (scopePos == MStringView::npos) ? base : base.substr(scopePos + ContractSemantics::s_CppScopeSeparator.size());
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
            if(angleDepth == 0u)
                return (argIndex == targetIndex) ? TrimView(spelling.substr(argStart, i - argStart)) : MStringView();
            --angleDepth;
        }
        else if(ch == '('){
            ++parenDepth;
        }
        else if(ch == ')' && parenDepth > 0u){
            --parenDepth;
        }
        else if(ch == ',' && angleDepth == 0u && parenDepth == 0u){
            if(argIndex == targetIndex)
                return TrimView(spelling.substr(argStart, i - argStart));
            ++argIndex;
            argStart = i + 1u;
        }
    }
    return {};
}

[[nodiscard]] MStringView ExtractPositiveConstantCapacityText(MetaArena& arena, const MStringView spelling){
    MStringView arg = ExtractNthTemplateArgText(spelling, 1u);
    while(!arg.empty() && LiteralDecoder::IsIntegerLiteralSuffixChar(arg.back()))
        arg.remove_suffix(1u);
    if(arg.empty())
        return CppImporterSemantics::s_DefaultCapacityText;
    b8 nonZero = false;
    for(const MChar ch : arg){
        if(!IsAsciiDigit(ch))
            return CppImporterSemantics::s_DefaultCapacityText;
        if(ch != '0')
            nonZero = true;
    }
    return nonZero ? InternArenaString(arena, arg) : CppImporterSemantics::s_DefaultCapacityText;
}

[[nodiscard]] const BuiltinGenericTypeInfo* FindGenericContainer(const MStringView name)noexcept{
    if(const BuiltinGenericTypeInfo* info = FindBuiltinGenericType(name))
        return info;
    if(const MStringView aliasedName = CppImporterSemantics::ResolveContainerSuffixAlias(name); !aliasedName.empty())
        return FindBuiltinGenericType(aliasedName);
    return nullptr;
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

[[nodiscard]] b8 TypeRefContainsName(const TypeRef* const ref, const MStringView targetName)noexcept{
    if(!ref)
        return false;
    if(ref->name == targetName)
        return true;
    for(const TypeArg& targ : ref->templateArgs){
        if(TypeRefContainsName(targ.type, targetName))
            return true;
    }
    if(TypeRefContainsName(ref->funcReturnType, targetName))
        return true;
    for(const FuncParamType& fp : ref->funcParams){
        if(TypeRefContainsName(fp.type, targetName))
            return true;
    }
    return false;
}

void RewriteTemplateTypeRefNameInPlace(TypeRef* const ref, const MStringView oldName, const MStringView newName)noexcept{
    if(!ref)
        return;
    if(ref->name == oldName)
        ref->name = newName;
    for(TypeArg& targ : ref->templateArgs)
        RewriteTemplateTypeRefNameInPlace(targ.type, oldName, newName);
    RewriteTemplateTypeRefNameInPlace(ref->funcReturnType, oldName, newName);
    for(FuncParamType& fp : ref->funcParams)
        RewriteTemplateTypeRefNameInPlace(fp.type, oldName, newName);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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

Expr* AllocateSignedIntegerLiteral(MetaArena& arena, const long long val){
    const u64 absVal = (val < 0)
        ? (0ull - static_cast<u64>(val))
        : static_cast<u64>(val);
    Array<char, s_UsizeDecimalDigitCount> valBuf = {};
    const AStringView digits = FormatDecimal(static_cast<usize>(absVal), valBuf);

    Expr* lit = AllocateAstNode<Expr>(arena);
    lit->kind = ExprKind::IntegerLiteral;
    lit->text = InternArenaString(arena, digits);
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

TypeRef* CloneTypeRefDeep(MetaArena& arena, const TypeRef* const src){
    if(!src)
        return nullptr;
    TypeRef* dst = AllocateAstNode<TypeRef>(arena);
    dst->name = InternArenaString(arena, src->name);
    dst->refMode = src->refMode;
    dst->inferredCaptureBytes = src->inferredCaptureBytes;
    dst->span = src->span;
    dst->isFuncType = src->isFuncType;
    dst->isAuto = src->isAuto;
    if(!src->templateArgs.empty()){
        dst->templateArgs.reserve(src->templateArgs.size());
        for(const TypeArg& targ : src->templateArgs){
            TypeArg copyArg;
            copyArg.type = CloneTypeRefDeep(arena, targ.type);
            copyArg.constantText = InternArenaString(arena, targ.constantText);
            dst->templateArgs.push_back(copyArg);
        }
    }
    dst->funcReturnType = CloneTypeRefDeep(arena, src->funcReturnType);
    if(!src->funcParams.empty()){
        dst->funcParams.reserve(src->funcParams.size());
        for(const FuncParamType& fp : src->funcParams){
            FuncParamType copyFp;
            copyFp.mode = fp.mode;
            copyFp.type = CloneTypeRefDeep(arena, fp.type);
            dst->funcParams.push_back(copyFp);
        }
    }
    return dst;
}

TypeRef* RewriteTemplateTypeRefName(MetaArena& arena, TypeRef* const ref, const MStringView oldName, const MStringView newName){
    if(!__hidden_cpp_header_importer_type_mapper::TypeRefContainsName(ref, oldName))
        return ref;
    TypeRef* cloned = CloneTypeRefDeep(arena, ref);
    __hidden_cpp_header_importer_type_mapper::RewriteTemplateTypeRefNameInPlace(cloned, oldName, newName);
    return cloned;
}

MStringView MapRawBuiltinScalarKind(const CXTypeKind kind)noexcept{
    switch(kind){
    case CXType_Void: return BuiltinTypeSemantics::VoidTypeName();
    case CXType_Bool: return BuiltinTypeSemantics::BoolScalarTypeName();
    case CXType_Char_S:
    case CXType_SChar: return BuiltinTypeSemantics::FindIntegerScalarTypeName(1u, true);
    case CXType_Char_U:
    case CXType_UChar: return BuiltinTypeSemantics::FindIntegerScalarTypeName(1u, false);
    case CXType_Short: return BuiltinTypeSemantics::FindIntegerScalarTypeName(2u, true);
    case CXType_UShort: return BuiltinTypeSemantics::FindIntegerScalarTypeName(2u, false);
    case CXType_Int: return BuiltinTypeSemantics::FindIntegerScalarTypeName(4u, true);
    case CXType_UInt: return BuiltinTypeSemantics::FindIntegerScalarTypeName(4u, false);
#if defined(FOUNDATION_PLATFORM_WINDOWS)
    case CXType_Long: return BuiltinTypeSemantics::FindIntegerScalarTypeName(4u, true);
    case CXType_ULong: return BuiltinTypeSemantics::FindIntegerScalarTypeName(4u, false);
#else
    case CXType_Long: return BuiltinTypeSemantics::FindPointerSizedIntScalarTypeName(true);
    case CXType_ULong: return BuiltinTypeSemantics::FindPointerSizedIntScalarTypeName(false);
#endif
    case CXType_LongLong: return BuiltinTypeSemantics::FindIntegerScalarTypeName(8u, true);
    case CXType_ULongLong: return BuiltinTypeSemantics::FindIntegerScalarTypeName(8u, false);
    case CXType_Float: return BuiltinTypeSemantics::FindFloatScalarTypeName(4u);
    case CXType_Double: return BuiltinTypeSemantics::FindFloatScalarTypeName(8u);
    default: return {};
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
    const BuiltinGenericTypeInfo* genericInfo = __hidden_cpp_header_importer_type_mapper::FindGenericContainer(sugaredLeaf);
    if(!genericInfo)
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
            const MStringView canonStripped = __hidden_cpp_header_importer_type_mapper::StripConstAndRefPrefix(MStringView(canonSpelling.data(), canonSpelling.size()));
            const MStringView canonLeaf = __hidden_cpp_header_importer_type_mapper::ExtractUnqualifiedLeaf(canonStripped);
            if(__hidden_cpp_header_importer_type_mapper::FindGenericContainer(canonLeaf) != nullptr || rawSpelling.find('<') != MStringView::npos){
                specType = canonical;
                numTArgs = canonicalNum;
            }
        }
    }
    if(numTArgs <= 0)
        return nullptr;

    const MStringView adhdName = genericInfo->adhdName;

    TypeRef* ref = AllocateAstNode<TypeRef>(m_arena);
    ref->name = adhdName;
    ref->refMode = refMode;

    if(genericInfo->paramKind == GenericParamKind::TypeAndCount){
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
        t1.constantText = genericInfo->emitsFirstTypeArgOnlyInCpp
            ? CppImporterSemantics::s_DefaultCapacityText
            : __hidden_cpp_header_importer_type_mapper::ExtractPositiveConstantCapacityText(m_arena, rawSpelling);
        ref->templateArgs.push_back(t1);
        return ref;
    }

    const u32 maxArgs = (genericInfo->paramKind == GenericParamKind::TwoType || genericInfo->paramKind == GenericParamKind::OneOrTwoType) ? 2u : 1u;
    const u32 requiredMinArgs = (genericInfo->paramKind == GenericParamKind::OneOrTwoType) ? 1u : maxArgs;
    if(static_cast<u32>(numTArgs) < requiredMinArgs)
        return nullptr;

    const u32 count = static_cast<u32>(numTArgs) < maxArgs ? static_cast<u32>(numTArgs) : maxArgs;
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

    if(depth == 0u){
        const TypeCacheKey cacheKey{ static_cast<i32>(type.kind), static_cast<u8>(isParamContext ? 1u : 0u), type.data[0], type.data[1] };
        if(const auto it = m_typeCache.find(cacheKey); it != m_typeCache.end())
            return it->second;
        TypeRef* mapped = mapTypeUncached(type, isParamContext, depth);
        m_typeCache.emplace(cacheKey, mapped);
        return mapped;
    }

    return mapTypeUncached(type, isParamContext, depth);
}

TypeRef* ImporterContext::mapTypeUncached(CXType type, const b8 isParamContext, const u32 depth){
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

    if(__hidden_cpp_header_importer_type_mapper::IsUnsupportedCppTypeKind(type.kind))
        return nullptr;

    const auto allocateNonGenericLeafType = [&](const MStringView name, const b8 dropInRefMode) -> TypeRef*{
        if(BuiltinTypeSemantics::IsVoidTypeName(name) && refMode != ParamPassingMode::None)
            return nullptr;
        TypeRef* ref = AllocateAstNode<TypeRef>(m_arena);
        ref->name = name;
        ref->refMode = (dropInRefMode && refMode == ParamPassingMode::In) ? ParamPassingMode::None : refMode;
        return ref;
    };

    if(const MStringView rawBuiltin = MapRawBuiltinScalarKind(type.kind); !rawBuiltin.empty())
        return allocateNonGenericLeafType(rawBuiltin, true);

    const CXType canonical = ::clang_getCanonicalType(type);
    if(__hidden_cpp_header_importer_type_mapper::IsUnsupportedCppTypeKind(canonical.kind))
        return nullptr;

    const MString sugaredSpelling = ConsumeClangString(m_arena, ::clang_getTypeSpelling(type));
    const MStringView sugaredView(sugaredSpelling.data(), sugaredSpelling.size());
    if(sugaredView.find(CppImporterSemantics::s_VariadicPackToken) != MStringView::npos)
        return nullptr;

    const MStringView strippedSugared = __hidden_cpp_header_importer_type_mapper::StripConstAndRefPrefix(sugaredView);
    const MStringView sugaredLeaf = __hidden_cpp_header_importer_type_mapper::ExtractUnqualifiedLeaf(strippedSugared);

    if(const MStringView direct = CppImporterSemantics::MapDirectTypeName(sugaredLeaf); !direct.empty())
        return allocateNonGenericLeafType(direct, BuiltinTypeSemantics::IsScalarType(direct));

    b8 wasGenericContainer = false;
    if(TypeRef* genericRef = mapGenericContainerType(type, canonical, sugaredView, sugaredLeaf, refMode, depth, wasGenericContainer))
        return genericRef;
    if(wasGenericContainer)
        return nullptr;

    if(const MStringView builtin = MapRawBuiltinScalarKind(canonical.kind); !builtin.empty())
        return allocateNonGenericLeafType(builtin, true);

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
        if(formatDeclName(declCursor, adhdName, dummyNs, dummyExport, dummyDirect, dummyFlag)){
            TypeRef* ref = AllocateAstNode<TypeRef>(m_arena);
            ref->name = adhdName;
            ref->refMode = (dk == CXCursor_EnumDecl && refMode == ParamPassingMode::In) ? ParamPassingMode::None : refMode;
            return ref;
        }
    }

    if(sugaredView.find('<') == MStringView::npos
        && IsAsciiIdentifier(sugaredLeaf)
        && !CppImporterSemantics::IsInternalIdentifierName(sugaredLeaf)
        && !__hidden_cpp_header_importer_type_mapper::FindGenericContainer(sugaredLeaf)
        && !CppImporterSemantics::IsUnmappedCppKeyword(sugaredLeaf)
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

