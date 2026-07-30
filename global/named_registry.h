// limztudio@gmail.com


#pragma once


#include "limit.h"
#include "name.h"


template<
    typename ScopeId,
    typename ScopeVector,
    typename ScopeMap,
    typename CreateScope,
    typename ConfigureScope
>
[[nodiscard]] ScopeId RegisterNamedScope(
    ScopeVector& scopes,
    ScopeMap& scopeMap,
    const u32 generation,
    const Name& scopeName,
    CreateScope&& createScope,
    ConfigureScope&& configureScope
){
    if(!scopeName)
        return {};

    const auto found = scopeMap.find(scopeName);
    if(found != scopeMap.end())
        return found.value();

    if(scopes.size() >= static_cast<usize>(Limit<u32>::s_Max))
        return {};

    auto scope = createScope(scopeName);
    if(!scope)
        return {};

    ScopeId scopeId;
    scopeId.index = static_cast<u32>(scopes.size());
    scopeId.generation = generation;
    configureScope(*scope, scopeId);

    scopes.push_back(Move(scope));
    scopeMap.try_emplace(scopeName, scopeId);
    return scopeId;
}


// Memory and Timing recorders use these scope-resolution helpers for generation-stamped records.
// Centralizing them keeps the lookup contract single-sourced.

template<
    typename ScopeId,
    typename ScopeVector
>
[[nodiscard]] auto FindNamedScope(const ScopeVector& scopes, const ScopeId scope) -> decltype(scopes[scope.index].get()){
    if(!scope.valid() || scope.index >= scopes.size())
        return nullptr;

    auto record = scopes[scope.index].get();
    if(!record || record->generation != scope.generation)
        return nullptr;

    return record;
}

template<typename ScopeId, typename ScopeVector>
[[nodiscard]] ScopeId ScopeAt(const ScopeVector& scopes, const usize index){
    if(index >= scopes.size())
        return {};

    auto record = scopes[index].get();
    if(!record)
        return {};

    ScopeId scope;
    scope.index = static_cast<u32>(index);
    scope.generation = record->generation;
    return scope;
}

template<typename ScopeVector>
[[nodiscard]] Name ScopeNameAt(const ScopeVector& scopes, const usize index){
    if(index >= scopes.size() || !scopes[index])
        return NAME_NONE;

    return scopes[index]->name;
}


