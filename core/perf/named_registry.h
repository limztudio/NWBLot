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


namespace NamedRegistryDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ScopeEntry>
[[nodiscard]] constexpr auto ScopeRecordPointer(const ScopeEntry& entry)noexcept(noexcept(entry.get())) -> RemoveCVRef_T<decltype(entry.get())>{
    return entry.get();
}

template<typename Record>
[[nodiscard]] constexpr Record* ScopeRecordPointer(Record* const entry)noexcept{
    return entry;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ScopeId, typename ScopeVector>
[[nodiscard]] auto FindNamedScope(const ScopeVector& scopes, const ScopeId scope)noexcept(
    noexcept(DeclVal<ScopeId&>().~ScopeId()) && noexcept(static_cast<bool>(!scope.valid() || scope.index >= scopes.size())) &&
    noexcept(NamedRegistryDetail::ScopeRecordPointer(scopes[scope.index])) && noexcept(static_cast<bool>(!NamedRegistryDetail::ScopeRecordPointer(scopes[scope.index]) || NamedRegistryDetail::ScopeRecordPointer(scopes[scope.index])->generation != scope.generation))
) -> RemoveCVRef_T<decltype(NamedRegistryDetail::ScopeRecordPointer(scopes[scope.index]))>{
    if(!scope.valid() || scope.index >= scopes.size())
        return nullptr;

    auto* record = NamedRegistryDetail::ScopeRecordPointer(scopes[scope.index]);
    if(!record || record->generation != scope.generation)
        return nullptr;

    return record;
}

template<typename ScopeId, typename ScopeVector>
[[nodiscard]] ScopeId ScopeAt(const ScopeVector& scopes, const usize index)noexcept(
    noexcept(ScopeId{}) && noexcept(ScopeId(DeclVal<ScopeId&&>())) && noexcept(static_cast<bool>(index >= scopes.size())) && noexcept(NamedRegistryDetail::ScopeRecordPointer(scopes[index])) &&
    noexcept(DeclVal<ScopeId&>().index = static_cast<u32>(index)) && noexcept(DeclVal<ScopeId&>().generation = NamedRegistryDetail::ScopeRecordPointer(scopes[index])->generation)
){
    if(index >= scopes.size())
        return {};

    auto* record = NamedRegistryDetail::ScopeRecordPointer(scopes[index]);
    if(!record)
        return {};

    ScopeId scope;
    scope.index = static_cast<u32>(index);
    scope.generation = record->generation;
    return scope;
}

template<typename ScopeVector>
[[nodiscard]] Name ScopeNameAt(const ScopeVector& scopes, const usize index)noexcept(
    noexcept(static_cast<bool>(index >= scopes.size())) && noexcept(NamedRegistryDetail::ScopeRecordPointer(scopes[index])) &&
    noexcept(static_cast<bool>(!NamedRegistryDetail::ScopeRecordPointer(scopes[index]))) && noexcept(DeclVal<void(*)(Name)noexcept>()(NamedRegistryDetail::ScopeRecordPointer(scopes[index])->name))
){
    if(index >= scopes.size())
        return s_NameNone;

    auto* record = NamedRegistryDetail::ScopeRecordPointer(scopes[index]);
    if(!record)
        return s_NameNone;

    return record->name;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_PERF_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

