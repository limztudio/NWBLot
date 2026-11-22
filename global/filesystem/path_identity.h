// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "operations.h"

#include "../containers.h"
#include "../text_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
[[nodiscard]] inline Expected<Path<ArenaT>, ErrorCode> ResolveComparisonPath(const Path<ArenaT>& path){
#if defined(FOUNDATION_PLATFORM_WINDOWS)
    Vector<tchar, ArenaT> buffer(path.arena());
    buffer.reserve(GlobalFilesystemDetail::s_InitialPathBufferCapacity);
    DWORD capacity = static_cast<DWORD>(GlobalFilesystemDetail::s_InitialPathBufferCapacity);
    for(;;){
        buffer.resize(static_cast<usize>(capacity));
        const DWORD length = GetFullPathNameW(path.c_str(), capacity, buffer.data(), nullptr);
        if(length == 0u)
            return MakeUnexpected(LastSystemError());
        if(length < capacity)
            return Path<ArenaT>(path.arena(), TStringView(buffer.data(), static_cast<usize>(length))).lexicallyNormal();
        if(length == Limit<DWORD>::s_Max)
            return MakeUnexpected(MakeErrorCode(ErrorCondition::value_too_large));
        capacity = length + 1u;
    }
#else
    return AbsolutePath(path);
#endif
}


[[nodiscard]] inline Expected<b8, ErrorCode> SamePathComponentText(const TStringView lhs, const TStringView rhs)noexcept{
#if defined(FOUNDATION_PLATFORM_WINDOWS)
    const int comparison = CompareStringOrdinal(lhs.data(), static_cast<int>(lhs.size()), rhs.data(), static_cast<int>(rhs.size()), TRUE);
    if(comparison == 0)
        return MakeUnexpected(LastSystemError());
    return comparison == CSTR_EQUAL;
#else
    return lhs == rhs;
#endif
}

[[nodiscard]] inline constexpr b8 SameGenericPrefixText(const AStringView lhs, const AStringView rhs)noexcept{
    if(lhs.size() != rhs.size())
        return false;
#if defined(FOUNDATION_PLATFORM_WINDOWS)
    for(usize i = 0u; i < lhs.size(); ++i){
        if(Canonicalize(lhs[i]) != Canonicalize(rhs[i]))
            return false;
    }
    return true;
#else
    return lhs == rhs;
#endif
}

struct PathExistingFileIdentity{
    u64 volumeSerial = 0u;
    Array<u8, 16u> fileId{};
    u64 posixDev = 0u;
    u64 posixIno = 0u;
    b8 valid = false;

    [[nodiscard]] constexpr b8 operator==(const PathExistingFileIdentity& rhs)const noexcept{
        if(valid != rhs.valid)
            return false;
        if(!valid)
            return true;
        if(volumeSerial != rhs.volumeSerial || posixDev != rhs.posixDev || posixIno != rhs.posixIno)
            return false;
        for(usize i = 0u; i < fileId.size(); ++i){
            if(fileId[i] != rhs.fileId[i])
                return false;
        }
        return true;
    }
    [[nodiscard]] constexpr b8 operator!=(const PathExistingFileIdentity& rhs)const noexcept{ return !(*this == rhs); }
};

