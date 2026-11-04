// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "source_dependencies.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Alloc = Core::Alloc;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<::Path<Core::Alloc::ScratchArena>> ShaderSourceDependencies::ResolveIncludeFile(
    const AStringView includeName,
    const ShaderBinaryPayload::IncludeKind::Enum kind,
    const TStringView sourceDirectory,
    const Span<const Path> includeDirectories,
    Core::Alloc::ScratchArena& scratchArena
){

    if(kind == ShaderBinaryPayload::IncludeKind::Relative){
        ::Path<Alloc::ScratchArena> localCandidate = (::Path<Alloc::ScratchArena>(scratchArena, sourceDirectory) / includeName).lexicallyNormal();
        const auto localCandidateQueryResult = IsRegularFile(localCandidate);
        if(localCandidateQueryResult && *localCandidateQueryResult){
            return localCandidate;
        }
        if(!localCandidateQueryResult && !IsMissingPathError(localCandidateQueryResult.error())){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to query include candidate '{}': {}"), PathToString<tchar>(localCandidate), ConvertString(localCandidateQueryResult.error().message()));
            return MakeUnexpected(Failure{});
        }
    }

    for(const Path& includeDirectory : includeDirectories){
        ::Path<Alloc::ScratchArena> includeCandidate = (::Path<Alloc::ScratchArena>(scratchArena, includeDirectory) / includeName).lexicallyNormal();
        const auto includeCandidateQueryResult = IsRegularFile(includeCandidate);
        if(includeCandidateQueryResult && *includeCandidateQueryResult){
            return includeCandidate;
        }
        if(!includeCandidateQueryResult && !IsMissingPathError(includeCandidateQueryResult.error())){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to query include candidate '{}': {}"), PathToString<tchar>(includeCandidate), ConvertString(includeCandidateQueryResult.error().message()));
            return MakeUnexpected(Failure{});
        }
    }

    return MakeUnexpected(Failure{});
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

