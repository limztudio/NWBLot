// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "source_dependencies.h"

#include <core/common/log.h>


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
    bool blockComment = false;
    bool lineComment = false;
    bool escaped = false;
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

Expected<ShaderSourceDependencies::IncludeDirective> ShaderSourceDependencies::ExtractIncludeDirective(const AStringView line)noexcept{
    IncludeDirective include;

    usize cursor = 0u;
    while(cursor < line.size() && IsAsciiSpace(line[cursor]))
        ++cursor;
    if(cursor >= line.size() || line[cursor] != '#')
        return MakeUnexpected(Failure{});
    ++cursor;
    while(cursor < line.size() && IsAsciiSpace(line[cursor]))
        ++cursor;
    const usize directiveBegin = cursor;
    while(cursor < line.size() && IsAsciiIdentifierChar(line[cursor]))
        ++cursor;
    const AStringView directive = line.substr(directiveBegin, cursor - directiveBegin);
    if(directive == "include_next" || directive == "import"){
        include.kind = ShaderSourceDependencies::IncludeKind::Unsupported;
        include.name = TrimView(line.substr(cursor));
        return include;
    }
    if(directive != AStringView("include"))
        return MakeUnexpected(Failure{});
    while(cursor < line.size() && IsAsciiSpace(line[cursor]))
        ++cursor;
    if(cursor >= line.size()){
        include.kind = ShaderSourceDependencies::IncludeKind::Unsupported;
        return include;
    }

    char closingDelimiter = '"';
    if(line[cursor] == '"')
        include.kind = ShaderSourceDependencies::IncludeKind::Relative;
    else if(line[cursor] == '<'){
        include.kind = ShaderSourceDependencies::IncludeKind::Standard;
        closingDelimiter = '>';
    }
    else{
        include.kind = ShaderSourceDependencies::IncludeKind::Macro;
        include.name = TrimView(line.substr(cursor));
        return include;
    }
    ++cursor;
    const usize closingDelimiterPos = line.find(closingDelimiter, cursor);
    if(closingDelimiterPos == AStringView::npos || closingDelimiterPos <= cursor){
        include.kind = ShaderSourceDependencies::IncludeKind::Unsupported;
        return include;
    }
    include.name = line.substr(cursor, closingDelimiterPos - cursor);
    return include;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Path> ShaderSourceDependencies::ResolveIncludeFile(const AStringView includeName, const ShaderSourceDependencies::IncludeKind::Enum kind, const Path& sourceDirectory, const ShaderCook::CookVector<Path>& includeDirectories){

    if(kind == ShaderSourceDependencies::IncludeKind::Relative){
        const Path localCandidate = (sourceDirectory / includeName).lexicallyNormal();
        const auto localCandidateQueryResult = IsRegularFile(localCandidate);
        if(localCandidateQueryResult && *localCandidateQueryResult){
            return localCandidate;
        }
        if(!localCandidateQueryResult && !IsMissingPathError(localCandidateQueryResult.error())){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to query include candidate '{}': {}")
                , PathToString<tchar>(localCandidate)
                , StringConvert(localCandidateQueryResult.error().message())
            );
            return MakeUnexpected(Failure{});
        }
    }

    for(const Path& includeDirectory : includeDirectories){
        const Path includeCandidate = (includeDirectory / includeName).lexicallyNormal();
        const auto includeCandidateQueryResult = IsRegularFile(includeCandidate);
        if(includeCandidateQueryResult && *includeCandidateQueryResult){
            return includeCandidate;
        }
        if(!includeCandidateQueryResult && !IsMissingPathError(includeCandidateQueryResult.error())){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to query include candidate '{}': {}")
                , PathToString<tchar>(includeCandidate)
                , StringConvert(includeCandidateQueryResult.error().message())
            );
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