template<typename ArenaT>
[[nodiscard]] inline Expected<Optional<PathExistingFileIdentity>, ErrorCode> QueryExistingPathIdentity(const Path<ArenaT>& path)noexcept{
    if(path.empty())
        return Optional<PathExistingFileIdentity>{};
#if defined(FOUNDATION_PLATFORM_WINDOWS)
    const HANDLE handle = CreateFile(
        path.c_str(),
        0u,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        nullptr
    );
    if(handle == INVALID_HANDLE_VALUE){
        const ErrorCode error = LastSystemError();
        if(IsMissingOrInvalidPathLookupError(error))
            return Optional<PathExistingFileIdentity>{};
        return MakeUnexpected(error);
    }

    PathExistingFileIdentity identity;
    ErrorCode queryError;
    FILE_ID_INFO idInfo = {};
    if(GetFileInformationByHandleEx(handle, FileIdInfo, &idInfo, sizeof(idInfo))){
        identity.volumeSerial = idInfo.VolumeSerialNumber;
        FOUNDATION_MEMCPY(identity.fileId.data(), sizeof(identity.fileId), &idInfo.FileId, sizeof(idInfo.FileId));
        identity.valid = true;
    }
    else{
        BY_HANDLE_FILE_INFORMATION fileInfo = {};
        if(GetFileInformationByHandle(handle, &fileInfo)){
            identity.volumeSerial = fileInfo.dwVolumeSerialNumber;
            const u32 hi = fileInfo.nFileIndexHigh;
            const u32 lo = fileInfo.nFileIndexLow;
            FOUNDATION_MEMCPY(identity.fileId.data(), sizeof(hi), &hi, sizeof(hi));
            FOUNDATION_MEMCPY(identity.fileId.data() + sizeof(hi), sizeof(lo), &lo, sizeof(lo));
            identity.valid = true;
        }
        else{
            queryError = LastSystemError();
        }
    }

    const b8 closed = CloseHandle(handle) != 0;
    const ErrorCode closeError = closed ? ErrorCode{} : LastSystemError();
    if(queryError)
        return MakeUnexpected(queryError);
    if(closeError)
        return MakeUnexpected(closeError);
    return identity;
#else
    const auto pathStat = GlobalFilesystemDetail::StatPath(path);
    if(!pathStat){
        if(IsMissingOrInvalidPathLookupError(pathStat.error()))
            return Optional<PathExistingFileIdentity>{};
        return MakeUnexpected(pathStat.error());
    }
    PathExistingFileIdentity identity;
    identity.posixDev = static_cast<u64>(pathStat->st_dev);
    identity.posixIno = static_cast<u64>(pathStat->st_ino);
    identity.valid = true;
    return identity;
#endif
}

template<typename ArenaT>
struct PathOutputIdentity{
    PathExistingFileIdentity anchor;
    TString<ArenaT> tailPath;
    u64 hashValue = 0u;


public:
    explicit PathOutputIdentity(ArenaT& arena)noexcept
        : tailPath(arena)
    {}

    [[nodiscard]] Expected<b8, ErrorCode> sameAs(const PathOutputIdentity& rhs)const noexcept{
        if(anchor != rhs.anchor)
            return false;
        const TStringView lhsView(tailPath.data(), tailPath.size());
        const TStringView rhsView(rhs.tailPath.data(), rhs.tailPath.size());
        if(lhsView.empty() && rhsView.empty())
            return true;
#if defined(FOUNDATION_PLATFORM_WINDOWS)
        return SamePathComponentText(lhsView, rhsView);
#else
        return lhsView == rhsView;
#endif
    }
};

template<typename ArenaT>
[[nodiscard]] inline Expected<PathOutputIdentity<ArenaT>, ErrorCode> ResolvePathOutputIdentity(const Path<ArenaT>& path, ArenaT& arena){
    PathOutputIdentity<ArenaT> result(arena);
    Vector<TString<ArenaT>, ArenaT> missingLeaves(arena);
    Path<ArenaT> cursor = path;

    for(;;){
        const auto existing = QueryExistingPathIdentity(cursor);
        if(!existing)
            return MakeUnexpected(existing.error());
        if(existing->has_value()){
            result.anchor = existing->value();
            break;
        }
        const TStringView leaf = cursor.filenameView();
        const TStringView parentView = cursor.parentPathView();
        if(leaf.empty() || parentView.empty() || parentView == cursor.native()){
            result.tailPath.assign(cursor.native().data(), cursor.native().size());
            break;
        }
        missingLeaves.emplace_back(leaf.data(), leaf.size(), arena);
        cursor = Move(cursor).parentPath();
    }

    if(!missingLeaves.empty()){
        for(usize i = missingLeaves.size(); i > 0u; --i){
            if(!result.tailPath.empty())
                result.tailPath.push_back(FOUNDATION_TEXT('/'));
            const TString<ArenaT>& leaf = missingLeaves[i - 1u];
            result.tailPath.append(leaf.data(), leaf.size());
        }
    }

    u64 hash = s_Fnv64OffsetBasis;
    Fnv64AppendBool(hash, result.anchor.valid);
    if(result.anchor.valid){
        Fnv64AppendValue(hash, result.anchor.volumeSerial);
        Fnv64AppendValue(hash, result.anchor.posixDev);
        Fnv64AppendValue(hash, result.anchor.posixIno);
        hash = UpdateFnv64(hash, Span<const u8>(result.anchor.fileId.data(), result.anchor.fileId.size()));
    }
    const TStringView tailView(result.tailPath.data(), result.tailPath.size());
#if defined(FOUNDATION_PLATFORM_WINDOWS)
    hash = UpdateFnv64TextCanonical(hash, tailView);
#else
    hash = UpdateFnv64TextExact(hash, tailView);
#endif
    result.hashValue = hash;
    return result;
}

