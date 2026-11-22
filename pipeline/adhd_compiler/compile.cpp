// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "compile.h"
#include "output_transaction.h"

#include <core/common/log.h>
#include <core/metascript/adhd/sema/type_utils.h>

#include <global/algorithm.h>
#include <global/atomic.h>
#include <global/filesystem.h>
#include <global/filesystem/path_identity.h>
#include <global/hash_utils.h>
#include <global/sync.h>
#include <global/text_utils.h>
#include <global/thread.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_COMPILER_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_compile{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static b8 IsRepresentableDepfilePath(const AStringView path)noexcept{
    if(path.empty() || path.front() == '~' || path.back() == ':' || path.back() == '\\')
        return false;
    constexpr AStringView s_Punctuation = "+\"'&,/_:.~()}@!- #$\\{";
    for(usize index = 0u; index < path.size(); ++index){
        const char c = path[index];
        if(static_cast<u8>(c) < 0x80u && !IsAsciiAlphaNumeric(c) && s_Punctuation.find(c) == AStringView::npos)
            return false;
        if(index > 0u && path[index - 1u] == '\\' && (c == '#' || c == ':' || c == '$'))
            return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ResolvedIncludeDirectory{
    Core::Metascript::MString genericText;
    Core::Metascript::MString resolvedGenericText;
    Optional<PathExistingFileIdentity> existingId;


public:
    explicit ResolvedIncludeDirectory(Core::Metascript::MetaArena& arena)noexcept
        : genericText(arena)
        , resolvedGenericText(arena)
    {}
};

[[nodiscard]] static Core::Metascript::MVector<ResolvedIncludeDirectory> BuildResolvedIncludeDirectories(
    const Span<const Core::Metascript::MString> includeDirs,
    Core::Metascript::MetaArena& arena
){
    using namespace Core::Metascript;

    MVector<ResolvedIncludeDirectory> resolved(arena);
    resolved.reserve(includeDirs.size());
    for(const MString& dirText : includeDirs){
        if(dirText.empty())
            continue;
        ResolvedIncludeDirectory entry(arena);
        const Path<MetaArena> incPath(arena, dirText);
        MString genericIncStorage(arena);
        const MStringView genericInc = GenericNativeTextView(incPath.native(), genericIncStorage);
        entry.genericText.assign(genericInc.data(), genericInc.size());

        const auto resolvedInc = ResolveComparisonPath(incPath);
        if(resolvedInc){
            MString resolvedIncStorage(arena);
            const MStringView resolvedIncView = GenericNativeTextView(resolvedInc->native(), resolvedIncStorage);
            entry.resolvedGenericText.assign(resolvedIncView.data(), resolvedIncView.size());
            const auto existingId = QueryExistingPathIdentity(*resolvedInc);
            if(existingId && existingId->has_value())
                entry.existingId = existingId->value();
        }
        resolved.push_back(Move(entry));
    }
    return resolved;
}

[[nodiscard]] static Core::Metascript::MString DeriveVirtualCompilePath(
    const Path<Core::Metascript::MetaArena>& inputPath,
    const Path<Core::Metascript::MetaArena>& resolvedInputPath,
    const Core::Metascript::MStringView rawInputView,
    const Span<const ResolvedIncludeDirectory> includeDirs,
    Core::Metascript::MetaArena& arena
){
    using namespace Core::Metascript;

    MString genericInputStorage(arena);
    const MStringView genericView = GenericNativeTextView(inputPath.native(), genericInputStorage);
    MString resolvedGenericInputStorage(arena);
    const MStringView resolvedGenericView = GenericNativeTextView(resolvedInputPath.native(), resolvedGenericInputStorage);

    MStringView bestRelView;
    const auto considerCandidate = [&](const MStringView candidateRelView)noexcept{
        if(candidateRelView.empty())
            return;
        if(bestRelView.empty() || candidateRelView.size() < bestRelView.size())
            bestRelView = candidateRelView;
    };
    const auto tryPrefixMatch = [&](const MStringView fullView, MStringView dirGeneric)noexcept{
        while(dirGeneric.size() > 1u && dirGeneric.back() == '/')
            dirGeneric.remove_suffix(1u);
        if(dirGeneric.empty() || fullView.size() <= dirGeneric.size() + 1u)
            return;
        if(SameGenericPrefixText(fullView.substr(0u, dirGeneric.size()), dirGeneric) && fullView[dirGeneric.size()] == '/')
            considerCandidate(fullView.substr(dirGeneric.size() + 1u));
    };

    for(const ResolvedIncludeDirectory& incDir : includeDirs){
        tryPrefixMatch(genericView, MStringView(incDir.genericText.data(), incDir.genericText.size()));
        if(!incDir.resolvedGenericText.empty())
            tryPrefixMatch(resolvedGenericView, MStringView(incDir.resolvedGenericText.data(), incDir.resolvedGenericText.size()));
    }

    if(!includeDirs.empty()){
        Path<MetaArena> ancestor = resolvedInputPath;
        usize slashPos = resolvedGenericView.size();
        for(;;){
            const TStringView parentView = ancestor.parentPathView();
            if(parentView.empty() || parentView == ancestor.native())
                break;
            while(slashPos > 0u && resolvedGenericView[slashPos - 1u] != '/')
                --slashPos;
            if(slashPos == 0u)
                break;
            --slashPos;
            ancestor = Move(ancestor).parentPath();

            const auto ancestorId = QueryExistingPathIdentity(ancestor);
            if(!ancestorId || !ancestorId->has_value())
                continue;
            const PathExistingFileIdentity& ancVal = ancestorId->value();
            for(const ResolvedIncludeDirectory& incDir : includeDirs){
                if(incDir.existingId.has_value() && incDir.existingId.value() == ancVal){
                    considerCandidate(resolvedGenericView.substr(slashPos + 1u));
                    break;
                }
            }
        }
    }

    if(!bestRelView.empty())
        return MString(bestRelView, arena);
    return MString(rawInputView, arena);
}


struct NamespacePathHash{
    using is_transparent = void;

    [[nodiscard]] usize operator()(const Core::Metascript::MStringView view)const noexcept{
#if defined(FOUNDATION_PLATFORM_WINDOWS)
        return static_cast<usize>(UpdateFnv64TextCanonical(s_Fnv64OffsetBasis, view));
#else
        return TransparentStringHash<Core::Metascript::MString>{}(view);
#endif
    }
    [[nodiscard]] usize operator()(const Core::Metascript::MString& str)const noexcept{
        return (*this)(Core::Metascript::MStringView(str.data(), str.size()));
    }
};

struct NamespacePathEqual{
    using is_transparent = void;

    [[nodiscard]] b8 operator()(const Core::Metascript::MStringView lhs, const Core::Metascript::MStringView rhs)const noexcept{
        return SameGenericPrefixText(lhs, rhs);
    }
    [[nodiscard]] b8 operator()(const Core::Metascript::MString& lhs, const Core::Metascript::MString& rhs)const noexcept{
        return SameGenericPrefixText(lhs, rhs);
    }
    [[nodiscard]] b8 operator()(const Core::Metascript::MString& lhs, const Core::Metascript::MStringView rhs)const noexcept{
        return SameGenericPrefixText(lhs, rhs);
    }
    [[nodiscard]] b8 operator()(const Core::Metascript::MStringView lhs, const Core::Metascript::MString& rhs)const noexcept{
        return SameGenericPrefixText(lhs, rhs);
    }
};

using NamespaceMap = HashMap<
    Core::Metascript::MString,
    Core::Metascript::MString,
    Core::Metascript::MetaArena,
    NamespacePathHash,
    NamespacePathEqual
>;

[[nodiscard]] b8 IsAlreadyNormalizedAbsoluteGenericPath(const Core::Metascript::MStringView rawPath)noexcept{
#if defined(FOUNDATION_PLATFORM_WINDOWS)
    if(rawPath.size() < 3u || !IsAsciiAlpha(rawPath[0]) || rawPath[1] != ':' || rawPath[2] != '/')
        return false;
    if(rawPath.size() > 3u && rawPath.back() == '/')
        return false;
#else
    if(rawPath.empty() || rawPath[0] != '/')
        return false;
    if(rawPath.size() > 1u && rawPath.back() == '/')
        return false;
#endif
    for(usize i = 0u; i < rawPath.size(); ++i){
        const char ch = rawPath[i];
        if(ch == '\\')
            return false;
        if(ch == '/' && i + 1u < rawPath.size()){
            const char next = rawPath[i + 1u];
            if(next == '/')
                return false;
            if(next == '.'){
                if(i + 2u == rawPath.size() || rawPath[i + 2u] == '/')
                    return false;
                if(i + 2u < rawPath.size() && rawPath[i + 2u] == '.' && (i + 3u == rawPath.size() || rawPath[i + 3u] == '/'))
                    return false;
            }
        }
    }
    return true;
}

[[nodiscard]] Core::Metascript::MString NormalizeNamespaceMapKey(const Core::Metascript::MStringView rawPath, Core::Metascript::MetaArena& arena){
    using namespace Core::Metascript;

    if(IsAlreadyNormalizedAbsoluteGenericPath(rawPath))
        return MString(rawPath.data(), rawPath.size(), arena);

    const Path<MetaArena> raw(arena, rawPath);
    Path<MetaArena> normalized(arena);
#if defined(FOUNDATION_PLATFORM_WINDOWS)
    const auto nativeText = raw.native();
    const b8 hasDriveAbs = raw.isAbsolute() || (nativeText.size() >= 3u && nativeText[1u] == FOUNDATION_TEXT(':') && (nativeText[2u] == FOUNDATION_TEXT('/') || nativeText[2u] == FOUNDATION_TEXT('\\')));
#else
    const b8 hasDriveAbs = raw.isAbsolute();
#endif
    if(hasDriveAbs){
        normalized = raw.lexicallyNormal();
    }
    else{
        const auto resolved = ResolveComparisonPath(raw);
        normalized = resolved ? *resolved : raw.lexicallyNormal();
    }
    return PathToGenericString<MChar>(arena, normalized);
}

[[nodiscard]] b8 LoadNamespaceMap(const Core::Metascript::MStringView mapPath, Core::Metascript::MetaArena& arena, NamespaceMap& outMap){
    using namespace Core::Metascript;

    MString content(arena);
    if(!ReadTextFile(Path<MetaArena>(arena, mapPath), content)){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("AdhdCompiler: failed to read namespace map '{}'"), ConvertString(mapPath));
        return false;
    }

    const MStringView text(content.data(), content.size());
    usize pos = 0u;
    while(pos < text.size()){
        usize lineEnd = text.find('\n', pos);
        if(lineEnd == MStringView::npos)
            lineEnd = text.size();
        MStringView line = text.substr(pos, lineEnd - pos);
        if(!line.empty() && line.back() == '\r')
            line = line.substr(0u, line.size() - 1u);
        pos = lineEnd + 1u;
        if(line.empty())
            continue;

        const usize tabPos = line.find('\t');
        if(tabPos == MStringView::npos){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("AdhdCompiler: invalid namespace map entry '{}' in '{}'"), ConvertString(line), ConvertString(mapPath));
            return false;
        }
        const MStringView pathPart = line.substr(0u, tabPos);
        const MStringView nsPart = line.substr(tabPos + 1u);
        if(!nsPart.empty() && !pathPart.empty()){
            MString key = NormalizeNamespaceMapKey(pathPart, arena);
            outMap.insert_or_assign(Move(key), MString(nsPart.data(), nsPart.size(), arena));
        }
    }
    return true;
}


