// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "callable_overload_selector.h"
#include "type_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


[[nodiscard]] i32 CallableOverloadSelector::scoreCandidateParams(const SymbolTable& symbols, const Span<const ParamDecl> params, const usize* preferredArgCount, const Span<const MStringView> argTypeNames, const Span<const u8> argIsMut)noexcept{
    i32 score = (!preferredArgCount || params.size() == *preferredArgCount) ? s_OverloadExactArityScore : 0;
    score += ScoreCallableParams(symbols, params, argTypeNames, argIsMut);
    return score;
}

[[nodiscard]] b8 CallableOverloadSelector::considerScoredCallableCandidate(
    const SymbolTable& symbols,
    const usize* preferredArgCount,
    const Span<const MStringView> argTypeNames,
    const Span<const u8> argIsMut,
    MethodDecl& candidate,
    MethodDecl*& inOutBestMatch,
    i32& inOutBestScore
)noexcept{
    if(argTypeNames.empty() && argIsMut.empty()){
        if(!preferredArgCount || candidate.params.size() == *preferredArgCount){
            inOutBestMatch = &candidate;
            return true;
        }
        if(!inOutBestMatch)
            inOutBestMatch = &candidate;
        return false;
    }
    const i32 score = scoreCandidateParams(symbols, candidate.params, preferredArgCount, argTypeNames, argIsMut);
    if(score > inOutBestScore){
        inOutBestScore = score;
        inOutBestMatch = &candidate;
    }
    return false;
}

[[nodiscard]] const MethodDecl* CallableOverloadSelector::findScoredClassCallable(
    const SymbolTable& symbols,
    const ClassDecl& classDecl,
    const MStringView name,
    const b8 isOperator,
    const usize* preferredArgCount,
    const b8 preferConst,
    const b8 requirePublic,
    const Span<const MStringView> argTypeNames,
    const Span<const u8> argIsMut
)noexcept{
    const MethodDecl* bestMatch = nullptr;
    i32 bestScore = -1;
    for(const MethodDecl* m : classDecl.methods){
        if(!m || m->isConstructor || m->isDestructor || m->isOperator != isOperator || m->name != name)
            continue;
        i32 score = scoreCandidateParams(symbols, m->params, preferredArgCount, argTypeNames, argIsMut);
        if(!requirePublic || m->access == AccessSpecifier::Public)
            score += s_OverloadPublicBonusScore;
        if(m->isConst == preferConst)
            score += s_OverloadConstMatchScore;
        if(score > bestScore){
            bestScore = score;
            bestMatch = m;
        }
    }
    return bestMatch;
}

const MethodDecl* CallableOverloadSelector::findClassConstructor(const SymbolTable& symbols, const ClassDecl& classDecl, const usize argCount, const b8 requirePublic, const Span<const MStringView> argTypeNames, const Span<const u8> argIsMut)noexcept{
    const MethodDecl* bestMatch = nullptr;
    i32 bestScore = -1;
    for(const MethodDecl* m : classDecl.methods){
        if(!m || !m->isConstructor || m->params.size() != argCount)
            continue;
        i32 score = (!requirePublic || m->access == AccessSpecifier::Public) ? s_OverloadPublicBonusScore : s_OverloadConstMatchScore;
        score += ScoreCallableParams(symbols, m->params, argTypeNames, argIsMut);
        if(score > bestScore){
            bestScore = score;
            bestMatch = m;
        }
    }
    return bestMatch;
}


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
