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


void ShaderSourceDependencies::SpliceSourceLines(AString<Alloc::ScratchArena>& inOutSource)noexcept{
    usize writeCursor = 0u;
    for(usize readCursor = 0u; readCursor < inOutSource.size(); ++readCursor){
        if(inOutSource[readCursor] == '\\' && readCursor + 1u < inOutSource.size()){
            if(inOutSource[readCursor + 1u] == '\n'){
                ++readCursor;
                continue;
            }
            if(inOutSource[readCursor + 1u] == '\r' && readCursor + 2u < inOutSource.size() && inOutSource[readCursor + 2u] == '\n'){
                readCursor += 2u;
                continue;
            }
        }
        inOutSource[writeCursor++] = inOutSource[readCursor];
    }
    inOutSource.resize(writeCursor);
}

void ShaderSourceDependencies::MaskSourceComments(AString<Alloc::ScratchArena>& inOutSource)noexcept{
    b8 blockComment = false;
    b8 lineComment = false;
    b8 escaped = false;
    char quote = '\0';
    for(usize cursor = 0u; cursor < inOutSource.size(); ++cursor){
        char& ch = inOutSource[cursor];
        if(blockComment){
            if(ch == '*' && cursor + 1u < inOutSource.size() && inOutSource[cursor + 1u] == '/'){
                ch = ' ';
                inOutSource[++cursor] = ' ';
                blockComment = false;
            }
            else if(ch != '\r' && ch != '\n')
                ch = ' ';
            continue;
        }
        if(lineComment){
            if(ch == '\n')
                lineComment = false;
            else if(ch != '\r')
                ch = ' ';
            continue;
        }
        if(quote != '\0'){
            if(escaped)
                escaped = false;
            else if(ch == '\\')
                escaped = true;
            else if(ch == quote)
                quote = '\0';
            if(ch == '\r' || ch == '\n')
                ch = ' ';
            continue;
        }
        if(ch == '"' || ch == '\''){
            quote = ch;
            continue;
        }
        if(ch != '/' || cursor + 1u >= inOutSource.size())
            continue;
        const char next = inOutSource[cursor + 1u];
        if(next == '/' || next == '*'){
            ch = ' ';
            inOutSource[++cursor] = ' ';
            lineComment = next == '/';
            blockComment = next == '*';
        }
    }
}


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