namespace ResolveErrorKind{
    enum Enum : u8{
        None = 0u,
        ResolveFailed,
        ReadFailed,
    };
};

struct IncludeResolveError{
    ResolveErrorKind::Enum kind = ResolveErrorKind::None;
    Core::Metascript::MString pathText;
    Core::Metascript::MString auxText;
    ErrorCode error;


public:
    explicit IncludeResolveError(Core::Metascript::MetaArena& arena)noexcept
        : pathText(arena)
        , auxText(arena)
    {}
};

inline void EmitIncludeResolveError(const IncludeResolveError& err){
    if(err.kind == ResolveErrorKind::ResolveFailed){
        NWB_LOGGER_ERROR(
            FOUNDATION_TEXT("AdhdCompiler: failed to resolve include '{}' from '{}': {}"),
            ConvertString(err.pathText),
            ConvertString(err.auxText),
            ConvertString(err.error.message())
        );
    }
    else if(err.kind == ResolveErrorKind::ReadFailed){
        NWB_LOGGER_ERROR(
            FOUNDATION_TEXT("AdhdCompiler: failed to read include '{}': {}"),
            ConvertString(err.pathText),
            ConvertString(err.error.message())
        );
    }
}

class IncludeFileCache final : NoCopy{
public:
    struct Status{
        enum Enum : u8{
            Found,
            Missing,
            Error,
        };
    };


private:
    struct LookupEntry{
        Status::Enum status = Status::Missing;
        Core::Metascript::MString resolvedKey;
        Core::Metascript::MString parentDirGeneric;
        IncludeResolveError error;


