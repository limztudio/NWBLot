// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <engine_namespace.h>

#include <core/assets/cook_metadata.h>
#include <tests/common/memory_sdk.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN
namespace Tests::GraphicsExtensionSdk{
using namespace Core::Assets;

struct alignas(128u) Alignment128{};

inline ParsedMetadataExtensionMap& Map(ParsedAssetMetadata& metadata)noexcept{ return metadata.extensions; }
inline ParsedMetadataExtensionMap MakeMap(HeapStorage& storage){
    return ParsedMetadataExtensionMap(0u, Hasher<Name>{}, EqualTo<Name>{}, storage.arena);
}
template<typename T, typename... Args>
T& Require(ParsedAssetMetadata& metadata, const Name& name, Args&&... args){
    if constexpr(std::is_constructible_v<T, AssetArena&, Args...>)
        return RequireParsedMetadataExtension<T>(name, metadata, metadata.arena, Forward<Args>(args)...);
    else
        return RequireParsedMetadataExtension<T>(name, metadata, Forward<Args>(args)...);
}
template<typename T, typename... Args>
ParsedMetadataExtension Make(HeapStorage& owner, Args&&... args){
    if constexpr(std::is_constructible_v<T, AssetArena&, Args...>)
        return MakeParsedMetadataExtension<T>(owner.arena, owner.arena, Forward<Args>(args)...);
    else
        return MakeParsedMetadataExtension<T>(owner.arena, Forward<Args>(args)...);
}
template<typename T, typename Metadata>
usize Find(Metadata& metadata, const Name& name)noexcept{
    return reinterpret_cast<usize>(FindParsedMetadataExtension<T>(name, metadata));
}
inline usize OwnerAddress(const ParsedMetadataExtension& value)noexcept{ return reinterpret_cast<usize>(value.get()); }
template<typename T>
T& Stored(ParsedMetadataExtension& value)noexcept{ return *MakeNotNull(static_cast<T*>(value.get())); }
template<typename MapT>
b8 Insert(MapT& map, const Name& name, ParsedMetadataExtension& owner){ return map.try_emplace(name, Move(owner)).second; }

template<typename Policy>
struct CallbackHash{
    Policy* policy;
    usize operator()(const Name& name)const{ return policy->hash(name); }
};

template<typename Policy>
using PolicyMap = HashMap<Name, ParsedMetadataExtension, AssetArena, CallbackHash<Policy>, EqualTo<Name>>;
template<typename Policy>
PolicyMap<Policy> MakePolicyMap(HeapStorage& storage, Policy& policy){
    return PolicyMap<Policy>(0u, CallbackHash<Policy>{&policy}, EqualTo<Name>{}, storage.arena);
}

class SavedNameCallbacks final : NoCopy{
public:
    SavedNameCallbacks()noexcept : m_recorder(NameDetail::SymbolRecorderState()), m_resolver(NameDetail::SymbolResolverState()){}
    ~SavedNameCallbacks()noexcept{
        SetNameSymbolRecordCallback(m_recorder.callback, m_recorder.userData);
        SetNameSymbolResolveCallback(m_resolver.callback, m_resolver.userData);
    }
private:
    NameDetail::NameSymbolRecorderState m_recorder;
    NameDetail::NameSymbolResolverState m_resolver;
};

#if defined(NWB_DEBUG)
inline constexpr b8 s_DebugNames = true;
#else
inline constexpr b8 s_DebugNames = false;
#endif
};
NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

