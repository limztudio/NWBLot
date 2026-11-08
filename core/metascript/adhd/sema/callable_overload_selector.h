// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "symbol_table.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


class CallableOverloadSelector final{
public:
    [[nodiscard]] static b8 ConsiderScoredCallableCandidate(const SymbolTable& symbols, const usize* preferredArgCount, Span<const MStringView> argTypeNames, Span<const u8> argIsMut, MethodDecl& candidate, MethodDecl*& inOutBestMatch, i32& inOutBestScore)noexcept;
    [[nodiscard]] static const MethodDecl* FindScoredClassCallable(const SymbolTable& symbols, const ClassDecl& classDecl, MStringView name, b8 isOperator, const usize* preferredArgCount, b8 preferConst, b8 requirePublic, Span<const MStringView> argTypeNames, Span<const u8> argIsMut)noexcept;
    [[nodiscard]] static const MethodDecl* FindClassConstructor(const SymbolTable& symbols, const ClassDecl& classDecl, usize argCount, b8 requirePublic, Span<const MStringView> argTypeNames, Span<const u8> argIsMut)noexcept;

private:
    [[nodiscard]] static i32 ScoreCandidateParams(const SymbolTable& symbols, Span<const ParamDecl> params, const usize* preferredArgCount, Span<const MStringView> argTypeNames, Span<const u8> argIsMut)noexcept;
    static constexpr i32 s_OverloadExactArityScore = 64;
    static constexpr i32 s_OverloadPublicBonusScore = 32;
    static constexpr i32 s_OverloadConstMatchScore = 1;
};


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
