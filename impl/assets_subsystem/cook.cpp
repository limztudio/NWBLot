// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook.h"
#include <core/assets/polymorphic_asset_codec.h>

#include <impl/assets_subsystem/subsystem_binary_codec.adhd.h>

#include <core/assets/binary_payload_io.h>
#include <core/assets/cook_metadata.h>
#include <core/assets/reserved_asset_ref_parser.h>
#include <core/common/log.h>

#include <global/binary.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 SubsystemAssetCodec::serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const{
    return Core::Assets::PolymorphicAssetCodec::SerializePolymorphicAsset<ISubsystem, &SubsystemAssetSerialization::Serialize>(asset, s_SubsystemSerializeContext, outBinary);
}

b8 SubsystemAssetSerialization::Serialize(const ISubsystem& subsystem, Core::Assets::AssetBytes& outBinary){
    Core::Alloc::ScratchArena scratchArena(s_SubsystemSerializeArena);
    if(!subsystem.validatePayload())
        return false;

    const usize fieldCount = subsystem.fieldCount();
    Vector<NameHash, Core::Alloc::ScratchArena> subsystemHashes(scratchArena);
    Vector<u64, Core::Alloc::ScratchArena> fieldCounts(scratchArena);
    Vector<NameHash, Core::Alloc::ScratchArena> assetHashes(scratchArena);
    subsystemHashes.reserve(subsystem.subsystems.size());
    fieldCounts.reserve(fieldCount);

    for(const ISubsystem::SubsystemRef& ref : subsystem.subsystems)
        subsystemHashes.push_back(ref.name().hash());

    usize totalAssetCount = 0u;
    for(usize i = 0u; i < fieldCount; ++i){
        const usize valueCount = subsystem.fieldValueCount(i);
        fieldCounts.push_back(static_cast<u64>(valueCount));
        totalAssetCount += valueCount;
    }

    assetHashes.reserve(totalAssetCount);
    for(usize i = 0u; i < fieldCount; ++i){
        const usize valueCount = subsystem.fieldValueCount(i);
        for(usize elementIndex = 0u; elementIndex < valueCount; ++elementIndex)
            assetHashes.push_back(subsystem.fieldValue(i, elementIndex).hash());
    }

    return SubsystemBinaryCodec().serialize(subsystem.assetType().hash(), subsystemHashes, fieldCounts, assetHashes, outBinary);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_subsystem_cook{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Core::Metascript;

[[nodiscard]] static b8 ValidateSubsystemMetadataFields(const ISubsystem& subsystem, const Path& nwbFilePath, const Value& asset){
    const usize count = subsystem.fieldCount();
    return Core::Assets::ValidateMetadataAssetFields(nwbFilePath, asset, s_SubsystemDiagnosticPrefix, [&subsystem, count](const AStringView fieldName)noexcept{
        if(fieldName == s_SubsystemSubsystemsFieldName)
            return true;
        for(usize i = 0u; i < count; ++i){
            if(fieldName == subsystem.fieldDescriptor(i).fieldName)
                return true;
        }
        return false;
    });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 AssetsSubsystemCook::ParseSubsystemCookMetadata(const Path& nwbFilePath, const Core::Metascript::Value& asset, Core::Alloc::ScratchArena& scratchArena, ISubsystem& outSubsystem){
    using namespace __hidden_subsystem_cook;

    if(!ValidateSubsystemMetadataFields(outSubsystem, nwbFilePath, asset))
        return false;

    outSubsystem.subsystems.clear();
    if(const Core::Metascript::Value* const subsystemsValue = asset.findField(s_SubsystemSubsystemsFieldName)){
        if(!subsystemsValue->isList()){
            NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': '{}' must be a list of strings"), ConvertString(s_SubsystemDiagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(s_SubsystemSubsystemsFieldName));
            return false;
        }
        outSubsystem.subsystems.reserve(subsystemsValue->asList().size());
        for(const Core::Metascript::Value& itemValue : subsystemsValue->asList()){
            const auto parsedRef = Core::Assets::ReservedAssetRefParser::ParseValue<ISubsystem>(nwbFilePath, s_SubsystemDiagnosticPrefix, s_SubsystemSubsystemsFieldName, itemValue, "must be a string", scratchArena);
            if(!parsedRef)
                return false;
            if(parsedRef->name() == outSubsystem.virtualPath()){
                NWB_LOGGER_ERROR(
                    NWB_TEXT("{} '{}': '{}' cannot reference subsystem '{}' itself")
                    , ConvertString(s_SubsystemDiagnosticPrefix)
                    , PathToString<tchar>(nwbFilePath)
                    , ConvertString(s_SubsystemSubsystemsFieldName)
                    , ConvertString(outSubsystem.virtualPath().resolvedText())
                );
                return false;
            }
            outSubsystem.subsystems.push_back(*parsedRef);
        }
        DeduplicateSubsystemRefs(outSubsystem.subsystems);
    }

    const usize count = outSubsystem.fieldCount();
    for(usize i = 0u; i < count; ++i){
        const SubsystemFieldValueDescriptor descriptor = outSubsystem.fieldDescriptor(i);
        const Core::Metascript::Value* const fieldValue = asset.findField(descriptor.fieldName);
        if(!fieldValue){
            NWB_LOGGER_ERROR(
                NWB_TEXT("{} '{}': required field '{}' is missing for '{}'")
                , ConvertString(s_SubsystemDiagnosticPrefix)
                , PathToString<tchar>(nwbFilePath)
                , ConvertString(descriptor.fieldName)
                , ConvertString(outSubsystem.assetType().resolvedText())
            );
            return false;
        }
        outSubsystem.clearFieldValues(i);
        if(descriptor.isArray){
            if(!fieldValue->isList() || fieldValue->asList().empty()){
                NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': '{}' must be a non-empty list of strings"), ConvertString(s_SubsystemDiagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(descriptor.fieldName));
                return false;
            }
            for(const Core::Metascript::Value& itemValue : fieldValue->asList()){
                const auto parsedRef = Core::Assets::ReservedAssetRefParser::ParseValue<Core::Assets::IAsset>(nwbFilePath, s_SubsystemDiagnosticPrefix, descriptor.fieldName, itemValue, "must be a string", scratchArena);
                if(!parsedRef)
                    return false;
                const usize existingCount = outSubsystem.fieldValueCount(i);
                for(usize prevIndex = 0u; prevIndex < existingCount; ++prevIndex){
                    if(outSubsystem.fieldValue(i, prevIndex) == parsedRef->name()){
                        NWB_LOGGER_ERROR(
                            NWB_TEXT("{} '{}': '{}' contains duplicate entry '{}'")
                            , ConvertString(s_SubsystemDiagnosticPrefix)
                            , PathToString<tchar>(nwbFilePath)
                            , ConvertString(descriptor.fieldName)
                            , ConvertString(parsedRef->name().resolvedText())
                        );
                        return false;
                    }
                }
                outSubsystem.appendFieldValue(i, parsedRef->name());
            }
        }
        else{
            const auto parsedRef = Core::Assets::ReservedAssetRefParser::ParseValue<Core::Assets::IAsset>(nwbFilePath, s_SubsystemDiagnosticPrefix, descriptor.fieldName, *fieldValue, "must be a string", scratchArena);
            if(!parsedRef)
                return false;
            outSubsystem.appendFieldValue(i, parsedRef->name());
        }
    }

    return outSubsystem.validatePayload();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

