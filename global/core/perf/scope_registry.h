// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_PERF_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_PERF_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