template<typename ArenaT>
class PathIdentity final{
public:
    PathIdentity(const Path<ArenaT>& lhs, const Path<ArenaT>& rhs)noexcept
        : m_lhs(lhs)
        , m_rhs(rhs)
    {}


public:
    [[nodiscard]] Expected<b8, ErrorCode> sameExisting()const noexcept{
        if(m_lhs.empty() || m_rhs.empty())
            return false;
        const auto lhsId = QueryExistingPathIdentity(m_lhs);
        if(!lhsId)
            return MakeUnexpected(lhsId.error());
        if(!lhsId->has_value())
            return false;
        const auto rhsId = QueryExistingPathIdentity(m_rhs);
        if(!rhsId)
            return MakeUnexpected(rhsId.error());
        if(!rhsId->has_value())
            return false;
        return lhsId->value() == rhsId->value();
    }

    [[nodiscard]] Expected<b8, ErrorCode> sameOutput()const{
        const auto sameFullText = SamePathComponentText(m_lhs.native(), m_rhs.native());
        if(!sameFullText)
            return MakeUnexpected(sameFullText.error());
        if(*sameFullText)
            return true;

        const auto sameExisting = this->sameExisting();
        if(!sameExisting)
            return MakeUnexpected(sameExisting.error());
        if(*sameExisting)
            return true;

        Path<ArenaT> lhsCur = m_lhs;
        Path<ArenaT> rhsCur = m_rhs;
        for(;;){
            const TStringView lhsLeaf = lhsCur.filenameView();
            const TStringView rhsLeaf = rhsCur.filenameView();
            if(lhsLeaf.empty() || rhsLeaf.empty())
                break;
            const auto sameLeaf = SamePathComponentText(lhsLeaf, rhsLeaf);
            if(!sameLeaf)
                return MakeUnexpected(sameLeaf.error());
            if(!*sameLeaf)
                break;

            const TStringView lhsParentView = lhsCur.parentPathView();
            const TStringView rhsParentView = rhsCur.parentPathView();
            if(lhsParentView.empty() || rhsParentView.empty() || lhsParentView == lhsCur.native() || rhsParentView == rhsCur.native())
                break;
            lhsCur = Move(lhsCur).parentPath();
            rhsCur = Move(rhsCur).parentPath();

            const auto sameParentText = SamePathComponentText(lhsCur.native(), rhsCur.native());
            if(!sameParentText)
                return MakeUnexpected(sameParentText.error());
            if(*sameParentText)
                return true;

            const auto sameParentExisting = PathIdentity(lhsCur, rhsCur).sameExisting();
            if(!sameParentExisting)
                return MakeUnexpected(sameParentExisting.error());
            if(*sameParentExisting)
                return true;
        }
        return false;
    }


private:
    const Path<ArenaT>& m_lhs;
    const Path<ArenaT>& m_rhs;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