    public:
        explicit LookupEntry(Core::Metascript::MetaArena& arena)noexcept
            : resolvedKey(arena)
            , parentDirGeneric(arena)
            , error(arena)
        {}
    };


public:
    IncludeFileCache()
        : m_arena(Name("pipeline/adhd_compiler/include_cache"))
        , m_lookupCache(0, NamespacePathHash(), NamespacePathEqual(), m_arena)
        , m_contentCache(0, NamespacePathHash(), NamespacePathEqual(), m_arena)
    {}


public:
    [[nodiscard]] Status::Enum resolveFromBase(
        const Core::Metascript::MStringView baseDirGeneric,
        const Core::Metascript::MStringView includePath,
        const b8 isAbsoluteOrDrive,
        Core::Metascript::MetaArena& callerArena,
        Core::Metascript::MString& outResolvedKey,
        Core::Metascript::MString& outParentDirGeneric,
        Core::Metascript::MString& outContent,
        IncludeResolveError& outError
    ){
        using namespace Core::Metascript;

        MString lookupKey(callerArena);
        lookupKey.reserve((isAbsoluteOrDrive ? 0u : baseDirGeneric.size()) + 1u + includePath.size());
        if(!isAbsoluteOrDrive)
            lookupKey.append(baseDirGeneric.data(), baseDirGeneric.size());
        lookupKey.push_back('\n');
        lookupKey.append(includePath.data(), includePath.size());
        const MStringView lookupKeyView(lookupKey.data(), lookupKey.size());

        ScopedLock<Futex> lock(m_mutex);

        const auto foundIt = m_lookupCache.find(lookupKeyView);
        if(foundIt != m_lookupCache.end()){
            const LookupEntry& entry = foundIt.value();
            if(entry.status == Status::Found){
                outResolvedKey.assign(entry.resolvedKey.data(), entry.resolvedKey.size());
                outParentDirGeneric.assign(entry.parentDirGeneric.data(), entry.parentDirGeneric.size());
                const auto contentIt = m_contentCache.find(MStringView(entry.resolvedKey.data(), entry.resolvedKey.size()));
                if(contentIt != m_contentCache.end())
                    outContent.assign(contentIt.value().data(), contentIt.value().size());
                else
                    outContent.clear();
            }
            else if(entry.status == Status::Error){
                outError.kind = entry.error.kind;
                outError.pathText.assign(entry.error.pathText.data(), entry.error.pathText.size());
                outError.auxText.assign(entry.error.auxText.data(), entry.error.auxText.size());
                outError.error = entry.error.error;
            }
            return entry.status;
        }

        LookupEntry entry(m_arena);
        const Path<MetaArena> include(m_arena, includePath);
        const Path<MetaArena> baseDir(m_arena, isAbsoluteOrDrive ? MStringView{} : baseDirGeneric);
        const auto candidate = ResolveComparisonPath(isAbsoluteOrDrive ? include : baseDir / include);
        if(!candidate){
            entry.status = Status::Error;
            entry.error.kind = ResolveErrorKind::ResolveFailed;
            entry.error.pathText.assign(includePath.data(), includePath.size());
            const auto baseNative = PathToGenericString<MChar>(m_arena, baseDir);
            entry.error.auxText.assign(baseNative.data(), baseNative.size());
            entry.error.error = candidate.error();

            outError.kind = entry.error.kind;
            outError.pathText.assign(entry.error.pathText.data(), entry.error.pathText.size());
            outError.auxText.assign(entry.error.auxText.data(), entry.error.auxText.size());
            outError.error = entry.error.error;

            m_lookupCache.emplace(MString(lookupKeyView.data(), lookupKeyView.size(), m_arena), Move(entry));
            return Status::Error;
        }

        MString candidateGeneric = PathToGenericString<MChar>(m_arena, *candidate);
        const MStringView candidateView(candidateGeneric.data(), candidateGeneric.size());
        const auto contentIt = m_contentCache.find(candidateView);
        if(contentIt != m_contentCache.end()){
            MString parentGeneric = PathToGenericString<MChar>(m_arena, candidate->parentPath());
            entry.status = Status::Found;
            entry.resolvedKey = candidateGeneric;
            entry.parentDirGeneric = Move(parentGeneric);

            outResolvedKey.assign(entry.resolvedKey.data(), entry.resolvedKey.size());
            outParentDirGeneric.assign(entry.parentDirGeneric.data(), entry.parentDirGeneric.size());
            outContent.assign(contentIt.value().data(), contentIt.value().size());

            m_lookupCache.emplace(MString(lookupKeyView.data(), lookupKeyView.size(), m_arena), Move(entry));
            return Status::Found;
        }

        const auto directory = IsDirectory(*candidate);
        if(!directory || *directory){
            const ErrorCode error = directory ? MakeErrorCode(ErrorCondition::is_a_directory) : directory.error();
            if(IsMissingPathError(error)){
                entry.status = Status::Missing;
                m_lookupCache.emplace(MString(lookupKeyView.data(), lookupKeyView.size(), m_arena), Move(entry));
                return Status::Missing;
            }
            entry.status = Status::Error;
            entry.error.kind = ResolveErrorKind::ReadFailed;
            entry.error.pathText = candidateGeneric;
            entry.error.error = error;

            outError.kind = entry.error.kind;
            outError.pathText.assign(entry.error.pathText.data(), entry.error.pathText.size());
            outError.error = error;

            m_lookupCache.emplace(MString(lookupKeyView.data(), lookupKeyView.size(), m_arena), Move(entry));
            return Status::Error;
        }

        MString cachedContent(m_arena);
        const auto read = ReadTextFile(*candidate, cachedContent);
        if(!read){
            if(IsMissingPathError(read.error())){
                entry.status = Status::Missing;
                m_lookupCache.emplace(MString(lookupKeyView.data(), lookupKeyView.size(), m_arena), Move(entry));
                return Status::Missing;
            }
            entry.status = Status::Error;
            entry.error.kind = ResolveErrorKind::ReadFailed;
            entry.error.pathText = candidateGeneric;
            entry.error.error = read.error();

            outError.kind = entry.error.kind;
            outError.pathText.assign(entry.error.pathText.data(), entry.error.pathText.size());
            outError.error = read.error();

            m_lookupCache.emplace(MString(lookupKeyView.data(), lookupKeyView.size(), m_arena), Move(entry));
            return Status::Error;
        }

        MString parentGeneric = PathToGenericString<MChar>(m_arena, candidate->parentPath());
        entry.status = Status::Found;
        entry.resolvedKey = candidateGeneric;
        entry.parentDirGeneric = Move(parentGeneric);

        outResolvedKey.assign(entry.resolvedKey.data(), entry.resolvedKey.size());
        outParentDirGeneric.assign(entry.parentDirGeneric.data(), entry.parentDirGeneric.size());
        outContent.assign(cachedContent.data(), cachedContent.size());

        m_contentCache.emplace(Move(candidateGeneric), Move(cachedContent));
        m_lookupCache.emplace(MString(lookupKeyView.data(), lookupKeyView.size(), m_arena), Move(entry));
        return Status::Found;
    }


private:
    Futex m_mutex;
    Core::Metascript::MetaArena m_arena;
    HashMap<
        Core::Metascript::MString,
        LookupEntry,
        Core::Metascript::MetaArena,
        NamespacePathHash,
        NamespacePathEqual
    > m_lookupCache;
    HashMap<
        Core::Metascript::MString,
        Core::Metascript::MString,
        Core::Metascript::MetaArena,
        NamespacePathHash,
        NamespacePathEqual
    > m_contentCache;
};


