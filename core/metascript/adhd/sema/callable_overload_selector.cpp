// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "callable_overload_selector.h"
#include "type_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] i32 CallableOverloadSelector::ScoreCandidateParams(const SymbolTable& symbols, const MethodDecl& candidate, const usize* preferredArgCount, const Span<const MStringView> argTypeNames, const Span<const u8> argIsMut)noexcept{
    i32 score = 0;
    if(!preferredArgCount || candidate.params.size() == *preferredArgCount)
        score = s_OverloadExactArityScore;
    else if(candidate.hasDefaultArgs && *preferredArgCount >= candidate.minArgCount && *preferredArgCount <= candidate.params.size())
        score = s_OverloadCompatibleArityScore;
    score += ScoreCallableParams(symbols, candidate.params, argTypeNames, argIsMut);
    return score;
}

[[nodiscard]] b8 CallableOverloadSelector::ConsiderScoredCallableCandidate(
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
        if(candidate.hasDefaultArgs && *preferredArgCount >= candidate.minArgCount && *preferredArgCount <= candidate.params.size()){
            if(inOutBestScore < s_OverloadCompatibleArityScore){
                inOutBestScore = s_OverloadCompatibleArityScore;
                inOutBestMatch = &candidate;
            }
            return false;
        }
        if(!inOutBestMatch)
            inOutBestMatch = &candidate;
        return false;
    }
    const i32 score = ScoreCandidateParams(symbols, candidate, preferredArgCount, argTypeNames, argIsMut);
    if(score > inOutBestScore){
        inOutBestScore = score;
        inOutBestMatch = &candidate;
    }
    return false;
}

[[nodiscard]] const MethodDecl* CallableOverloadSelector::FindScoredClassCallable(
    const SymbolTable& symbols,
    const ClassDecl& classDecl,
    const MStringView name,
    const b8 isOperator,
    const usize* preferredArgCount,
    const b8 preferConst,
    const b8 requirePublic,
    const Span<const MStringView> argTypeNames,
    const Span<const u8> argIsMut,
    const b8 staticOnly
)noexcept{
    const MethodDecl* bestMatch = nullptr;
    i32 bestScore = -1;
    for(const MethodDecl* m : classDecl.methods){
        if(!m || m->isConstructor || m->isDestructor || m->isOperator != isOperator || m->name != name)
            continue;
        if(staticOnly && !m->isStatic)
            continue;
        i32 score = ScoreCandidateParams(symbols, *m, preferredArgCount, argTypeNames, argIsMut);
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

const MethodDecl* CallableOverloadSelector::FindClassConstructor(const SymbolTable& symbols, const ClassDecl& classDecl, const usize argCount, const b8 requirePublic, const Span<const MStringView> argTypeNames, const Span<const u8> argIsMut)noexcept{
    const MethodDecl* bestMatch = nullptr;
    i32 bestScore = -1;
    for(const MethodDecl* m : classDecl.methods){
        if(!m || !m->isConstructor)
            continue;
        const b8 exactArity = (m->params.size() == argCount);
        const b8 compatArity = !exactArity && m->hasDefaultArgs && argCount >= m->minArgCount && argCount <= m->params.size();
        if(!exactArity && !compatArity)
            continue;
        i32 score = exactArity ? s_OverloadExactArityScore : s_OverloadCompatibleArityScore;
        score += (!requirePublic || m->access == AccessSpecifier::Public) ? s_OverloadPublicBonusScore : s_OverloadConstMatchScore;
        score += ScoreCallableParams(symbols, m->params, argTypeNames, argIsMut);
        if(score > bestScore){
            bestScore = score;
            bestMatch = m;
        }
    }
    return bestMatch;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

