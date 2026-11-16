// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cpp_header_importer_internal.h"

#include <global/array.h>
#include <global/environment.h>
#include <global/filesystem/executable_paths.h>
#include <global/filesystem/operations.h>
#include <global/numeric_text.h>
#include <global/text_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace CppHeaderImportDetail;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_cpp_header_importer{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] MStringView ExtractClangMajorVersion(const MStringView versionText)noexcept{
    const MStringView marker = CppImporterSemantics::s_ClangVersionMarker;
    usize start = versionText.find(marker);
    start = (start == MStringView::npos) ? 0u : (start + marker.size());

    while(start < versionText.size() && !IsAsciiDigit(versionText[start]))
        ++start;

    usize end = start;
    while(end < versionText.size() && IsAsciiDigit(versionText[end]))
        ++end;

    return versionText.substr(start, end - start);
}

[[nodiscard]] b8 TryResourceDirCandidate(MetaArena& arena, const ::Path<MetaArena>& candidate, MString& outDir){
    const auto exists = IsDirectory(candidate);
    if(!exists || !*exists)
        return false;

    const AString<MetaArena> utf8 = PathToString<char>(arena, candidate);
    outDir.assign(utf8.data(), utf8.size());
    return true;
}

[[nodiscard]] MString FindClangResourceDirectory(MetaArena& arena){
    MString result{arena};
    const MString versionText = ConsumeClangString(arena, ::clang_getClangVersion());
    const MStringView major = ExtractClangMajorVersion(MStringView(versionText.data(), versionText.size()));
    if(major.empty())
        return result;

    auto probeBasePath = [&](const AStringView basePath) -> b8{
        if(basePath.empty())
            return false;

        const TString<MetaArena> baseNative = ConvertString(arena, basePath);
        const ::Path<MetaArena> libraryPath(arena, TStringView(baseNative.data(), baseNative.size()));
        const ::Path<MetaArena> libDir = libraryPath.parentPath();
        if(libDir.empty())
            return false;

        if(TryResourceDirCandidate(arena, libDir / CppImporterSemantics::s_ClangSubdir / major, result))
            return true;

        const ::Path<MetaArena> prefixDir = libDir.parentPath();
        if(!prefixDir.empty()){
            if(TryResourceDirCandidate(arena, prefixDir / CppImporterSemantics::s_LibSubdir / CppImporterSemantics::s_ClangSubdir / major, result))
                return true;
            if(TryResourceDirCandidate(arena, prefixDir / CppImporterSemantics::s_Lib64Subdir / CppImporterSemantics::s_ClangSubdir / major, result))
                return true;
        }
        return false;
    };

#if defined(NWB_LIBCLANG_DEFAULT_RESOURCE_DIR)
    {
        const TString<MetaArena> nativeDefaultRes = ConvertString(arena, AStringView(NWB_LIBCLANG_DEFAULT_RESOURCE_DIR));
        const ::Path<MetaArena> candidate(arena, TStringView(nativeDefaultRes.data(), nativeDefaultRes.size()));
        const auto includeDirExists = IsDirectory(candidate / CppImporterSemantics::s_IncludeSubdir);
        if(includeDirExists && *includeDirExists && TryResourceDirCandidate(arena, candidate, result))
            return result;
    }
#endif

#if defined(NWB_LIBCLANG_DEFAULT_RUNTIME_PATH)
    if(probeBasePath(NWB_LIBCLANG_DEFAULT_RUNTIME_PATH))
        return result;
#endif

    const auto exeDir = ExecutablePaths(arena).directory();
    if(exeDir){
        const AString<MetaArena> exeDirUtf8 = PathToString<char>(arena, *exeDir);
        if(probeBasePath(AStringView(exeDirUtf8.data(), exeDirUtf8.size())))
            return result;
    }

    if(const auto envResource = ReadEnvironmentVariable(arena, CppImporterSemantics::s_ClangResourceEnvVar)){
        if(*envResource && !(*envResource)->empty()){
            const TString<MetaArena> nativeRes = ConvertString(arena, AStringView((*envResource)->data(), (*envResource)->size()));
            const ::Path<MetaArena> candidate(arena, TStringView(nativeRes.data(), nativeRes.size()));
            if(TryResourceDirCandidate(arena, candidate, result))
                return result;
        }
    }

    return result;
}