class FilesystemIncludeResolver final : public Core::Metascript::IIncludeResolver{
public:
    FilesystemIncludeResolver(
        const Core::Metascript::MStringView currentFileDirGeneric,
        const Span<const ResolvedIncludeDirectory> includeDirs,
        const NamespaceMap* namespaceMap,
        IncludeFileCache& cache,
        Core::Metascript::MetaArena& arena,
        Core::Metascript::MVector<IncludeResolveError>* deferredErrors = nullptr
    )
        : m_resolvedParentDirTexts(arena)
        , m_includeDirs(includeDirs)
        , m_namespaceMap(namespaceMap)
        , m_cache(cache)
        , m_deferredErrors(deferredErrors)
    {
        m_resolvedParentDirTexts.emplace_back(currentFileDirGeneric.data(), currentFileDirGeneric.size(), arena);
    }


public:
    [[nodiscard]] virtual b8 resolveInclude(const Core::Metascript::MStringView includePath, const b8 isAngleBracket, Core::Metascript::MetaArena& arena, Core::Metascript::MString& outResolvedKey, Core::Metascript::MString& outContent)override{
        using namespace Core::Metascript;

        if(!Adhd::HasAdhdExtension(includePath))
            return false;

        b8 isAbsoluteOrDrive = false;
        if(!includePath.empty() && (includePath[0] == '/' || includePath[0] == '\\'))
            isAbsoluteOrDrive = true;
#if defined(FOUNDATION_PLATFORM_WINDOWS)
        else if(includePath.size() >= 2u && includePath[1u] == ':')
            isAbsoluteOrDrive = true;
#endif

        MString parentDirGeneric(arena);
        IncludeResolveError error(arena);

        const auto handleResult = [&](const IncludeFileCache::Status::Enum result) -> b8{
            if(result == IncludeFileCache::Status::Found){
                m_resolvedParentDirTexts.push_back(Move(parentDirGeneric));
                return true;
            }
            if(result == IncludeFileCache::Status::Error){
                if(m_deferredErrors)
                    m_deferredErrors->push_back(Move(error));
                else
                    EmitIncludeResolveError(error);
            }
            return false;
        };

        if(isAbsoluteOrDrive){
            const IncludeFileCache::Status::Enum result = m_cache.resolveFromBase(
                {},
                includePath,
                true,
                arena,
                outResolvedKey,
                parentDirGeneric,
                outContent,
                error
            );
            return handleResult(result);
        }

        if(!isAngleBracket){
            for(usize i = m_resolvedParentDirTexts.size(); i > 0u; --i){
                const MString& baseText = m_resolvedParentDirTexts[i - 1u];
                const IncludeFileCache::Status::Enum result = m_cache.resolveFromBase(
                    MStringView(baseText.data(), baseText.size()),
                    includePath,
                    false,
                    arena,
                    outResolvedKey,
                    parentDirGeneric,
                    outContent,
                    error
                );
                if(result != IncludeFileCache::Status::Missing)
                    return handleResult(result);
            }
        }

        for(const ResolvedIncludeDirectory& incDir : m_includeDirs){
            const IncludeFileCache::Status::Enum result = m_cache.resolveFromBase(
                MStringView(incDir.genericText.data(), incDir.genericText.size()),
                includePath,
                false,
                arena,
                outResolvedKey,
                parentDirGeneric,
                outContent,
                error
            );
            if(result != IncludeFileCache::Status::Missing)
                return handleResult(result);
        }

        return false;
    }

    virtual void popIncludeScope()noexcept override{
        if(m_resolvedParentDirTexts.size() > 1u)
            m_resolvedParentDirTexts.pop_back();
    }
    [[nodiscard]] virtual Core::Metascript::MStringView resolveIncludeNamespace(const Core::Metascript::MStringView resolvedKey)const noexcept override{
        if(!m_namespaceMap || resolvedKey.empty())
            return {};
        const auto it = m_namespaceMap->find(resolvedKey);
        if(it == m_namespaceMap->end())
            return {};
        return Core::Metascript::MStringView(it.value().data(), it.value().size());
    }


private:
    Core::Metascript::MVector<Core::Metascript::MString> m_resolvedParentDirTexts;
    Span<const ResolvedIncludeDirectory> m_includeDirs;
    const NamespaceMap* m_namespaceMap = nullptr;
    IncludeFileCache& m_cache;
    Core::Metascript::MVector<IncludeResolveError>* m_deferredErrors = nullptr;
};


class OutputPathConflictIndex final : NoCopy{
public:
    explicit OutputPathConflictIndex(Core::Metascript::MetaArena& arena)
        : m_arena(arena)
        , m_inputPaths(arena)
        , m_inputIdentities(arena)
        , m_inputBuckets(0, Hasher<u64>(), EqualTo<u64>(), arena)
        , m_outputPaths(arena)
        , m_outputIdentities(arena)
        , m_outputBuckets(0, Hasher<u64>(), EqualTo<u64>(), arena)
    {}


public:
    void reserveInputs(const usize count){
        m_inputPaths.reserve(count);
        m_inputIdentities.reserve(count);
    }

