// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cpp_header_importer_internal.h"

#include <core/metascript/arena_names.h>
#include <global/array.h>
#include <global/environment.h>
#include <global/filesystem/executable_paths.h>
#include <global/filesystem/operations.h>
#include <global/numeric_text.h>
#include <global/sync.h>
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

[[nodiscard]] MString FindClangResourceDirectoryUncached(MetaArena& arena){
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

[[nodiscard]] MStringView GetCachedClangResourceDirectory(){
    struct ResourceDirCache{
        MetaArena arena;
        MString dir;

        ResourceDirCache()
            : arena(MetascriptArenaScope::s_ParserScratch)
            , dir(FindClangResourceDirectoryUncached(arena))
        {}
    };

    static const ResourceDirCache s_Cache;
    return MStringView(s_Cache.dir.data(), s_Cache.dir.size());
}

void AppendFormattedDiagnostic(MetaArena& arena, CXDiagnostic diag, const SourceSpan span, MVector<ParseError>& outErrors){
    const unsigned opts = CXDiagnostic_DisplaySourceLocation | CXDiagnostic_DisplayColumn | CXDiagnostic_DisplayCategoryName;
    const MString msg = ConsumeClangString(arena, ::clang_formatDiagnostic(diag, opts));
    ReportAdhdDiagnostic(arena, outErrors, span, MStringView(msg.data(), msg.size()));
}

void AddExistingUnique(MVector<MString>& list, MStringMap<u8>& seen, const MString& item){
    const MStringView view(item.data(), item.size());
    if(seen.find(view) != seen.end())
        return;
    seen.emplace(item, 1u);
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
#if defined(_M_ARM64) || defined(__aarch64__)
    pushArg(CppImporterSemantics::s_TargetArm64WindowsMsvc);
#elif defined(_M_X64) || defined(__x86_64__)
    pushArg(CppImporterSemantics::s_TargetX64WindowsMsvc);
#endif
    for(const MStringView arg : CppImporterSemantics::s_ClangWindowsArgs)
        pushArg(arg);
#elif defined(FOUNDATION_PLATFORM_LINUX)
#if defined(__aarch64__)
    pushArg(CppImporterSemantics::s_TargetArm64LinuxGnu);
#elif defined(__x86_64__)
    pushArg(CppImporterSemantics::s_TargetX64LinuxGnu);
#endif
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

    const MStringView resourceDir = GetCachedClangResourceDirectory();
    if(!resourceDir.empty()){
        pushArg(CppImporterSemantics::s_ResourceDirFlag);
        pushArg(resourceDir);
    }

    for(const MString& def : options.defines){
        if(def.empty())
            continue;
        MString flag(CppImporterSemantics::s_DefineFlagPrefix.data(), CppImporterSemantics::s_DefineFlagPrefix.size(), arena);
        flag.append(def.data(), def.size());
        argStorage.push_back(Move(flag));
    }

    MStringMap<u8> seenRoots(arena);
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

        AddExistingUnique(normalizedIncludeRoots, seenRoots, norm);
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

struct CachedFileStamp{
    MString path;
    u64 fileSize = 0u;
    u64 mtimeLow = 0u;
    u64 mtimeHigh = 0u;

    explicit CachedFileStamp(MetaArena& arena)noexcept
        : path(arena)
    {}
};

[[nodiscard]] b8 QueryFileStamp(MetaArena& scratchArena, const MStringView utf8Path, CachedFileStamp& outStamp){
    if(utf8Path.empty())
        return false;
    const TString<MetaArena> nativePath = ConvertString(scratchArena, utf8Path);
#if defined(FOUNDATION_PLATFORM_WINDOWS)
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if(!::GetFileAttributesEx(nativePath.c_str(), GetFileExInfoStandard, &data))
        return false;
    if((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0u)
        return false;
    outStamp.path.assign(utf8Path.data(), utf8Path.size());
    outStamp.fileSize = (static_cast<u64>(data.nFileSizeHigh) << 32u) | static_cast<u64>(data.nFileSizeLow);
    outStamp.mtimeLow = static_cast<u64>(data.ftLastWriteTime.dwLowDateTime);
    outStamp.mtimeHigh = static_cast<u64>(data.ftLastWriteTime.dwHighDateTime);
    return true;
#else
    struct stat status{};
    if(::stat(nativePath.c_str(), &status) != 0 || S_ISDIR(status.st_mode))
        return false;
    outStamp.path.assign(utf8Path.data(), utf8Path.size());
    outStamp.fileSize = static_cast<u64>(status.st_size);
    outStamp.mtimeLow = static_cast<u64>(status.st_mtime);
    outStamp.mtimeHigh = 0u;
    return true;
#endif
}

[[nodiscard]] b8 VerifyCachedFileStamps(MetaArena& scratchArena, const Span<const CachedFileStamp> stamps){
    CachedFileStamp current(scratchArena);
    for(const CachedFileStamp& expected : stamps){
        if(!QueryFileStamp(scratchArena, MStringView(expected.path.data(), expected.path.size()), current))
            return false;
        if(current.fileSize != expected.fileSize || current.mtimeLow != expected.mtimeLow || current.mtimeHigh != expected.mtimeHigh)
            return false;
    }
    return true;
}

[[nodiscard]] MString BuildImportCacheKey(
    MetaArena& arena,
    const MStringView virtualTuPath,
    const MStringView syntheticSource,
    const Span<const MString> argStorage,
    const CppHeaderImportOptions& options
){
    MString key(arena);
    key.reserve(256u + virtualTuPath.size() + syntheticSource.size() + argStorage.size() * 32u);
    key.append(virtualTuPath.data(), virtualTuPath.size());
    key.push_back('\x1e');
    key.push_back(options.enableAutoProjectIncludes ? '1' : '0');
    key.push_back('\x1e');
    for(const MString& arg : argStorage){
        key.append(arg.data(), arg.size());
        key.push_back('\x1f');
    }
    key.push_back('\x1e');
    for(const MString& dir : options.includeDirectories){
        key.append(dir.data(), dir.size());
        key.push_back('\x1f');
    }
    key.push_back('\x1e');
    key.append(syntheticSource.data(), syntheticSource.size());
    return key;
}

struct CachedHeaderImport{
    MetaArena arena;
    TranslationUnit unit;
    MVector<CachedFileStamp> fileStamps;

    CachedHeaderImport()noexcept
        : arena(MetascriptArenaScope::s_ParserScratch)
        , unit(arena)
        , fileStamps(arena)
    {}
};

struct HeaderImportCache{
    static constexpr usize s_MaxEntries = 128u;

    Futex lock;
    MetaArena mapArena;
    MStringMap<GlobalUniquePtr<CachedHeaderImport>> entries;

    HeaderImportCache()noexcept
        : mapArena(MetascriptArenaScope::s_ParserScratch)
        , entries(mapArena)
    {}
};

[[nodiscard]] HeaderImportCache& GetHeaderImportCache()noexcept{
    static HeaderImportCache s_Cache;
    return s_Cache;
}

[[nodiscard]] Adhd::Expr* CloneEnumValueExpr(MetaArena& arena, const Adhd::Expr* const src){
    if(!src)
        return nullptr;
    Adhd::Expr* dst = Adhd::AllocateAstNode<Adhd::Expr>(arena);
    dst->kind = src->kind;
    dst->op = src->op;
    dst->text = Adhd::InternArenaString(arena, src->text);
    dst->span = src->span;
    dst->rhs = CloneEnumValueExpr(arena, src->rhs);
    return dst;
}

[[nodiscard]] Adhd::MethodDecl* CloneImportedMethodDecl(MetaArena& dstArena, const Adhd::MethodDecl& src, const MStringView ownerClassNameOverride){
    Adhd::MethodDecl* dst = Adhd::AllocateAstNode<Adhd::MethodDecl>(dstArena);
    dst->access = src.access;
    dst->ownerClassName = !ownerClassNameOverride.empty() ? ownerClassNameOverride : Adhd::InternArenaString(dstArena, src.ownerClassName);
    if(!src.templateParams.empty()){
        dst->templateParams.reserve(src.templateParams.size());
        for(const Adhd::TemplateParamDecl& tp : src.templateParams){
            Adhd::TemplateParamDecl copyTp(dstArena);
            copyTp.name = Adhd::InternArenaString(dstArena, tp.name);
            copyTp.constValueType = CloneTypeRefDeep(dstArena, tp.constValueType);
            copyTp.span = tp.span;
            dst->templateParams.push_back(Move(copyTp));
        }
    }
    dst->returnType = CloneTypeRefDeep(dstArena, src.returnType);
    dst->name = Adhd::InternArenaString(dstArena, src.name);
    if(!src.params.empty()){
        dst->params.reserve(src.params.size());
        for(const Adhd::ParamDecl& p : src.params){
            Adhd::ParamDecl copyP;
            copyP.mode = p.mode;
            copyP.type = CloneTypeRefDeep(dstArena, p.type);
            copyP.name = Adhd::InternArenaString(dstArena, p.name);
            copyP.span = p.span;
            copyP.retainsBorrow = p.retainsBorrow;
            copyP.cppConstReference = p.cppConstReference;
            copyP.forwardsArgument = p.forwardsArgument;
            copyP.copiesArgument = p.copiesArgument;
            dst->params.push_back(copyP);
        }
    }
    dst->span = src.span;
    dst->isConst = src.isConst;
    dst->isConstructor = src.isConstructor;
    dst->isOperator = src.isOperator;
    dst->cppNamespace = Adhd::InternArenaString(dstArena, src.cppNamespace);
    dst->cppExportName = Adhd::InternArenaString(dstArena, src.cppExportName);
    dst->requiredHeader = Adhd::InternArenaString(dstArena, src.requiredHeader);
    dst->minArgCount = src.minArgCount;
    dst->hasDefaultArgs = src.hasDefaultArgs;
    dst->isStatic = src.isStatic;
    dst->returnsReference = src.returnsReference;
    dst->isImported = src.isImported;
    dst->isExtern = src.isExtern;
    return dst;
}

void MergeResolvedIncludedFiles(MetaArena& dstArena, const Span<const MString> srcFiles, Adhd::TranslationUnit& dstUnit){
    MStringMap<u8> seenFiles(dstArena);
    for(const MString& existing : dstUnit.resolvedIncludedFiles)
        seenFiles.emplace(existing, 1u);
    for(const MString& file : srcFiles)
        AddExistingUnique(dstUnit.resolvedIncludedFiles, seenFiles, file);
}

void MergeImportedUnitIntoTarget(const Adhd::TranslationUnit& src, MetaArena& dstArena, Adhd::TranslationUnit& dstUnit){
    MergeResolvedIncludedFiles(dstArena, Span<const MString>(src.resolvedIncludedFiles.data(), src.resolvedIncludedFiles.size()), dstUnit);

    MStringMap<Adhd::ClassDecl*> classByName(dstArena);
    MStringMap<Adhd::ClassDecl*> classByExportName(dstArena);
    MStringMap<Adhd::EnumDecl*> enumByName(dstArena);
    MStringMap<Adhd::EnumDecl*> enumByExportName(dstArena);
    MStringMap<Adhd::TypeAliasDecl*> aliasByName(dstArena);
    MStringMap<Adhd::Stmt*> constantByName(dstArena);
    MStringMap<u8> seenFreeFunctions(dstArena);
    MStringMap<u8> seenConstants(dstArena);

    for(Adhd::ClassDecl* cls : dstUnit.classes){
        if(!cls)
            continue;
        if(!cls->name.empty())
            classByName.emplace(MString(cls->name.data(), cls->name.size(), dstArena), cls);
        if(!cls->cppExportName.empty())
            classByExportName.emplace(MString(cls->cppExportName.data(), cls->cppExportName.size(), dstArena), cls);
    }
    for(Adhd::EnumDecl* e : dstUnit.enums){
        if(!e)
            continue;
        if(!e->name.empty())
            enumByName.emplace(MString(e->name.data(), e->name.size(), dstArena), e);
        if(!e->cppExportName.empty())
            enumByExportName.emplace(MString(e->cppExportName.data(), e->cppExportName.size(), dstArena), e);
    }
    for(Adhd::TypeAliasDecl* a : dstUnit.typeAliases){
        if(a && !a->name.empty())
            aliasByName.emplace(MString(a->name.data(), a->name.size(), dstArena), a);
    }
    for(Adhd::Stmt* g : dstUnit.globalConstants){
        if(!g)
            continue;
        if(!g->name.empty())
            constantByName.emplace(MString(g->name.data(), g->name.size(), dstArena), g);
        if(!g->cppExportName.empty())
            seenConstants.emplace(MString(g->cppExportName.data(), g->cppExportName.size(), dstArena), 1u);
    }
    for(const Adhd::MethodDecl* f : dstUnit.freeFunctions){
        if(!f || f->name.empty())
            continue;
        const MString sigKey = BuildFreeFunctionSigKey(dstArena, *f);
        seenFreeFunctions.emplace(sigKey, 1u);
    }

    auto findMergeDecl = [](const auto& byExport, const auto& byName, const MStringView name, const MStringView exportName)noexcept -> decltype(byName.begin()->second){
        if(!exportName.empty()){
            if(const auto it = byExport.find(exportName); it != byExport.end())
                return it->second;
        }
        if(!name.empty()){
            if(const auto it = byName.find(name); it != byName.end())
                return it->second;
        }
        return nullptr;
    };

    for(const Adhd::EnumDecl* srcEnum : src.enums){
        if(!srcEnum)
            continue;
        Adhd::EnumDecl* dstEnum = findMergeDecl(enumByExportName, enumByName, srcEnum->name, srcEnum->cppExportName);
        const b8 isNew = (dstEnum == nullptr);
        if(isNew){
            dstEnum = Adhd::AllocateAstNode<Adhd::EnumDecl>(dstArena);
            dstEnum->name = Adhd::InternArenaString(dstArena, srcEnum->name);
        }
        dstEnum->cppNamespace = Adhd::InternArenaString(dstArena, srcEnum->cppNamespace);
        dstEnum->cppExportName = Adhd::InternArenaString(dstArena, srcEnum->cppExportName);
        dstEnum->requiredHeader = Adhd::InternArenaString(dstArena, srcEnum->requiredHeader);
        dstEnum->isDirectCppEnum = srcEnum->isDirectCppEnum;
        dstEnum->isFlag = srcEnum->isFlag;
        dstEnum->isImported = srcEnum->isImported;
        dstEnum->isExtern = srcEnum->isExtern;
        dstEnum->sizeTypeName = Adhd::InternArenaString(dstArena, srcEnum->sizeTypeName);
        dstEnum->underlyingSize = srcEnum->underlyingSize;
        dstEnum->underlyingAlign = srcEnum->underlyingAlign;
        if(isNew){
            dstUnit.enums.push_back(dstEnum);
            if(!dstEnum->name.empty())
                enumByName.emplace(MString(dstEnum->name.data(), dstEnum->name.size(), dstArena), dstEnum);
            if(!dstEnum->cppExportName.empty())
                enumByExportName.emplace(MString(dstEnum->cppExportName.data(), dstEnum->cppExportName.size(), dstArena), dstEnum);
        }
        HashSet<MStringView, MetaArena, MStringViewHash> seenEntries(dstArena);
        if(!isNew){
            for(const Adhd::EnumEntry& existing : dstEnum->entries)
                seenEntries.insert(existing.name);
        }
        dstEnum->entries.reserve(dstEnum->entries.size() + srcEnum->entries.size());
        for(const Adhd::EnumEntry& srcEntry : srcEnum->entries){
            const MStringView entryName = Adhd::InternArenaString(dstArena, srcEntry.name);
            if(!seenEntries.insert(entryName).second)
                continue;
            Adhd::EnumEntry copyEntry;
            copyEntry.name = entryName;
            copyEntry.span = srcEntry.span;
            copyEntry.valueExpr = CloneEnumValueExpr(dstArena, srcEntry.valueExpr);
            dstEnum->entries.push_back(copyEntry);
        }
    }

    auto methodSigEq = [](const Adhd::MethodDecl& a, const Adhd::MethodDecl& b)noexcept -> b8{
        if(a.isConstructor != b.isConstructor || a.isOperator != b.isOperator || a.isConst != b.isConst || a.isStatic != b.isStatic)
            return false;
        if(a.name != b.name || a.params.size() != b.params.size() || a.templateParams.size() != b.templateParams.size())
            return false;
        for(usize i = 0u; i < a.params.size(); ++i){
            const Adhd::TypeRef* ta = a.params[i].type;
            const Adhd::TypeRef* tb = b.params[i].type;
            if((ta == nullptr) != (tb == nullptr))
                return false;
            if(ta && (ta->name != tb->name || a.params[i].mode != b.params[i].mode))
                return false;
        }
        return true;
    };

    for(const Adhd::ClassDecl* srcCls : src.classes){
        if(!srcCls)
            continue;
        Adhd::ClassDecl* dstCls = findMergeDecl(classByExportName, classByName, srcCls->name, srcCls->cppExportName);
        const b8 isNew = (dstCls == nullptr);
        if(isNew){
            dstCls = Adhd::AllocateAstNode<Adhd::ClassDecl>(dstArena);
            dstCls->name = Adhd::InternArenaString(dstArena, srcCls->name);
        }
        dstCls->cppNamespace = Adhd::InternArenaString(dstArena, srcCls->cppNamespace);
        dstCls->cppExportName = Adhd::InternArenaString(dstArena, srcCls->cppExportName);
        dstCls->requiredHeader = Adhd::InternArenaString(dstArena, srcCls->requiredHeader);
        dstCls->isImported = srcCls->isImported;
        dstCls->isExtern = srcCls->isExtern;
        dstCls->isAbstract = srcCls->isAbstract;
        dstCls->totalSize = srcCls->totalSize;
        dstCls->hasExplicitLayout = srcCls->hasExplicitLayout;
        dstCls->totalAlign = srcCls->totalAlign;
        dstCls->isCopyConstructible = srcCls->isCopyConstructible;
        dstCls->isMoveConstructible = srcCls->isMoveConstructible;
        dstCls->isCopyAssignable = srcCls->isCopyAssignable;
        dstCls->isMoveAssignable = srcCls->isMoveAssignable;
        dstCls->hasExplicitDestructor = srcCls->hasExplicitDestructor;
        if(isNew){
            dstUnit.classes.push_back(dstCls);
            if(!dstCls->name.empty())
                classByName.emplace(MString(dstCls->name.data(), dstCls->name.size(), dstArena), dstCls);
            if(!dstCls->cppExportName.empty())
                classByExportName.emplace(MString(dstCls->cppExportName.data(), dstCls->cppExportName.size(), dstArena), dstCls);
        }

        HashSet<MStringView, MetaArena, MStringViewHash> seenFields(dstArena);
        HashMap<MStringView, MVector<const Adhd::MethodDecl*>, MetaArena, MStringViewHash> seenMethodsByName(dstArena);
        if(!isNew){
            for(const Adhd::FieldDecl* existingField : dstCls->fields){
                if(existingField && !existingField->name.empty())
                    seenFields.insert(existingField->name);
            }
            for(const Adhd::MethodDecl* existingMethod : dstCls->methods){
                if(!existingMethod)
                    continue;
                auto [it, inserted] = seenMethodsByName.emplace(existingMethod->name, MVector<const Adhd::MethodDecl*>(dstArena));
                it.value().push_back(existingMethod);
            }
        }

        for(const Adhd::FieldDecl* srcField : srcCls->fields){
            if(!srcField)
                continue;
            const MStringView fieldName = Adhd::InternArenaString(dstArena, srcField->name);
            if(!isNew && seenFields.find(fieldName) != seenFields.end())
                continue;
            seenFields.insert(fieldName);
            Adhd::FieldDecl* copyField = Adhd::AllocateAstNode<Adhd::FieldDecl>(dstArena);
            copyField->access = srcField->access;
            copyField->type = CloneTypeRefDeep(dstArena, srcField->type);
            copyField->name = fieldName;
            copyField->originalIndex = dstCls->fields.size();
            copyField->isStatic = srcField->isStatic;
            copyField->resolvedSize = srcField->resolvedSize;
            copyField->resolvedAlign = srcField->resolvedAlign;
            copyField->resolvedOffset = srcField->resolvedOffset;
            dstCls->fields.push_back(copyField);
            dstCls->orderedFields.push_back(copyField);
        }

        dstCls->methods.reserve(dstCls->methods.size() + srcCls->methods.size());
        for(const Adhd::MethodDecl* srcMethod : srcCls->methods){
            if(!srcMethod)
                continue;
            Adhd::MethodDecl* copyMethod = CloneImportedMethodDecl(dstArena, *srcMethod, dstCls->name);
            if(isNew){
                dstCls->methods.push_back(copyMethod);
                continue;
            }
            auto [it, inserted] = seenMethodsByName.emplace(copyMethod->name, MVector<const Adhd::MethodDecl*>(dstArena));
            b8 duplicate = false;
            for(const Adhd::MethodDecl* existingMethod : it.value()){
                if(existingMethod && methodSigEq(*existingMethod, *copyMethod)){
                    duplicate = true;
                    break;
                }
            }
            if(!duplicate){
                it.value().push_back(copyMethod);
                dstCls->methods.push_back(copyMethod);
            }
        }
    }

    for(const Adhd::TypeAliasDecl* srcAlias : src.typeAliases){
        if(!srcAlias)
            continue;
        const MStringView aliasName = Adhd::InternArenaString(dstArena, srcAlias->name);
        if(aliasByName.find(aliasName) != aliasByName.end() || findMergeDecl(classByExportName, classByName, aliasName, {}) != nullptr)
            continue;
        Adhd::TypeAliasDecl* copyAlias = Adhd::AllocateAstNode<Adhd::TypeAliasDecl>(dstArena);
        copyAlias->name = aliasName;
        copyAlias->cppNamespace = Adhd::InternArenaString(dstArena, srcAlias->cppNamespace);
        copyAlias->cppExportName = Adhd::InternArenaString(dstArena, srcAlias->cppExportName);
        copyAlias->requiredHeader = Adhd::InternArenaString(dstArena, srcAlias->requiredHeader);
        copyAlias->aliasedType = CloneTypeRefDeep(dstArena, srcAlias->aliasedType);
        copyAlias->isImported = srcAlias->isImported;
        copyAlias->isExtern = srcAlias->isExtern;
        copyAlias->span = srcAlias->span;
        dstUnit.typeAliases.push_back(copyAlias);
        aliasByName.emplace(MString(aliasName.data(), aliasName.size(), dstArena), copyAlias);
    }

    for(const Adhd::Stmt* srcConst : src.globalConstants){
        if(!srcConst)
            continue;
        const MStringView constName = Adhd::InternArenaString(dstArena, srcConst->name);
        const MStringView cppExportName = Adhd::InternArenaString(dstArena, srcConst->cppExportName);
        if(!cppExportName.empty() && seenConstants.find(cppExportName) != seenConstants.end())
            continue;
        if(constantByName.find(constName) != constantByName.end())
            continue;
        Adhd::Stmt* copyStmt = Adhd::AllocateAstNode<Adhd::Stmt>(dstArena);
        copyStmt->kind = srcConst->kind;
        copyStmt->name = constName;
        copyStmt->varType = CloneTypeRefDeep(dstArena, srcConst->varType);
        copyStmt->cppNamespace = Adhd::InternArenaString(dstArena, srcConst->cppNamespace);
        copyStmt->cppExportName = cppExportName;
        copyStmt->requiredHeader = Adhd::InternArenaString(dstArena, srcConst->requiredHeader);
        copyStmt->isConst = srcConst->isConst;
        copyStmt->isImported = srcConst->isImported;
        copyStmt->isExtern = srcConst->isExtern;
        copyStmt->span = srcConst->span;
        if(!cppExportName.empty())
            seenConstants.emplace(MString(cppExportName.data(), cppExportName.size(), dstArena), 1u);
        constantByName.emplace(MString(constName.data(), constName.size(), dstArena), copyStmt);
        dstUnit.globalConstants.push_back(copyStmt);
    }

    for(const Adhd::MethodDecl* srcFn : src.freeFunctions){
        if(!srcFn)
            continue;
        const MString sigKey = BuildFreeFunctionSigKey(dstArena, *srcFn);
        if(seenFreeFunctions.find(MStringView(sigKey.data(), sigKey.size())) != seenFreeFunctions.end())
            continue;
        Adhd::MethodDecl* copyFn = CloneImportedMethodDecl(dstArena, *srcFn, {});
        seenFreeFunctions.emplace(sigKey, 1u);
        dstUnit.freeFunctions.push_back(copyFn);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CppHeaderImportDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


MString BuildFreeFunctionSigKey(MetaArena& arena, const MethodDecl& decl){
    MString sigKey(decl.name.data(), decl.name.size(), arena);
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
        const MString sigKey = BuildFreeFunctionSigKey(m_arena, *f);
        m_seenFreeFunctions.emplace(sigKey, 1u);
    }

    const CXCursor rootCursor = ::clang_getTranslationUnitCursor(tu);
    visitScopeCursor(rootCursor);
}

b8 ImporterContext::formatDeclName(CXCursor declCursor, MStringView& outName, MStringView& outNamespace, MStringView& outCppExportName, b8& outIsDirectEnum, b8& outIsFlag){
    const CursorKey key = MakeCursorKey(declCursor);
    if(const auto it = m_declNameCache.find(key); it != m_declNameCache.end()){
        const CachedDeclName& cached = it->second;
        outName = cached.name;
        outNamespace = cached.cppNamespace;
        outCppExportName = cached.cppExportName;
        outIsDirectEnum = cached.isDirectEnum;
        outIsFlag = cached.isFlag;
        return cached.valid;
    }

    CachedDeclName entry{};
    entry.valid = FormatNormalizedAdhdDeclName(m_arena, declCursor, entry.name, entry.cppNamespace, entry.cppExportName, entry.isDirectEnum, entry.isFlag);
    m_declNameCache.emplace(key, entry);
    outName = entry.name;
    outNamespace = entry.cppNamespace;
    outCppExportName = entry.cppExportName;
    outIsDirectEnum = entry.isDirectEnum;
    outIsFlag = entry.isFlag;
    return entry.valid;
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

    if(const auto byFileIt = m_headerByFile.find(file); byFileIt != m_headerByFile.end())
        return byFileIt->second;

    const MString rawFileName = ConsumeClangString(m_arena, ::clang_getFileName(file));
    if(rawFileName.empty()){
        m_headerByFile.emplace(file, MStringView{});
        return {};
    }

    const MString normalized = NormalizeSlashes(m_arena, MStringView(rawFileName.data(), rawFileName.size()));
    const MStringView key(normalized.data(), normalized.size());
    if(const auto it = m_headerCache.find(key); it != m_headerCache.end()){
        m_headerByFile.emplace(file, it->second);
        return it->second;
    }

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
    m_headerByFile.emplace(file, interned);
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
    if(!formatDeclName(enumCursor, adhdName, cppNs, cppExportName, isDirect, isFlag))
        return;

    EnumDecl* decl = FindExistingDeclForMerge(m_enumByExportName, m_enumByName, adhdName, cppExportName);
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
    HashSet<MStringView, MetaArena, MStringViewHash> seenEntries(m_arena);
    for(const EnumEntry& e : decl->entries)
        seenEntries.insert(e.name);
    for(const CXCursor child : children){
        if(::clang_getCursorKind(child) != CXCursor_EnumConstantDecl)
            continue;
        const MString entryNameStr = ConsumeClangString(m_arena, ::clang_getCursorSpelling(child));
        if(entryNameStr.empty())
            continue;
        const MStringView entryName = InternArenaString(m_arena, entryNameStr);
        if(!seenEntries.insert(entryName).second)
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
    if(!formatDeclName(aliasCursor, adhdName, cppNs, cppExportName, dummyDirect, dummyFlag))
        return;

    if(m_aliasByName.find(adhdName) != m_aliasByName.end() || FindExistingDeclForMerge(m_classByExportName, m_classByName, adhdName, {}) != nullptr)
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
    if(!formatDeclName(varCursor, adhdName, cppNs, cppExportName, dummyDirect, dummyFlag))
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

void ImporterContext::extractFreeFunction(CXCursor fnCursor, const b8 isTemplate){
    MStringView adhdName;
    MStringView cppNs;
    MStringView cppExportName;
    b8 dummyDirect = false;
    b8 dummyFlag = false;
    if(!formatDeclName(fnCursor, adhdName, cppNs, cppExportName, dummyDirect, dummyFlag))
        return;

    MethodDecl* decl = buildMethodDecl(fnCursor, {}, isTemplate);
    if(!decl)
        return;
    decl->name = adhdName;
    decl->cppNamespace = cppNs;
    decl->cppExportName = cppExportName;

    const MString sigKey = BuildFreeFunctionSigKey(m_arena, *decl);
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

    const MString cacheKey = __hidden_cpp_header_importer::BuildImportCacheKey(
        arena,
        MStringView(virtualTuPath.data(), virtualTuPath.size()),
        MStringView(syntheticSource.data(), syntheticSource.size()),
        Span<const MString>(argStorage.data(), argStorage.size()),
        options
    );
    const MStringView cacheKeyView(cacheKey.data(), cacheKey.size());

    __hidden_cpp_header_importer::HeaderImportCache& cache = __hidden_cpp_header_importer::GetHeaderImportCache();
    {
        FutexGuard lock(cache.lock);
        if(const auto it = cache.entries.find(cacheKeyView); it != cache.entries.end() && it->second){
            const auto& stamps = it->second->fileStamps;
            if(__hidden_cpp_header_importer::VerifyCachedFileStamps(arena, Span<const __hidden_cpp_header_importer::CachedFileStamp>(stamps.data(), stamps.size()))){
                __hidden_cpp_header_importer::MergeImportedUnitIntoTarget(it->second->unit, arena, outUnit);
                return true;
            }
            cache.entries.erase(it);
        }
    }

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
        CXTranslationUnit_SkipFunctionBodies | CXTranslationUnit_IgnoreNonErrorsFromIncludedFiles,
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

    MVector<MString> collectedIncludedFiles(arena);
    MStringMap<u8> seenInclusions(arena);
    struct InclusionCollector{
        MetaArena* arena;
        MVector<MString>* files;
        MStringMap<u8>* seen;
    } incCollector{ &arena, &collectedIncludedFiles, &seenInclusions };

    ::clang_getInclusions(tu, [](CXFile includedFile, CXSourceLocation*, unsigned, CXClientData clientData){
        auto* ctx = static_cast<InclusionCollector*>(clientData);
        if(!includedFile)
            return;
        const MString fileName = ConsumeClangString(*ctx->arena, ::clang_getFileName(includedFile));
        const MString normalized = NormalizeSlashes(*ctx->arena, MStringView(fileName.data(), fileName.size()));
        const MStringView normView(normalized.data(), normalized.size());
        if(normView.empty() || normView.ends_with(CppImporterSemantics::s_VirtualTuFilename))
            return;
        __hidden_cpp_header_importer::AddExistingUnique(*ctx->files, *ctx->seen, normalized);
    }, &incCollector);

    if(hasFatalErrors){
        __hidden_cpp_header_importer::MergeResolvedIncludedFiles(arena, Span<const MString>(collectedIncludedFiles.data(), collectedIncludedFiles.size()), outUnit);
        ::clang_disposeTranslationUnit(tu);
        ::clang_disposeIndex(index);
        return false;
    }

    GlobalUniquePtr<__hidden_cpp_header_importer::CachedHeaderImport> cached;
    {
        FutexGuard lock(cache.lock);
        cached = MakeGlobalUnique<__hidden_cpp_header_importer::CachedHeaderImport>(cache.mapArena);
    }
    b8 allStamped = cached && !collectedIncludedFiles.empty();
    if(allStamped){
        cached->fileStamps.reserve(collectedIncludedFiles.size());
        cached->unit.resolvedIncludedFiles.reserve(collectedIncludedFiles.size());
        for(const MString& file : collectedIncludedFiles){
            __hidden_cpp_header_importer::CachedFileStamp stamp(cached->arena);
            if(!__hidden_cpp_header_importer::QueryFileStamp(arena, MStringView(file.data(), file.size()), stamp)){
                allStamped = false;
                break;
            }
            cached->fileStamps.push_back(Move(stamp));
            cached->unit.resolvedIncludedFiles.push_back(MString(file.data(), file.size(), cached->arena));
        }
    }

    if(allStamped){
        MVector<MString> cachedRoots(cached->arena);
        cachedRoots.reserve(normalizedRoots.size());
        for(const MString& root : normalizedRoots)
            cachedRoots.push_back(MString(root.data(), root.size(), cached->arena));
        ImporterContext importer(cached->arena, cached->unit);
        importer.run(tu, Span<const MString>(cachedRoots.data(), cachedRoots.size()));

        ::clang_disposeTranslationUnit(tu);
        ::clang_disposeIndex(index);

        __hidden_cpp_header_importer::MergeImportedUnitIntoTarget(cached->unit, arena, outUnit);

        FutexGuard lock(cache.lock);
        if(cache.entries.size() >= __hidden_cpp_header_importer::HeaderImportCache::s_MaxEntries)
            cache.entries.clear();
        cache.entries.emplace(MString(cacheKey.data(), cacheKey.size(), cache.mapArena), Move(cached));
        return true;
    }

    {
        FutexGuard lock(cache.lock);
        cached.reset();
    }

    __hidden_cpp_header_importer::MergeResolvedIncludedFiles(arena, Span<const MString>(collectedIncludedFiles.data(), collectedIncludedFiles.size()), outUnit);
    ImporterContext importer(arena, outUnit);
    importer.run(tu, Span<const MString>(normalizedRoots.data(), normalizedRoots.size()));

    ::clang_disposeTranslationUnit(tu);
    ::clang_disposeIndex(index);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

