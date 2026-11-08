// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "csg_evaluator_dependencies.h"
#include "csg_shader_variants.h"

#include <impl/assets_shader/source_dependencies.h>

#include <core/common/log.h>

#include <global/text_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AssetsGraphicsCookDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using Core::Assets::ScratchArena;
using Core::Assets::ScratchString;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<::Path<ScratchArena>> CsgEvaluatorDependencyCollector::resolve(const AStringView includeName, const Span<const Path> includeDirectories){
    if(includeName.empty())
        return MakeUnexpected(Failure{});

    const ::Path<ScratchArena> includePath(m_scratchArena, includeName);
    if(includePath.isAbsolute()){
        const auto includePathQueryResult = IsRegularFile(includePath);
        if(includePathQueryResult && *includePathQueryResult)
            return includePath.lexicallyNormal();
        if(!includePathQueryResult && !IsMissingPathError(includePathQueryResult.error())){
            NWB_LOGGER_ERROR(NWB_TEXT("GraphicsCook: failed to query CSG evaluator module include '{}': {}"), PathToString<tchar>(includePath), ConvertString(includePathQueryResult.error().message()));
            return MakeUnexpected(Failure{});
        }
    }

    for(const Path& includeDirectory : includeDirectories){
        const ::Path<ScratchArena> candidate = (::Path<ScratchArena>(m_scratchArena, includeDirectory) / includePath).lexicallyNormal();
        const auto candidateQueryResult = IsRegularFile(candidate);
        if(candidateQueryResult && *candidateQueryResult)
            return candidate;
        if(!candidateQueryResult && !IsMissingPathError(candidateQueryResult.error())){
            NWB_LOGGER_ERROR(NWB_TEXT("GraphicsCook: failed to query CSG evaluator module include '{}': {}"), PathToString<tchar>(candidate), ConvertString(candidateQueryResult.error().message()));
            return MakeUnexpected(Failure{});
        }
    }

    NWB_LOGGER_ERROR(NWB_TEXT("GraphicsCook: failed to resolve CSG evaluator module include '{}'"), ConvertString(includeName));
    return MakeUnexpected(Failure{});
}


b8 CsgEvaluatorDependencyCollector::appendUnique(const ::Path<ScratchArena>& dependency, DependencyPathScratchSet& inOutSeenDependencies){
    auto absoluteDependencyResult = AbsolutePath(dependency);
    if(!absoluteDependencyResult){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsCook: failed to resolve CSG evaluator module dependency '{}': {}"), PathToString<tchar>(dependency), ConvertString(absoluteDependencyResult.error().message()));
        return false;
    }
    const ::Path<ScratchArena> absoluteDependency = absoluteDependencyResult->lexicallyNormal();
    ScratchString canonicalPath = PathToString(m_scratchArena, absoluteDependency);
#if defined(NWB_PLATFORM_WINDOWS)
    CanonicalizeTextInPlace(canonicalPath);
#endif
    if(inOutSeenDependencies.insert(Move(canonicalPath)).second)
        m_dependencies.emplace_back(m_dependencies.get_allocator().arena(), absoluteDependency);
    return true;
}


b8 CsgEvaluatorDependencyCollector::collect(const ShaderCook::ShaderEntry& entry, const Span<const Path> includeDirectories){
    const auto foundDefine = entry.defineValues.find(s_CsgProjectEvaluatorModuleDefineName);
    if(foundDefine == entry.defineValues.end())
        return true;

    DependencyPathScratchSet seenDependencies(0, Hasher<ScratchString>(), EqualTo<ScratchString>(), m_scratchArena);
    seenDependencies.reserve(m_dependencies.size());
    // Dependency collection already produces normalized absolute paths; retain its first-seen ordering.
    for(const Path& dependency : m_dependencies){
        ScratchString canonicalPath = PathToString(m_scratchArena, dependency);
#if defined(NWB_PLATFORM_WINDOWS)
        CanonicalizeTextInPlace(canonicalPath);
#endif
        if(seenDependencies.contains(canonicalPath))
            continue;
        if(!seenDependencies.insert(Move(canonicalPath)).second){
            NWB_ASSERT(false);
            return false;
        }
    }

    Vector<::Path<ScratchArena>, ScratchArena> moduleDependencies(m_scratchArena);
    for(const ShaderCook::CookString& defineValue : foundDefine.value().values){
        const AStringView includeName = UnquoteDoubleQuotedView(AStringView(defineValue));
        if(includeName.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("GraphicsCook: CSG evaluator module define value '{}' must be a quoted include path"), ConvertString(defineValue));
            return false;
        }

        const auto modulePath = resolve(includeName, includeDirectories);
        if(!modulePath)
            return false;

        moduleDependencies.clear();
        if(!ShaderSourceDependencies::Gather(*modulePath, includeDirectories, AssetsGraphicsCsgShaderVariants::s_ExternallyPlannedMacroIncludes, m_scratchArena, moduleDependencies))
            return false;
        for(const ::Path<ScratchArena>& dependency : moduleDependencies){
            if(!appendUnique(dependency, seenDependencies))
                return false;
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