    void reserveOutputs(const usize count){
        m_outputPaths.reserve(count);
        m_outputIdentities.reserve(count);
    }

    [[nodiscard]] b8 addInput(const Path<Core::Metascript::MetaArena>& inputPath){
        auto identity = ResolvePathOutputIdentity(inputPath, m_arena);
        if(!identity){
            NWB_LOGGER_ERROR(
                FOUNDATION_TEXT("AdhdCompiler: failed to compute identity for '{}': {}"),
                PathToString<tchar>(inputPath),
                ConvertString(identity.error().message())
            );
            return false;
        }
        const u64 hashValue = identity->hashValue;
        const usize index = m_inputPaths.size();
        m_inputPaths.push_back(inputPath);
        m_inputIdentities.push_back(Move(*identity));
        insertBucketIndex(m_inputBuckets, hashValue, index);
        return true;
    }

    [[nodiscard]] b8 addOutput(const Path<Core::Metascript::MetaArena>& outputPath){
        auto identity = ResolvePathOutputIdentity(outputPath, m_arena);
        if(!identity){
            NWB_LOGGER_ERROR(
                FOUNDATION_TEXT("AdhdCompiler: failed to compute identity for '{}': {}"),
                PathToString<tchar>(outputPath),
                ConvertString(identity.error().message())
            );
            return false;
        }
        if(conflictsWithBucket(outputPath, *identity, m_outputPaths, m_outputIdentities, m_outputBuckets))
            return false;
        if(conflictsWithBucket(outputPath, *identity, m_inputPaths, m_inputIdentities, m_inputBuckets))
            return false;

        const u64 hashValue = identity->hashValue;
        const usize index = m_outputPaths.size();
        m_outputPaths.push_back(outputPath);
        m_outputIdentities.push_back(Move(*identity));
        insertBucketIndex(m_outputBuckets, hashValue, index);
        return true;
    }

    [[nodiscard]] b8 conflictsWithOutputs(const Path<Core::Metascript::MetaArena>& candidatePath){
        auto identity = ResolvePathOutputIdentity(candidatePath, m_arena);
        if(!identity){
            NWB_LOGGER_ERROR(
                FOUNDATION_TEXT("AdhdCompiler: failed to compute identity for '{}': {}"),
                PathToString<tchar>(candidatePath),
                ConvertString(identity.error().message())
            );
            return true;
        }
        return conflictsWithBucket(candidatePath, *identity, m_outputPaths, m_outputIdentities, m_outputBuckets);
    }


private:
    using BucketMap = HashMap<
        u64,
        Core::Metascript::MVector<usize>,
        Core::Metascript::MetaArena
    >;

    void insertBucketIndex(BucketMap& buckets, const u64 hashValue, const usize index){
        const auto it = buckets.find(hashValue);
        if(it != buckets.end()){
            it.value().push_back(index);
            return;
        }
        Core::Metascript::MVector<usize> entries(m_arena);
        entries.push_back(index);
        buckets.emplace(hashValue, Move(entries));
    }

    [[nodiscard]] b8 conflictsWithBucket(
        const Path<Core::Metascript::MetaArena>& candidatePath,
        const PathOutputIdentity<Core::Metascript::MetaArena>& candidateIdentity,
        const Core::Metascript::MVector<Path<Core::Metascript::MetaArena>>& existingPaths,
        const Core::Metascript::MVector<PathOutputIdentity<Core::Metascript::MetaArena>>& existingIdentities,
        const BucketMap& buckets
    )const{
        const auto it = buckets.find(candidateIdentity.hashValue);
        if(it == buckets.end())
            return false;
        for(const usize existingIdx : it.value()){
            const auto same = existingIdentities[existingIdx].sameAs(candidateIdentity);
            if(!same){
                NWB_LOGGER_ERROR(
                    FOUNDATION_TEXT("AdhdCompiler: failed to compare '{}' and '{}': {}"),
                    PathToString<tchar>(candidatePath),
                    PathToString<tchar>(existingPaths[existingIdx]),
                    ConvertString(same.error().message())
                );
                return true;
            }
            if(*same){
                NWB_LOGGER_ERROR(
                    FOUNDATION_TEXT("AdhdCompiler: paths '{}' and '{}' have the same output name"),
                    PathToString<tchar>(candidatePath),
                    PathToString<tchar>(existingPaths[existingIdx])
                );
                return true;
            }
        }
        return false;
    }


private:
    Core::Metascript::MetaArena& m_arena;
    Core::Metascript::MVector<Path<Core::Metascript::MetaArena>> m_inputPaths;
    Core::Metascript::MVector<PathOutputIdentity<Core::Metascript::MetaArena>> m_inputIdentities;
    BucketMap m_inputBuckets;
    Core::Metascript::MVector<Path<Core::Metascript::MetaArena>> m_outputPaths;
    Core::Metascript::MVector<PathOutputIdentity<Core::Metascript::MetaArena>> m_outputIdentities;
    BucketMap m_outputBuckets;
};


struct CompileWorkerSlot{
    b8 ok = false;
    b8 readFailed = false;
    Core::Metascript::MVector<IncludeResolveError> resolverErrors;
    Core::Metascript::MVector<Core::Metascript::ParseError> parseErrors;
    Core::Metascript::MVector<Core::Metascript::MString> resolvedIncludedFiles;
    Core::Metascript::MString generatedHeader;
    Core::Metascript::MString generatedSource;


public:
    explicit CompileWorkerSlot(Core::Metascript::MetaArena& arena)noexcept
        : resolverErrors(arena)
        , parseErrors(arena)
        , resolvedIncludedFiles(arena)
        , generatedHeader(arena)
        , generatedSource(arena)
    {}
};


