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

bool ShaderSourceDependencies::ExtractIncludeDirective(const AStringView line, AStringView& outIncludeName, ShaderSourceDependencies::IncludeKind::Enum& outKind)noexcept{
    outIncludeName = {};
    outKind = ShaderSourceDependencies::IncludeKind::Relative;

    usize cursor = 0u;
    while(cursor < line.size() && IsAsciiSpace(line[cursor]))
        ++cursor;
    if(cursor >= line.size() || line[cursor] != '#')
        return false;
    ++cursor;
    while(cursor < line.size() && IsAsciiSpace(line[cursor]))
        ++cursor;
    const usize directiveBegin = cursor;
    while(cursor < line.size() && IsAsciiIdentifierChar(line[cursor]))
        ++cursor;
    const AStringView directive = line.substr(directiveBegin, cursor - directiveBegin);
    if(directive == "include_next" || directive == "import"){
        outKind = ShaderSourceDependencies::IncludeKind::Unsupported;
        outIncludeName = TrimView(line.substr(cursor));
        return true;
    }
    if(directive != AStringView("include"))
        return false;
    while(cursor < line.size() && IsAsciiSpace(line[cursor]))
        ++cursor;
    if(cursor >= line.size()){
        outKind = ShaderSourceDependencies::IncludeKind::Unsupported;
        return true;
    }

    char closingDelimiter = '"';
    if(line[cursor] == '"')
        outKind = ShaderSourceDependencies::IncludeKind::Relative;
    else if(line[cursor] == '<'){
        outKind = ShaderSourceDependencies::IncludeKind::Standard;
        closingDelimiter = '>';
    }
    else{
        outKind = ShaderSourceDependencies::IncludeKind::Macro;
        outIncludeName = TrimView(line.substr(cursor));
        return true;
    }
    ++cursor;
    const usize closingDelimiterPos = line.find(closingDelimiter, cursor);
    if(closingDelimiterPos == AStringView::npos || closingDelimiterPos <= cursor){
        outKind = ShaderSourceDependencies::IncludeKind::Unsupported;
        return true;
    }
    outIncludeName = line.substr(cursor, closingDelimiterPos - cursor);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ShaderSourceDependencies::ResolveIncludeFile(const AStringView includeName, const ShaderSourceDependencies::IncludeKind::Enum kind, const Path& sourceDirectory, const ShaderCook::CookVector<Path>& includeDirectories, Path& outPath){
    ErrorCode errorCode;

    if(kind == ShaderSourceDependencies::IncludeKind::Relative){
        const Path localCandidate = (sourceDirectory / includeName).lexicallyNormal();
        errorCode.clear();
        if(IsRegularFile(localCandidate, errorCode)){
            outPath = localCandidate;
            return true;
        }
        if(errorCode && !IsMissingPathError(errorCode)){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to query include candidate '{}': {}")
                , PathToString<tchar>(localCandidate)
                , StringConvert(errorCode.message())
            );
            return false;
        }
    }

    for(const Path& includeDirectory : includeDirectories){
        const Path includeCandidate = (includeDirectory / includeName).lexicallyNormal();
        errorCode.clear();
        if(IsRegularFile(includeCandidate, errorCode)){
            outPath = includeCandidate;
            return true;
        }
        if(errorCode && !IsMissingPathError(errorCode)){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to query include candidate '{}': {}")
                , PathToString<tchar>(includeCandidate)
                , StringConvert(errorCode.message())
            );
            return false;
        }
    }

    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

