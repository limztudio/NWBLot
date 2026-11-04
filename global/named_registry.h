// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <global/assert.h>
#include <global/expected.h>
#include <global/limit.h>
#include <global/name.h>
#include <global/scope_exit.h>
#include <global/span.h>


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


// Generation-stamped scope lookup.


template<typename ScopeId, typename ScopeRecord>
[[nodiscard]] auto FindNamedScope(const Span<const ScopeRecord> scopes, const ScopeId scope)noexcept(
    noexcept(DeclVal<ScopeId&>().~ScopeId()) && noexcept(static_cast<b8>(!scope.valid() || scope.index >= scopes.size())) &&
    noexcept(scopes[scope.index].get()) && noexcept(static_cast<b8>(!scopes[scope.index].get() || scopes[scope.index].get()->generation != scope.generation))
) -> RemoveCVRef_T<decltype(scopes[scope.index].get())>{
    if(!scope.valid() || scope.index >= scopes.size())
        return nullptr;

    auto* record = scopes[scope.index].get();
    if(!record || record->generation != scope.generation)
        return nullptr;

    return record;
}

template<typename ScopeId, typename ScopeRecord>
[[nodiscard]] ScopeId ScopeAt(const Span<const ScopeRecord> scopes, const usize index)noexcept(
    noexcept(ScopeId{}) && noexcept(ScopeId(DeclVal<ScopeId&&>())) && noexcept(static_cast<b8>(index >= scopes.size())) && noexcept(scopes[index].get()) &&
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

template<typename ScopeRecord>
[[nodiscard]] Name ScopeNameAt(const Span<const ScopeRecord> scopes, const usize index)noexcept(
    noexcept(static_cast<b8>(index >= scopes.size())) && noexcept(scopes[index].get()) &&
    noexcept(static_cast<b8>(!scopes[index].get())) && noexcept(DeclVal<void(*)(Name)noexcept>()(scopes[index].get()->name))
){
    if(index >= scopes.size())
        return s_NameNone;

    auto* record = scopes[index].get();
    if(!record)
        return s_NameNone;

    return record->name;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