void AppendFormattedDiagnostic(MetaArena& arena, CXDiagnostic diag, const SourceSpan span, MVector<ParseError>& outErrors){
    const unsigned opts = CXDiagnostic_DisplaySourceLocation | CXDiagnostic_DisplayColumn | CXDiagnostic_DisplayCategoryName;
    const MString msg = ConsumeClangString(arena, ::clang_formatDiagnostic(diag, opts));
    ReportAdhdDiagnostic(arena, outErrors, span, MStringView(msg.data(), msg.size()));
}

void AddExistingUnique(MVector<MString>& list, const MString& item){
    for(const MString& existing : list){
        if(existing == item)
            return;
    }
    list.push_back(item);
}

void BuildClangArguments(
    MetaArena& arena,
    const CppHeaderImportOptions& options,
    MVector<MString>& argStorage,
    MVector<MString>& normalizedIncludeRoots,
    MVector<const char*>& outArgPtrs
){
    auto pushArg = [&](const MStringView text){
        if(text.empty())
            return;
        argStorage.push_back(MString(text.data(), text.size(), arena));
    };

#if defined(FOUNDATION_PLATFORM_WINDOWS)
#if defined(FOUNDATION_ARCH_ARM64) || defined(_M_ARM64) || defined(__aarch64__)
    pushArg(CppImporterSemantics::s_TargetArm64WindowsMsvc);
#elif defined(FOUNDATION_ARCH_X86_64) || defined(_M_X64) || defined(__x86_64__)
    pushArg(CppImporterSemantics::s_TargetX64WindowsMsvc);
#endif
    for(const MStringView arg : CppImporterSemantics::s_ClangWindowsArgs)
        pushArg(arg);
#endif

    for(const MStringView arg : CppImporterSemantics::s_ClangDefaultArgs)
        pushArg(arg);

    for(const CppClangConfigGroupEntry& group : CppImporterSemantics::s_ClangConfigGroups){
        b8 hasDef = false;
        for(const MString& def : options.defines){
            if(def.starts_with(group.optPrefix) || def.starts_with(group.dbgPrefix) || def.starts_with(group.finalPrefix)){
                hasDef = true;
                break;
            }
        }
        if(!hasDef)
            pushArg(group.defaultFlag);
    }

    const MString resourceDir = FindClangResourceDirectory(arena);
    if(!resourceDir.empty()){
        pushArg(CppImporterSemantics::s_ResourceDirFlag);
        pushArg(MStringView(resourceDir.data(), resourceDir.size()));
    }

    for(const MString& def : options.defines){
        if(def.empty())
            continue;
        MString flag(CppImporterSemantics::s_DefineFlagPrefix.data(), CppImporterSemantics::s_DefineFlagPrefix.size(), arena);
        flag.append(def.data(), def.size());
        argStorage.push_back(Move(flag));
    }

    auto addIncludeDirFlag = [&](const MStringView rawDir, const b8 isSystem){
        if(rawDir.empty())
            return;
        const MString norm = NormalizeSlashes(arena, rawDir);
        const MStringView normView(norm.data(), norm.size());
        const TString<MetaArena> nativeDir = ConvertString(arena, normView);
        const ::Path<MetaArena> dirPath(arena, TStringView(nativeDir.data(), nativeDir.size()));
        const auto exists = IsDirectory(dirPath);
        if(!exists || !*exists)
            return;

        AddExistingUnique(normalizedIncludeRoots, norm);
        const MStringView flagPrefix = isSystem ? CppImporterSemantics::s_SystemIncludeFlagPrefix : CppImporterSemantics::s_IncludeFlagPrefix;
        MString flag(flagPrefix.data(), flagPrefix.size(), arena);
        flag.append(norm.data(), norm.size());
        argStorage.push_back(Move(flag));
    };

    for(const MString& incDir : options.includeDirectories)
        addIncludeDirFlag(MStringView(incDir.data(), incDir.size()), false);

    if(options.enableAutoProjectIncludes){
#if defined(NWB_PROJECT_SOURCE_DIR)
        const MStringView projectRoot = NWB_PROJECT_SOURCE_DIR;
        addIncludeDirFlag(projectRoot, false);

        for(const MStringView rel : CppImporterSemantics::s_ThirdPartyIncludeSubdirs){
            const MString full = JoinWithSingleSlash(arena, projectRoot, rel);
            addIncludeDirFlag(MStringView(full.data(), full.size()), true);
        }
#endif
    }

    for(const MString& extraArg : options.compilerArgs){
        if(!extraArg.empty())
            pushArg(MStringView(extraArg.data(), extraArg.size()));
    }

    outArgPtrs.reserve(argStorage.size());
    for(const MString& s : argStorage)
        outArgPtrs.push_back(s.c_str());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CppHeaderImportDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void ImporterContext::run(CXTranslationUnit tu, const Span<const MString> normalizedIncludeRoots){
    for(const MString& root : normalizedIncludeRoots)
        m_includeRoots.push_back(root);

    for(ClassDecl* cls : m_unit.classes){
        if(!cls)
            continue;
        if(!cls->name.empty())
            m_classByName.emplace(MString(cls->name.data(), cls->name.size(), m_arena), cls);
        if(!cls->cppExportName.empty())
            m_classByExportName.emplace(MString(cls->cppExportName.data(), cls->cppExportName.size(), m_arena), cls);
    }
    for(EnumDecl* e : m_unit.enums){
        if(!e)
            continue;
        if(!e->name.empty())
            m_enumByName.emplace(MString(e->name.data(), e->name.size(), m_arena), e);
        if(!e->cppExportName.empty())
            m_enumByExportName.emplace(MString(e->cppExportName.data(), e->cppExportName.size(), m_arena), e);
    }
    for(TypeAliasDecl* a : m_unit.typeAliases){
        if(a && !a->name.empty())
            m_aliasByName.emplace(MString(a->name.data(), a->name.size(), m_arena), a);
    }
    for(Stmt* g : m_unit.globalConstants){
        if(!g)
            continue;
        if(!g->name.empty())
            m_constantByName.emplace(MString(g->name.data(), g->name.size(), m_arena), g);
        if(!g->cppExportName.empty())
            m_seenConstants.emplace(MString(g->cppExportName.data(), g->cppExportName.size(), m_arena), 1u);
    }
    for(const MethodDecl* f : m_unit.freeFunctions){
        if(!f || f->name.empty())
            continue;
        const MString sigKey = buildFreeFunctionSigKey(*f);
        m_seenFreeFunctions.emplace(sigKey, 1u);
    }

    const CXCursor rootCursor = ::clang_getTranslationUnitCursor(tu);
    visitScopeCursor(rootCursor);
}

void ImporterContext::visitChildrenList(CXCursor parent, MVector<CXCursor>& outChildren)const{
    ::clang_visitChildren(parent, [](CXCursor c, CXCursor, CXClientData clientData) -> CXChildVisitResult{
        auto* out = static_cast<MVector<CXCursor>*>(clientData);
        out->push_back(c);
        return CXChildVisit_Continue;
    }, &outChildren);
}

MStringView ImporterContext::resolveRequiredHeader(CXCursor cursor){
    const CXSourceLocation loc = ::clang_getCursorLocation(cursor);
    CXFile file = nullptr;
    ::clang_getSpellingLocation(loc, &file, nullptr, nullptr, nullptr);
    if(!file)
        return {};

    const MString rawFileName = ConsumeClangString(m_arena, ::clang_getFileName(file));
    if(rawFileName.empty())
        return {};

    const MString normalized = NormalizeSlashes(m_arena, MStringView(rawFileName.data(), rawFileName.size()));
    const MStringView key(normalized.data(), normalized.size());
    if(const auto it = m_headerCache.find(key); it != m_headerCache.end())
        return it->second;

    MStringView bestRelative = key;
    for(const MString& root : m_includeRoots){
        const MStringView rootView(root.data(), root.size());
        if(StartsWithCaseInsensitivePrefix(key, rootView)){
            MStringView sub = key.substr(rootView.size());
            if(sub.starts_with("/"))
                sub.remove_prefix(1u);
            if(sub.size() < bestRelative.size())
                bestRelative = sub;
        }
    }

    const MStringView interned = InternAngleBracketHeader(m_arena, bestRelative);
    m_headerCache.emplace(normalized, interned);
    return interned;
}

b8 ImporterContext::evaluateIntegralConstant(CXCursor cursor, MStringView& outText){
    const CXType canonical = ::clang_getCanonicalType(::clang_getCursorType(cursor));
    switch(canonical.kind){
    case CXType_Bool:
    case CXType_Char_U:
    case CXType_UChar:
    case CXType_Char16:
    case CXType_Char32:
    case CXType_UShort:
    case CXType_UInt:
    case CXType_ULong:
    case CXType_ULongLong:
    case CXType_UInt128:
    case CXType_Char_S:
    case CXType_SChar:
    case CXType_WChar:
    case CXType_Short:
    case CXType_Int:
    case CXType_Long:
    case CXType_LongLong:
    case CXType_Int128:
    case CXType_Float:
    case CXType_Double:
    case CXType_LongDouble:
    case CXType_Float128:
    case CXType_Half:
    case CXType_BFloat16:
    case CXType_Float16:
    case CXType_Enum:
        break;
    default:
        return false;
    }

    CXEvalResult eval = ::clang_Cursor_Evaluate(cursor);
    if(!eval)
        return false;

    const CXEvalResultKind kind = ::clang_EvalResult_getKind(eval);
    b8 ok = false;
    Array<char, 64> buf = {};

    if(kind == CXEval_Int){
        if(::clang_EvalResult_isUnsignedInt(eval)){
            const u64 val = static_cast<u64>(::clang_EvalResult_getAsUnsigned(eval));
            const AStringView digits = TextDetail::FormatDecimalBuffer<u64>(val, buf.data());
            buf[digits.size()] = 'u';
            outText = InternArenaString(m_arena, MStringView(buf.data(), digits.size() + 1u));
            ok = true;
        }
        else{
            if(const auto formatted = FormatI64(static_cast<i64>(::clang_EvalResult_getAsLongLong(eval)), Span<char>(buf.data(), buf.size()))){
                outText = InternArenaString(m_arena, *formatted);
                ok = true;
            }
        }
    }
    else if(kind == CXEval_Float){
        if(const auto formatted = FormatF64(::clang_EvalResult_getAsDouble(eval), Span<char>(buf.data(), buf.size()))){
            outText = InternArenaString(m_arena, *formatted);
            ok = true;
        }
    }

    ::clang_EvalResult_dispose(eval);
    return ok;
}

void ImporterContext::extractEnum(CXCursor enumCursor){
    if(!::clang_isCursorDefinition(enumCursor))
        return;

    MStringView adhdName;
    MStringView cppNs;
    MStringView cppExportName;
    b8 isDirect = false;
    b8 isFlag = false;
    if(!FormatNormalizedAdhdDeclName(m_arena, enumCursor, adhdName, cppNs, cppExportName, isDirect, isFlag))
        return;

    EnumDecl* decl = findExistingDeclForMerge(m_enumByExportName, m_enumByName, adhdName, cppExportName);
    const b8 isNew = (decl == nullptr);
    if(isNew){
        decl = AllocateAstNode<EnumDecl>(m_arena);
        decl->name = adhdName;
    }

    decl->cppNamespace = cppNs;
    decl->cppExportName = cppExportName;
    decl->requiredHeader = resolveRequiredHeader(enumCursor);
    decl->isDirectCppEnum = isDirect;
    decl->isFlag = isFlag;
    decl->isImported = true;
    decl->isExtern = true;

    const CXType intType = ::clang_getEnumDeclIntegerType(enumCursor);
    if(TypeRef* sizeRef = mapType(intType, false)){
        decl->sizeTypeName = sizeRef->name;
        const long long sz = ::clang_Type_getSizeOf(intType);
        const long long al = ::clang_Type_getAlignOf(intType);
        if(sz > 0) decl->underlyingSize = static_cast<usize>(sz);
        if(al > 0) decl->underlyingAlign = static_cast<usize>(al);
    }

    if(isNew){
        m_unit.enums.push_back(decl);
        m_enumByName.emplace(MString(decl->name.data(), decl->name.size(), m_arena), decl);
        m_enumByExportName.emplace(MString(decl->cppExportName.data(), decl->cppExportName.size(), m_arena), decl);
    }

    MVector<CXCursor> children(m_arena);
    visitChildrenList(enumCursor, children);
    for(const CXCursor child : children){
        if(::clang_getCursorKind(child) != CXCursor_EnumConstantDecl)
            continue;
        const MString entryNameStr = ConsumeClangString(m_arena, ::clang_getCursorSpelling(child));
        if(entryNameStr.empty())
            continue;
        const MStringView entryName = InternArenaString(m_arena, entryNameStr);

        b8 duplicate = false;
        for(const EnumEntry& e : decl->entries){
            if(e.name == entryName){
                duplicate = true;
                break;
            }
        }
        if(duplicate)
            continue;

        EnumEntry entry;
        entry.name = entryName;
        const long long signedVal = ::clang_getEnumConstantDeclValue(child);
        entry.valueExpr = AllocateSignedIntegerLiteral(m_arena, signedVal);
        decl->entries.push_back(entry);
    }
}

void ImporterContext::extractTypeAlias(CXCursor aliasCursor){
    MStringView adhdName;
    MStringView cppNs;
    MStringView cppExportName;
    b8 dummyDirect = false;
    b8 dummyFlag = false;
    if(!FormatNormalizedAdhdDeclName(m_arena, aliasCursor, adhdName, cppNs, cppExportName, dummyDirect, dummyFlag))
        return;

    if(m_aliasByName.find(adhdName) != m_aliasByName.end() || findExistingDeclForMerge(m_classByExportName, m_classByName, adhdName, {}) != nullptr)
        return;

    const CXType underlying = ::clang_getTypedefDeclUnderlyingType(aliasCursor);
    TypeRef* mapped = mapType(underlying, false);
    if(!mapped || mapped->name.empty() || BuiltinTypeSemantics::IsVoidTypeName(mapped->name) || mapped->name == adhdName || mapped->refMode != ParamPassingMode::None)
        return;

    TypeAliasDecl* decl = AllocateAstNode<TypeAliasDecl>(m_arena);
    decl->name = adhdName;
    decl->cppNamespace = cppNs;
    decl->cppExportName = cppExportName;
    decl->requiredHeader = resolveRequiredHeader(aliasCursor);
    decl->aliasedType = mapped;
    decl->isImported = true;
    decl->isExtern = true;
    m_unit.typeAliases.push_back(decl);
    m_aliasByName.emplace(MString(adhdName.data(), adhdName.size(), m_arena), decl);
}

void ImporterContext::extractGlobalConstant(CXCursor varCursor){
    const CXType vtype = ::clang_getCursorType(varCursor);
    if(!::clang_isConstQualifiedType(vtype))
        return;

    MStringView adhdName;
    MStringView cppNs;
    MStringView cppExportName;
    b8 dummyDirect = false;
    b8 dummyFlag = false;
    if(!FormatNormalizedAdhdDeclName(m_arena, varCursor, adhdName, cppNs, cppExportName, dummyDirect, dummyFlag))
        return;

    if(m_seenConstants.find(cppExportName) != m_seenConstants.end())
        return;
    if(m_constantByName.find(adhdName) != m_constantByName.end())
        return;

    TypeRef* mapped = mapType(vtype, false);
    if(!mapped || mapped->refMode != ParamPassingMode::None || BuiltinTypeSemantics::IsVoidTypeName(mapped->name))
        return;

    const b8 isStringViewConstant = CppImporterSemantics::IsStringViewConstantTypeName(mapped->name);
    MStringView evalText;
    const b8 hasEval = !isStringViewConstant && evaluateIntegralConstant(varCursor, evalText);
    if(!hasEval && !isStringViewConstant)
        return;

    Stmt* stmt = AllocateAstNode<Stmt>(m_arena);
    stmt->kind = StmtKind::VarDecl;
    stmt->name = adhdName;
    stmt->varType = mapped;
    stmt->cppNamespace = cppNs;
    stmt->cppExportName = cppExportName;
    stmt->requiredHeader = resolveRequiredHeader(varCursor);
    stmt->isConst = true;
    stmt->isImported = true;
    stmt->isExtern = true;

    m_seenConstants.emplace(MString(cppExportName.data(), cppExportName.size(), m_arena), 1u);
    m_constantByName.emplace(MString(adhdName.data(), adhdName.size(), m_arena), stmt);
    m_unit.globalConstants.push_back(stmt);
}

MString ImporterContext::buildFreeFunctionSigKey(const MethodDecl& decl)const{
    MString sigKey(decl.name.data(), decl.name.size(), m_arena);
    sigKey.push_back('<');
    AppendUnsignedDecimal(sigKey, decl.templateParams.size());
    sigKey.push_back('>');
    sigKey.push_back('(');
    for(const ParamDecl& p : decl.params){
        if(p.mode == ParamPassingMode::Mut || (p.type && p.type->refMode == ParamPassingMode::Mut)){
            const MStringView mutKw = KeywordSemantics::ParamPassingModeKeyword(ParamPassingMode::Mut);
            sigKey.append(mutKw.data(), mutKw.size());
            sigKey.push_back(' ');
        }
        if(p.type)
            AppendTypeRefExternText(sigKey, *p.type, false);
        sigKey.push_back(',');
    }
    sigKey.push_back(')');
    return sigKey;
}

void ImporterContext::extractFreeFunction(CXCursor fnCursor, const b8 isTemplate){
    MStringView adhdName;
    MStringView cppNs;
    MStringView cppExportName;
    b8 dummyDirect = false;
    b8 dummyFlag = false;
    if(!FormatNormalizedAdhdDeclName(m_arena, fnCursor, adhdName, cppNs, cppExportName, dummyDirect, dummyFlag))
        return;

    MethodDecl* decl = buildMethodDecl(fnCursor, {}, isTemplate);
    if(!decl)
        return;
    decl->name = adhdName;
    decl->cppNamespace = cppNs;
    decl->cppExportName = cppExportName;

    const MString sigKey = buildFreeFunctionSigKey(*decl);
    if(m_seenFreeFunctions.find(MStringView(sigKey.data(), sigKey.size())) != m_seenFreeFunctions.end())
        return;

    m_seenFreeFunctions.emplace(sigKey, 1u);
    m_unit.freeFunctions.push_back(decl);
}

void ImporterContext::visitScopeCursor(CXCursor parentCursor){
    MVector<CXCursor> children(m_arena);
    visitChildrenList(parentCursor, children);

    for(const CXCursor c : children){
        const CXSourceLocation loc = ::clang_getCursorLocation(c);
        if(::clang_Location_isInSystemHeader(loc))
            continue;

        const CXAvailabilityKind avail = ::clang_getCursorAvailability(c);
        if(avail == CXAvailability_NotAvailable || avail == CXAvailability_NotAccessible)
            continue;

        const CXCursorKind kind = ::clang_getCursorKind(c);
        if(kind == CXCursor_Namespace){
            const MString nsStr = ConsumeClangString(m_arena, ::clang_getCursorSpelling(c));
            const MStringView nsView(nsStr.data(), nsStr.size());
            if(CppImporterSemantics::IsInternalIdentifierName(nsView))
                continue;
            visitScopeCursor(c);
            continue;
        }
        if(kind == CXCursor_LinkageSpec || kind == CXCursor_UnexposedDecl){
            visitScopeCursor(c);
            continue;
        }
        if(kind == CXCursor_StructDecl || kind == CXCursor_ClassDecl){
            extractClass(c);
            continue;
        }
        if(kind == CXCursor_EnumDecl){
            extractEnum(c);
            continue;
        }
        if(kind == CXCursor_TypeAliasDecl || kind == CXCursor_TypedefDecl){
            extractTypeAlias(c);
            continue;
        }
        if(kind == CXCursor_VarDecl){
            extractGlobalConstant(c);
            continue;
        }
        if(kind == CXCursor_FunctionDecl || kind == CXCursor_FunctionTemplate){
            if(kind == CXCursor_FunctionTemplate && ::clang_getTemplateCursorKind(c) != CXCursor_FunctionDecl)
                continue;
            extractFreeFunction(c, kind == CXCursor_FunctionTemplate);
            continue;
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 CppHeaderImporter::ImportHeaders(
    const CppHeaderImportOptions& options,
    MetaArena& arena,
    TranslationUnit& outUnit,
    MVector<ParseError>& outErrors
){
    if(options.headers.empty())
        return true;

    MString syntheticSource(arena);
    for(const ImportedCppHeader& hdr : options.headers){
        const MStringView inner = StripHeaderDelimiters(hdr.headerPath);
        if(inner.empty())
            continue;
        syntheticSource.append("#include ");
        syntheticSource.push_back(hdr.isSystemHeader ? '<' : '"');
        syntheticSource.append(inner.data(), inner.size());
        syntheticSource.push_back(hdr.isSystemHeader ? '>' : '"');
        syntheticSource.push_back('\n');
    }

    if(syntheticSource.empty())
        return true;

    const MStringView virtualTuName = CppImporterSemantics::s_VirtualTuFilename;
    MString virtualTuPath(virtualTuName.data(), virtualTuName.size(), arena);
#if defined(NWB_PROJECT_SOURCE_DIR)
    {
        const MStringView projectRoot = NWB_PROJECT_SOURCE_DIR;
        if(!projectRoot.empty())
            virtualTuPath = JoinWithSingleSlash(arena, projectRoot, virtualTuName);
    }
#endif

    MVector<MString> argStorage(arena);
    MVector<MString> normalizedRoots(arena);
    MVector<const char*> argPtrs(arena);
    __hidden_cpp_header_importer::BuildClangArguments(arena, options, argStorage, normalizedRoots, argPtrs);

    CXUnsavedFile unsavedFile{};
    unsavedFile.Filename = virtualTuPath.c_str();
    unsavedFile.Contents = syntheticSource.c_str();
    unsavedFile.Length = static_cast<unsigned long>(syntheticSource.size());

    const CXIndex index = ::clang_createIndex(0, 0);
    if(!index){
        ReportAdhdDiagnostic(arena, outErrors, options.headers[0].span, "Failed to create libclang index");
        return false;
    }

    CXTranslationUnit tu = nullptr;
    const CXErrorCode parseResult = ::clang_parseTranslationUnit2(
        index,
        virtualTuPath.c_str(),
        argPtrs.data(),
        static_cast<int>(argPtrs.size()),
        &unsavedFile,
        1u,
        CXTranslationUnit_SkipFunctionBodies,
        &tu
    );

    if(parseResult != CXError_Success || !tu){
        ::clang_disposeIndex(index);
        ReportAdhdDiagnostic(arena, outErrors, options.headers[0].span, "libclang failed to parse imported C++ headers");
        return false;
    }

    b8 hasFatalErrors = false;
    const unsigned diagCount = ::clang_getNumDiagnostics(tu);
    for(unsigned i = 0u; i < diagCount; ++i){
        const CXDiagnostic diag = ::clang_getDiagnostic(tu, i);
        const CXDiagnosticSeverity sev = ::clang_getDiagnosticSeverity(diag);
        if(sev >= CXDiagnostic_Error){
            __hidden_cpp_header_importer::AppendFormattedDiagnostic(arena, diag, options.headers[0].span, outErrors);
            hasFatalErrors = true;
        }
        ::clang_disposeDiagnostic(diag);
    }

    struct InclusionCollector{
        MetaArena* arena;
        TranslationUnit* unit;
    } incCollector{ &arena, &outUnit };

    ::clang_getInclusions(tu, [](CXFile includedFile, CXSourceLocation*, unsigned, CXClientData clientData){
        auto* ctx = static_cast<InclusionCollector*>(clientData);
        if(!includedFile)
            return;
        const MString fileName = ConsumeClangString(*ctx->arena, ::clang_getFileName(includedFile));
        const MString normalized = NormalizeSlashes(*ctx->arena, MStringView(fileName.data(), fileName.size()));
        const MStringView normView(normalized.data(), normalized.size());
        if(normView.empty() || normView.ends_with(CppImporterSemantics::s_VirtualTuFilename))
            return;
        __hidden_cpp_header_importer::AddExistingUnique(ctx->unit->resolvedIncludedFiles, normalized);
    }, &incCollector);

    if(hasFatalErrors){
        ::clang_disposeTranslationUnit(tu);
        ::clang_disposeIndex(index);
        return false;
    }

    ImporterContext importer(arena, outUnit);
    importer.run(tu, Span<const MString>(normalizedRoots.data(), normalizedRoots.size()));

    ::clang_disposeTranslationUnit(tu);
    ::clang_disposeIndex(index);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

