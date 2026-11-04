// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "operations.h"

#include "../containers.h"
#include "../text_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
[[nodiscard]] inline Expected<Path<ArenaT>, ErrorCode> ResolveComparisonPath(const Path<ArenaT>& path){
#if defined(NWB_PLATFORM_WINDOWS)
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
#if defined(NWB_PLATFORM_WINDOWS)
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
#if defined(NWB_PLATFORM_WINDOWS)
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
[[nodiscard]] inline Expected<b8, ErrorCode> SameOutputPath(const Path<ArenaT>& lhs, const Path<ArenaT>& rhs){
    const auto sameFullText = SamePathComponentText(lhs.native(), rhs.native());
    if(!sameFullText)
        return MakeUnexpected(sameFullText.error());
    if(*sameFullText)
        return true;

    const auto sameExisting = AreSameExistingPath(lhs, rhs);
    if(!sameExisting)
        return MakeUnexpected(sameExisting.error());
    if(*sameExisting)
        return true;

    Path<ArenaT> lhsCur = lhs;
    Path<ArenaT> rhsCur = rhs;
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

        const auto sameParentExisting = AreSameExistingPath(lhsCur, rhsCur);
        if(!sameParentExisting)
            return MakeUnexpected(sameParentExisting.error());
        if(*sameParentExisting)
            return true;
    }
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

