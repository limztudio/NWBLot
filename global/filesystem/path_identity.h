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
#if defined(FOUNDATION_PLATFORM_WINDOWS)
        const HANDLE lhsHandle = CreateFile(
            m_lhs.c_str(),
            0u,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS,
            nullptr
        );
        if(lhsHandle == INVALID_HANDLE_VALUE){
            const ErrorCode lhsError = LastSystemError();
            if(IsMissingOrInvalidPathLookupError(lhsError))
                return false;
            return MakeUnexpected(lhsError);
        }
        const HANDLE rhsHandle = CreateFile(
            m_rhs.c_str(),
            0u,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS,
            nullptr
        );
        if(rhsHandle == INVALID_HANDLE_VALUE){
            const ErrorCode rhsError = LastSystemError();
            const b8 lhsClosed = CloseHandle(lhsHandle) != 0;
            const ErrorCode closeError = lhsClosed ? ErrorCode{} : LastSystemError();
            if(IsMissingOrInvalidPathLookupError(rhsError)){
                if(closeError)
                    return MakeUnexpected(closeError);
                return false;
            }
            return MakeUnexpected(rhsError);
        }

        b8 same = false;
        ErrorCode queryError;
        FILE_ID_INFO lhsId = {};
        FILE_ID_INFO rhsId = {};
        if(
            GetFileInformationByHandleEx(lhsHandle, FileIdInfo, &lhsId, sizeof(lhsId))
            && GetFileInformationByHandleEx(rhsHandle, FileIdInfo, &rhsId, sizeof(rhsId))
        ){
            same = lhsId.VolumeSerialNumber == rhsId.VolumeSerialNumber
                && FOUNDATION_MEMCMP(&lhsId.FileId, &rhsId.FileId, sizeof(lhsId.FileId)) == 0;
        }
        else{
            BY_HANDLE_FILE_INFORMATION lhsInfo = {};
            BY_HANDLE_FILE_INFORMATION rhsInfo = {};
            if(GetFileInformationByHandle(lhsHandle, &lhsInfo) && GetFileInformationByHandle(rhsHandle, &rhsInfo)){
                same = lhsInfo.dwVolumeSerialNumber == rhsInfo.dwVolumeSerialNumber
                    && lhsInfo.nFileIndexHigh == rhsInfo.nFileIndexHigh
                    && lhsInfo.nFileIndexLow == rhsInfo.nFileIndexLow;
            }
            else
                queryError = LastSystemError();
        }

        const b8 rhsClosed = CloseHandle(rhsHandle) != 0;
        const ErrorCode rhsCloseError = rhsClosed ? ErrorCode{} : LastSystemError();
        const b8 lhsClosed = CloseHandle(lhsHandle) != 0;
        const ErrorCode lhsCloseError = lhsClosed ? ErrorCode{} : LastSystemError();
        if(queryError)
            return MakeUnexpected(queryError);
        if(rhsCloseError)
            return MakeUnexpected(rhsCloseError);
        if(lhsCloseError)
            return MakeUnexpected(lhsCloseError);
        return same;
#else
        const auto lhsStat = GlobalFilesystemDetail::StatPath(m_lhs);
        if(!lhsStat){
            if(IsMissingOrInvalidPathLookupError(lhsStat.error()))
                return false;
            return MakeUnexpected(lhsStat.error());
        }
        const auto rhsStat = GlobalFilesystemDetail::StatPath(m_rhs);
        if(!rhsStat){
            if(IsMissingOrInvalidPathLookupError(rhsStat.error()))
                return false;
            return MakeUnexpected(rhsStat.error());
        }
        return lhsStat->st_dev == rhsStat->st_dev && lhsStat->st_ino == rhsStat->st_ino;
#endif
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

