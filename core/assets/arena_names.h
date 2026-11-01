// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

#include <global/auto_registration.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AssetsArenaScope{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Name s_AutoCodecFactoryQueueArena("core/assets/auto_codec_factory_queue");
inline constexpr Name s_RegisterCodecsScratch("core/assets/register_auto_collected_codecs_scratch");

inline constexpr Name s_MetadataParserQueueArena("core/assets/asset_metadata_parser_queue");
inline constexpr Name s_BunchExpanderQueueArena("core/assets/asset_bunch_expander_queue");
inline constexpr Name s_CompositeRootTypeQueueArena("core/assets/composite_root_type_queue");
inline constexpr Name s_NonStandaloneTypeQueueArena("core/assets/non_standalone_type_queue");
inline constexpr Name s_NonStandaloneTokenQueueArena("core/assets/non_standalone_token_queue");
inline constexpr Name s_NonStandaloneTokenSuffixQueueArena("core/assets/non_standalone_token_suffix_queue");
inline constexpr Name s_PairedSidecarRuleQueueArena("core/assets/paired_sidecar_rule_queue");

inline constexpr Name s_AutoRegistrationQueueArena("core/assets/cook_entry_auto_registration_queue");
inline constexpr Name s_CookDependencyQueueArena("core/assets/cook_dependency_auto_registration_queue");

inline constexpr Name s_GatherMergeQueueArena("core/assets/gather_merge_queue");
inline constexpr Name s_GatherMergeQueryScratch("core/assets/gather_merge_query_scratch");

inline constexpr Name s_InputListScratch("assets/input_list");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ValueT, const Name& ArenaName>
[[nodiscard]] inline ::AutoRegistrationQueue<ValueT, AssetArena>& QueryAssetAutoRegistrationQueue(){
    static ::AutoRegistrationQueue<ValueT, AssetArena> s_Queue(ArenaName);
    return s_Queue;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

