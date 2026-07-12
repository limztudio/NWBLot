// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AssetsArenaScope{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Name s_ProcessPendingScratch("core/assets/manager_process_pending_scratch");
inline constexpr Name s_DescribeAvailableCookersScratch("core/assets/describe_available_cookers_scratch");

inline constexpr Name s_AutoCodecFactoryQueueArena("core/assets/auto_codec_factory_queue");
inline constexpr Name s_AutoCookerFactoryQueueArena("core/assets/auto_cooker_factory_queue");
inline constexpr Name s_RegisterCodecsScratch("core/assets/register_auto_collected_codecs_scratch");
inline constexpr Name s_RegisterCookersScratch("core/assets/register_auto_collected_cookers_scratch");

inline constexpr Name s_MetadataParserQueueArena("core/assets/asset_metadata_parser_queue");
inline constexpr Name s_BunchExpanderQueueArena("core/assets/asset_bunch_expander_queue");
inline constexpr Name s_DocumentMetadataParsersScratch("core/assets/document_metadata_parsers_scratch");
inline constexpr Name s_ValueMetadataParsersScratch("core/assets/value_metadata_parsers_scratch");
inline constexpr Name s_AssetBunchExpandersScratch("core/assets/asset_bunch_expanders_scratch");

inline constexpr Name s_AutoRegistrationQueueArena("core/assets/cook_entry_auto_registration_queue");
inline constexpr Name s_RegisterAutoCollectedScratch("core/assets/cook_entry_register_auto_collected_scratch");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