inline constexpr Name s_CompileWorkerArena("pipeline/adhd_compiler/worker");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 CompileAdhd(const AdhdCompileOptions& options){
    using namespace Core::Metascript;

    MetaArena& arena = options.arena;
    if(!options.emitCppExternsPath.empty() && options.inputs.size() > 1u){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("AdhdCompiler: --emit-cpp-externs requires a single input file"));
        return false;
    }

    const Path<MetaArena> outDir(arena, options.outputDirectory);
    MVector<Path<MetaArena>> inputPaths(arena);
    MVector<Path<MetaArena>> headerOutPaths(arena);
    MVector<Path<MetaArena>> sourceOutPaths(arena);
    MVector<Path<MetaArena>> outputPaths(arena);
    __hidden_compile::OutputPathConflictIndex conflictIndex(arena);
    inputPaths.reserve(options.inputs.size());
    headerOutPaths.reserve(options.inputs.size());
    sourceOutPaths.reserve(options.inputs.size());
    const usize expectedOutputCount = options.inputs.size() * 2u + (options.emitCppExternsPath.empty() ? 0u : 1u) + (options.depfilePath.empty() ? 0u : 1u);
    outputPaths.reserve(expectedOutputCount);
    conflictIndex.reserveInputs(options.inputs.size());
    conflictIndex.reserveOutputs(expectedOutputCount);
    for(const MString& input : options.inputs){
        Path<MetaArena> headerOutPath = outDir / Path<MetaArena>(arena, Adhd::Compiler::DeriveHeaderFilename(input, arena));
        Path<MetaArena> sourceOutPath = outDir / Path<MetaArena>(arena, Adhd::Compiler::DeriveSourceFilename(input, arena));
        auto absoluteInput = ResolveComparisonPath(Path<MetaArena>(arena, input));
        auto absoluteHeader = ResolveComparisonPath(headerOutPath);
        auto absoluteSource = ResolveComparisonPath(sourceOutPath);
        if(!absoluteInput){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("AdhdCompiler: failed to resolve input path '{}': {}"), ConvertString(input), ConvertString(absoluteInput.error().message()));
            return false;
        }
        if(!absoluteHeader){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("AdhdCompiler: failed to resolve header path for '{}': {}"), ConvertString(input), ConvertString(absoluteHeader.error().message()));
            return false;
        }
        if(!absoluteSource){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("AdhdCompiler: failed to resolve source path for '{}': {}"), ConvertString(input), ConvertString(absoluteSource.error().message()));
            return false;
        }
        if(!conflictIndex.addInput(*absoluteInput))
            return false;
        inputPaths.push_back(Move(*absoluteInput));
        headerOutPaths.push_back(Move(headerOutPath));
        sourceOutPaths.push_back(Move(sourceOutPath));
        outputPaths.push_back(Move(*absoluteHeader));
        outputPaths.push_back(Move(*absoluteSource));
    }
    for(const Path<MetaArena>& outPath : outputPaths){
        if(!conflictIndex.addOutput(outPath))
            return false;
    }

    const auto registerOptionalOutput = [&](const MStringView rawPath, const tchar* kindLabel, Path<MetaArena>* outResolvedPath) -> b8{
        if(rawPath.empty())
            return true;
        auto resolved = ResolveComparisonPath(Path<MetaArena>(arena, rawPath));
        if(!resolved){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("AdhdCompiler: failed to resolve {} path '{}': {}"), kindLabel, ConvertString(rawPath), ConvertString(resolved.error().message()));
            return false;
        }
        if(!conflictIndex.addOutput(*resolved))
            return false;
        if(outResolvedPath)
            *outResolvedPath = *resolved;
        outputPaths.push_back(Move(*resolved));
        return true;
    };

    Path<MetaArena> emitCppExternsOutPath(arena);
    Path<MetaArena> depfileOutPath(arena);
    if(!registerOptionalOutput(options.emitCppExternsPath, FOUNDATION_TEXT("externs output"), &emitCppExternsOutPath))
        return false;
    if(!registerOptionalOutput(options.depfilePath, FOUNDATION_TEXT("depfile"), &depfileOutPath))
        return false;

    const MStringView customNs = options.customNamespace;
    MString combinedDepContent(arena);
    MString emittedCppExterns(arena);
    MVector<MString> generatedHeaders(arena);
    MVector<MString> generatedSources(arena);
    generatedHeaders.reserve(options.inputs.size());
    generatedSources.reserve(options.inputs.size());

    __hidden_compile::NamespaceMap namespaceMap(0, __hidden_compile::NamespacePathHash(), __hidden_compile::NamespacePathEqual(), arena);
    const __hidden_compile::NamespaceMap* activeNamespaceMap = nullptr;
    if(!options.namespaceMapPath.empty()){
        if(!__hidden_compile::LoadNamespaceMap(options.namespaceMapPath, arena, namespaceMap))
            return false;
        activeNamespaceMap = &namespaceMap;
    }

    const MVector<__hidden_compile::ResolvedIncludeDirectory> resolvedIncludeDirs = __hidden_compile::BuildResolvedIncludeDirectories(options.includeDirectories, arena);
    __hidden_compile::IncludeFileCache includeCache;

    HashSet<
        MString,
        MetaArena,
        __hidden_compile::NamespacePathHash,
        __hidden_compile::NamespacePathEqual
    > checkedIncludeKeys(0, __hidden_compile::NamespacePathHash(), __hidden_compile::NamespacePathEqual(), arena);

    const auto validateAndAppendDepfile = [&](const usize inputIndex, const Path<MetaArena>& inputPath, const MStringView inputView, const Span<const MString> resolvedIncludedFiles) -> b8{
        for(const MString& includePath : resolvedIncludedFiles){
            const MStringView includeKeyView(includePath.data(), includePath.size());
            if(checkedIncludeKeys.find(includeKeyView) != checkedIncludeKeys.end())
                continue;
            const auto include = ResolveComparisonPath(Path<MetaArena>(arena, includePath));
            if(!include){
                NWB_LOGGER_ERROR(FOUNDATION_TEXT("AdhdCompiler: failed to resolve included path '{}': {}"), ConvertString(includePath), ConvertString(include.error().message()));
                return false;
            }
            if(conflictIndex.conflictsWithOutputs(*include))
                return false;
            checkedIncludeKeys.emplace(includePath.data(), includePath.size(), arena);
        }

        if(!options.depfilePath.empty()){
            const Path<MetaArena>& headerOutPath = headerOutPaths[inputIndex];
            const Path<MetaArena>& sourceOutPath = sourceOutPaths[inputIndex];
            MString headerGenericStorage(arena);
            MString sourceGenericStorage(arena);
            MString inputGenericStorage(arena);
            const MStringView headerGeneric = GenericNativeTextView(headerOutPath.native(), headerGenericStorage);
            const MStringView sourceGeneric = GenericNativeTextView(sourceOutPath.native(), sourceGenericStorage);
            const MStringView inputGeneric = GenericNativeTextView(inputPath.native(), inputGenericStorage);
            if(
                !__hidden_compile::IsRepresentableDepfilePath(headerGeneric)
                || !__hidden_compile::IsRepresentableDepfilePath(sourceGeneric)
                || !__hidden_compile::IsRepresentableDepfilePath(inputGeneric)
            ){
                NWB_LOGGER_ERROR(FOUNDATION_TEXT("AdhdCompiler: depfile path for '{}' cannot be represented safely"), ConvertString(inputView));
                return false;
            }
            for(const MString& depPath : resolvedIncludedFiles){
                if(!__hidden_compile::IsRepresentableDepfilePath(depPath)){
                    NWB_LOGGER_ERROR(FOUNDATION_TEXT("AdhdCompiler: included dependency path for '{}' cannot be represented safely"), ConvertString(inputView));
                    return false;
                }
            }

            AppendDepfileEscapedPath(headerGeneric, combinedDepContent);
            combinedDepContent.push_back(' ');
            AppendDepfileEscapedPath(sourceGeneric, combinedDepContent);
            combinedDepContent.append(": ");
            AppendDepfileEscapedPath(inputGeneric, combinedDepContent);
            for(const MString& depPath : resolvedIncludedFiles){
                combinedDepContent.push_back(' ');
                AppendDepfileEscapedPath(depPath, combinedDepContent);
            }
            combinedDepContent.push_back('\n');
        }
        return true;
    };

    if(options.inputs.size() <= 1u){
        for(usize inputIndex = 0u; inputIndex < options.inputs.size(); ++inputIndex){
            const MString& inputPathText = options.inputs[inputIndex];
            const MStringView inputView = inputPathText;
            const Path<MetaArena> inputPath(arena, inputView);

            MString sourceContent(arena);
            if(!ReadTextFile(inputPath, sourceContent)){
                NWB_LOGGER_ERROR(FOUNDATION_TEXT("AdhdCompiler: failed to read input file '{}'"), ConvertString(inputView));
                return false;
            }

            const Path<MetaArena> parentDir = inputPaths[inputIndex].parentPath();
            MString inputParentText = PathToGenericString<MChar>(arena, parentDir);
            __hidden_compile::FilesystemIncludeResolver resolver(
                MStringView(inputParentText.data(), inputParentText.size()),
                resolvedIncludeDirs,
                activeNamespaceMap,
                includeCache,
                arena
            );
            const MString virtualPath = __hidden_compile::DeriveVirtualCompilePath(inputPath, inputPaths[inputIndex], inputView, resolvedIncludeDirs, arena);

            MVector<MString> cppIncludeDirs(arena);
            cppIncludeDirs.reserve(options.cppIncludeDirectories.size() + 1u);
            for(const MString& dir : options.cppIncludeDirectories)
                cppIncludeDirs.push_back(dir);
            if(!inputParentText.empty())
                cppIncludeDirs.push_back(Move(inputParentText));

            Adhd::Compiler::CompileOptions compileOpts;
            compileOpts.source = sourceContent;
            compileOpts.virtualFilePath = virtualPath;
            compileOpts.includeResolver = &resolver;
            compileOpts.customNamespace = customNs;
            compileOpts.cppHeaders = options.cppHeaders;
            compileOpts.cppIncludeDirectories = cppIncludeDirs;
            compileOpts.macroDefinitions = options.macroDefinitions;
            compileOpts.cppDefines = options.cppDefines;
            compileOpts.cppCompilerArgs = options.cppCompilerArgs;
            if(!options.emitCppExternsPath.empty())
                compileOpts.outEmittedCppExterns = &emittedCppExterns;

            Adhd::CompileResult result(arena);
            if(!Adhd::Compiler::Compile(compileOpts, arena, result)){
                for(const ParseError& err : result.errors)
                    NWB_LOGGER_ERROR(FOUNDATION_TEXT("{}:{}:{}: error: {}"), ConvertString(inputView), err.line, err.column, ConvertString(err.message));
                return false;
            }

            if(!validateAndAppendDepfile(inputIndex, inputPath, inputView, result.unit.resolvedIncludedFiles))
                return false;
            generatedHeaders.push_back(Move(result.generatedHeader));
            generatedSources.push_back(Move(result.generatedSource));
        }
    }
    else{
        (void)Adhd::BuiltinPreludeLoadError();

        const usize inputCount = options.inputs.size();
        MVector<__hidden_compile::CompileWorkerSlot> slots(arena);
        slots.reserve(inputCount);
        for(usize i = 0u; i < inputCount; ++i)
            slots.emplace_back(arena);

        Atomic<usize> nextIndex{ 0u };
        Futex slotMutex;

        const auto runWorker = [&](){
            MetaArena workerArena(__hidden_compile::s_CompileWorkerArena);
            for(;;){
                const usize inputIndex = nextIndex.fetch_add(1u, MemoryOrder::relaxed);
                if(inputIndex >= inputCount)
                    break;

                const MString& inputPathText = options.inputs[inputIndex];
                const MStringView inputView(inputPathText.data(), inputPathText.size());
                const Path<MetaArena> inputPath(workerArena, inputView);

                MString sourceContent(workerArena);
                if(!ReadTextFile(inputPath, sourceContent)){
                    ScopedLock<Futex> lock(slotMutex);
                    slots[inputIndex].readFailed = true;
                    continue;
                }

                const Path<MetaArena> workerResolvedInput(workerArena, inputPaths[inputIndex].native());
                const Path<MetaArena> parentDir = workerResolvedInput.parentPath();
                MString inputParentText = PathToGenericString<MChar>(workerArena, parentDir);

                MVector<__hidden_compile::IncludeResolveError> localResolverErrors(workerArena);
                __hidden_compile::FilesystemIncludeResolver resolver(
                    MStringView(inputParentText.data(), inputParentText.size()),
                    resolvedIncludeDirs,
                    activeNamespaceMap,
                    includeCache,
                    workerArena,
                    &localResolverErrors
                );
                const MString virtualPath = __hidden_compile::DeriveVirtualCompilePath(inputPath, workerResolvedInput, inputView, resolvedIncludeDirs, workerArena);

                MVector<MString> cppIncludeDirs(workerArena);
                cppIncludeDirs.reserve(options.cppIncludeDirectories.size() + 1u);
                for(const MString& dir : options.cppIncludeDirectories)
                    cppIncludeDirs.emplace_back(dir.data(), dir.size(), workerArena);
                if(!inputParentText.empty())
                    cppIncludeDirs.push_back(Move(inputParentText));

                MVector<MString> workerCppHeaders(workerArena);
                workerCppHeaders.reserve(options.cppHeaders.size());
                for(const MString& h : options.cppHeaders)
                    workerCppHeaders.emplace_back(h.data(), h.size(), workerArena);

                MVector<MString> workerMacroDefs(workerArena);
                workerMacroDefs.reserve(options.macroDefinitions.size());
                for(const MString& d : options.macroDefinitions)
                    workerMacroDefs.emplace_back(d.data(), d.size(), workerArena);

                MVector<MString> workerCppDefs(workerArena);
                workerCppDefs.reserve(options.cppDefines.size());
                for(const MString& d : options.cppDefines)
                    workerCppDefs.emplace_back(d.data(), d.size(), workerArena);

                MVector<MString> workerCppArgs(workerArena);
                workerCppArgs.reserve(options.cppCompilerArgs.size());
                for(const MString& a : options.cppCompilerArgs)
                    workerCppArgs.emplace_back(a.data(), a.size(), workerArena);

                Adhd::Compiler::CompileOptions compileOpts;
                compileOpts.source = sourceContent;
                compileOpts.virtualFilePath = virtualPath;
                compileOpts.includeResolver = &resolver;
                compileOpts.customNamespace = customNs;
                compileOpts.cppHeaders = workerCppHeaders;
                compileOpts.cppIncludeDirectories = cppIncludeDirs;
                compileOpts.macroDefinitions = workerMacroDefs;
                compileOpts.cppDefines = workerCppDefs;
                compileOpts.cppCompilerArgs = workerCppArgs;

                Adhd::CompileResult result(workerArena);
                const b8 ok = Adhd::Compiler::Compile(compileOpts, workerArena, result);

                ScopedLock<Futex> lock(slotMutex);
                __hidden_compile::CompileWorkerSlot& slot = slots[inputIndex];
                slot.ok = ok;
                for(const __hidden_compile::IncludeResolveError& rErr : localResolverErrors){
                    __hidden_compile::IncludeResolveError copied(arena);
                    copied.kind = rErr.kind;
                    copied.pathText.assign(rErr.pathText.data(), rErr.pathText.size());
                    copied.auxText.assign(rErr.auxText.data(), rErr.auxText.size());
                    copied.error = rErr.error;
                    slot.resolverErrors.push_back(Move(copied));
                }
                if(!ok){
                    slot.parseErrors.reserve(result.errors.size());
                    for(const ParseError& err : result.errors){
                        ParseError copied{
                            err.line,
                            err.column,
                            MString(err.message.data(), err.message.size(), arena)
                        };
                        slot.parseErrors.push_back(Move(copied));
                    }
                    continue;
                }
                slot.resolvedIncludedFiles.reserve(result.unit.resolvedIncludedFiles.size());
                for(const MString& dep : result.unit.resolvedIncludedFiles)
                    slot.resolvedIncludedFiles.emplace_back(dep.data(), dep.size(), arena);
                slot.generatedHeader.assign(result.generatedHeader.data(), result.generatedHeader.size());
                slot.generatedSource.assign(result.generatedSource.data(), result.generatedSource.size());
            }
        };

        const usize hwThreads = static_cast<usize>(std::thread::hardware_concurrency());
        const usize workerCount = Min(inputCount, Max<usize>(1u, hwThreads > 0u ? hwThreads : 4u));
        {
            MVector<JoiningThread> threads(arena);
            threads.reserve(workerCount > 1u ? workerCount - 1u : 0u);
            for(usize w = 1u; w < workerCount; ++w)
                threads.emplace_back(runWorker);
            runWorker();
        }

        for(usize inputIndex = 0u; inputIndex < inputCount; ++inputIndex){
            const MString& inputPathText = options.inputs[inputIndex];
            const MStringView inputView = inputPathText;
            const Path<MetaArena> inputPath(arena, inputView);
            __hidden_compile::CompileWorkerSlot& slot = slots[inputIndex];

            if(slot.readFailed){
                NWB_LOGGER_ERROR(FOUNDATION_TEXT("AdhdCompiler: failed to read input file '{}'"), ConvertString(inputView));
                return false;
            }
            for(const __hidden_compile::IncludeResolveError& rErr : slot.resolverErrors)
                __hidden_compile::EmitIncludeResolveError(rErr);
            if(!slot.ok){
                for(const ParseError& err : slot.parseErrors)
                    NWB_LOGGER_ERROR(FOUNDATION_TEXT("{}:{}:{}: error: {}"), ConvertString(inputView), err.line, err.column, ConvertString(err.message));
                return false;
            }
            if(!validateAndAppendDepfile(inputIndex, inputPath, inputView, slot.resolvedIncludedFiles))
                return false;
            generatedHeaders.push_back(Move(slot.generatedHeader));
            generatedSources.push_back(Move(slot.generatedSource));
        }
    }

    if(!EnsureDirectories(outDir)){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("AdhdCompiler: failed to create output directory '{}'"), ConvertString(options.outputDirectory));
        return false;
    }
    const auto ensureOptionalParentDir = [](const Path<MetaArena>& targetPath, const MStringView rawPath, const tchar* kindLabel) -> b8{
        if(rawPath.empty())
            return true;
        const Path<MetaArena> parent = targetPath.parentPath();
        if(!parent.empty() && !EnsureDirectories(parent)){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("AdhdCompiler: failed to create {} directory for '{}'"), kindLabel, ConvertString(rawPath));
            return false;
        }
        return true;
    };
    if(!ensureOptionalParentDir(emitCppExternsOutPath, options.emitCppExternsPath, FOUNDATION_TEXT("output")))
        return false;
    if(!ensureOptionalParentDir(depfileOutPath, options.depfilePath, FOUNDATION_TEXT("depfile")))
        return false;

    const usize totalOutputs = outputPaths.size();
    MVector<MStringView> outputContents(arena);
    outputContents.reserve(totalOutputs);
    for(usize i = 0u; i < options.inputs.size(); ++i){
        outputContents.push_back(generatedHeaders[i]);
        outputContents.push_back(generatedSources[i]);
    }
    if(!options.emitCppExternsPath.empty())
        outputContents.push_back(emittedCppExterns);
    if(!options.depfilePath.empty())
        outputContents.push_back(combinedDepContent);

    AdhdOutputTransaction transaction(arena, outputPaths, outputContents, options.inputs, options.depfilePath, options.emitCppExternsPath);
    return transaction.publish();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_COMPILER_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

