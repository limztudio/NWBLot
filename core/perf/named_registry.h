// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

#include <global/expected.h>
#include <global/limit.h>
#include <global/name.h>
#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_PERF_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ScopeId, typename ScopeVector, typename ScopeMap, typename CreateScope, typename ConfigureScope>
[[nodiscard]] Expected<ScopeId> RegisterNamedScope(ScopeVector& scopes, ScopeMap& scopeMap, const u32 generation, const Name& scopeName, CreateScope&& createScope, ConfigureScope&& configureScope){
    if(!scopeName || generation == 0u)
        return MakeUnexpected(Failure{});

    const auto found = scopeMap.find(scopeName);
    if(found != scopeMap.end())
        return found.value();

    if(scopes.size() >= static_cast<usize>(Limit<u32>::s_Max))
        return MakeUnexpected(Failure{});

    auto scope = createScope(scopeName);
    if(!scope)
        return MakeUnexpected(Failure{});

    ScopeId scopeId;
    scopeId.index = static_cast<u32>(scopes.size());
    scopeId.generation = generation;
    configureScope(*scope, scopeId);

    scopes.push_back(Move(scope));
    ScopeExit discardScope([&]()noexcept(noexcept(scopes.pop_back())){ scopes.pop_back(); });
    if(!scopeMap.try_emplace(scopeName, scopeId).second){
        NWB_ASSERT(false);
        return MakeUnexpected(Failure{});
    }
    discardScope.release();
    return scopeId;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Generation-stamped scope lookup shared by memory and timing recorders.


template<typename ScopeId, typename ScopeVector>
[[nodiscard]] auto FindNamedScope(const ScopeVector& scopes, const ScopeId scope)noexcept(
    noexcept(DeclVal<ScopeId&>().~ScopeId()) && noexcept(static_cast<bool>(!scope.valid() || scope.index >= scopes.size())) &&
    noexcept(scopes[scope.index].get()) && noexcept(static_cast<bool>(!scopes[scope.index].get() || scopes[scope.index].get()->generation != scope.generation))
) -> RemoveCVRef_T<decltype(scopes[scope.index].get())>{
    if(!scope.valid() || scope.index >= scopes.size())
        return nullptr;

    auto* record = scopes[scope.index].get();
    if(!record || record->generation != scope.generation)
        return nullptr;

    return record;
}

template<typename ScopeId, typename ScopeVector>
[[nodiscard]] ScopeId ScopeAt(const ScopeVector& scopes, const usize index)noexcept(
    noexcept(ScopeId{}) && noexcept(ScopeId(DeclVal<ScopeId&&>())) && noexcept(static_cast<bool>(index >= scopes.size())) && noexcept(scopes[index].get()) &&
    noexcept(DeclVal<ScopeId&>().index = static_cast<u32>(index)) && noexcept(DeclVal<ScopeId&>().generation = scopes[index].get()->generation)
){
    if(index >= scopes.size())
        return {};

    auto* record = scopes[index].get();
    if(!record)
        return {};

    ScopeId scope;
    scope.index = static_cast<u32>(index);
    scope.generation = record->generation;
    return scope;
}

template<typename ScopeVector>
[[nodiscard]] Name ScopeNameAt(const ScopeVector& scopes, const usize index)noexcept(noexcept(static_cast<bool>(index >= scopes.size() || !scopes[index])) && noexcept(DeclVal<void(*)(Name)noexcept>()(scopes[index]->name))){
    if(index >= scopes.size() || !scopes[index])
        return s_NameNone;

    return scopes[index]->name;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_PERF_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

