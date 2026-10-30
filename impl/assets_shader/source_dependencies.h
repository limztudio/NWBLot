// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "cook.h"

#include <core/assets/paths.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ShaderSourceDependencies{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace IncludeKind = ::NWB::Impl::ShaderBinaryPayload::IncludeKind;
using IncludeDirective = ::NWB::Impl::ShaderBinaryPayload::IncludeDirective;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void SpliceSourceLines(AString<Core::Alloc::ScratchArena>& inOutSource)noexcept;
void MaskSourceComments(AString<Core::Alloc::ScratchArena>& inOutSource)noexcept;

[[nodiscard]] Expected<IncludeDirective> ExtractIncludeDirective(AStringView line)noexcept;
[[nodiscard]] Expected<::Path<Core::Alloc::ScratchArena>> ResolveIncludeFile(
    AStringView includeName,
    IncludeKind::Enum kind,
    TStringView sourceDirectory,
    Span<const Path> includeDirectories,
    Core::Alloc::ScratchArena& scratchArena
);

template<typename SourceArenaT, typename DependencyVectorT>
[[nodiscard]] b8 Gather(
    const ::Path<SourceArenaT>& sourcePath,
    Span<const Path> includeDirectories,
    const Span<const AStringView> externallyPlannedMacroIncludes,
    Core::Alloc::ScratchArena& scratchArena,
    DependencyVectorT& outDependencies
){
    using ScratchArena = Core::Alloc::ScratchArena;
    using ScratchPath = ::Path<ScratchArena>;
    using ScratchString = AString<ScratchArena>;

    outDependencies.clear();
    HashSet<ScratchString, ScratchArena, Hasher<ScratchString>, EqualTo<ScratchString>> visited{ 0, Hasher<ScratchString>(), EqualTo<ScratchString>(), scratchArena };
    Deque<ScratchPath, ScratchArena> pending{scratchArena};
    pending.emplace_back(scratchArena, sourcePath);
    ScratchString sourceText{scratchArena};

    while(!pending.empty()){
        ScratchPath dependencyPath = Move(pending.back());
        pending.pop_back();

        const auto absolutePathResult = AbsolutePath(dependencyPath);
        if(!absolutePathResult){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to resolve dependency path '{}' : {}"), PathToString<tchar>(dependencyPath), ConvertString(absolutePathResult.error().message()));
            return false;
        }
        const ScratchPath absolutePath = absolutePathResult->lexicallyNormal();

        ScratchString canonicalPathKey = PathToString(scratchArena, absolutePath);
#if defined(NWB_PLATFORM_WINDOWS)
        CanonicalizeTextInPlace(canonicalPathKey);
#endif
        if(!visited.insert(Move(canonicalPathKey)).second)
            continue;

        sourceText.clear();
        const auto sourceRead = ReadTextFile(absolutePath, sourceText);
        if(!sourceRead){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to read dependency '{}': {}:{}"), PathToString<tchar>(absolutePath), ConvertString(sourceRead.error().category().name()), sourceRead.error().value());
            return false;
        }
        StripUtf8Bom(sourceText);
        ShaderSourceDependencies::SpliceSourceLines(sourceText);
        ShaderSourceDependencies::MaskSourceComments(sourceText);

        outDependencies.emplace_back(outDependencies.get_allocator().arena(), absolutePath);

        const AStringView sourceView(sourceText.data(), sourceText.size());
        usize lineBegin = 0;
        while(lineBegin < sourceView.size()){
            usize lineEnd = lineBegin;
            while(lineEnd < sourceView.size() && sourceView[lineEnd] != '\n')
                ++lineEnd;

            AStringView line = sourceView.substr(lineBegin, lineEnd - lineBegin);
            if(!line.empty() && line.back() == '\r')
                line.remove_suffix(1);

            if(const auto include = ShaderSourceDependencies::ExtractIncludeDirective(line)){
                const AStringView includeName = include->name;
                const auto includeKind = include->kind;
                if(includeKind == ShaderSourceDependencies::IncludeKind::Macro || includeKind == ShaderSourceDependencies::IncludeKind::Unsupported){
                    const b8 plannedMacro = includeKind == ShaderSourceDependencies::IncludeKind::Macro && FindIf(externallyPlannedMacroIncludes.begin(), externallyPlannedMacroIncludes.end(), [includeName](const AStringView name)noexcept{
                        return name == includeName;
                    }
                    ) != externallyPlannedMacroIncludes.end();
                    if(plannedMacro){
                        lineBegin = lineEnd + 1u;
                        continue;
                    }
                    NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: unsupported or untracked dependency directive '{}' in '{}'"), ConvertString(TrimView(line)), PathToString<tchar>(absolutePath));
                    return false;
                }
                auto includePath = ShaderSourceDependencies::ResolveIncludeFile(includeName, includeKind, absolutePath.parentPathView(), includeDirectories, scratchArena);
                if(!includePath){
                    NWB_LOGGER_ERROR(NWB_TEXT("Unable to resolve include '{}' from '{}'"), ConvertString(includeName), PathToString<tchar>(absolutePath));
                    return false;
                }

                pending.push_back(Move(*includePath));
            }

            lineBegin = lineEnd + 1;
        }
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
