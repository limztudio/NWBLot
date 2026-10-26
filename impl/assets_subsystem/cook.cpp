// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook.h"
#include "arena_names.h"
#include "binary_payload.h"

#include <core/assets/binary_payload_io.h>
#include <core/assets/cook_metadata.h>
#include <core/assets/paths.h>
#include <core/common/log.h>

#include <global/binary.h>
#include <global/text_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool SubsystemAssetSerialization::Serialize(const ISubsystem& subsystem, Core::Assets::AssetBytes& outBinary){
    Core::Alloc::ScratchArena scratchArena(AssetsSubsystemArenaScope::s_SerializeArena);
    if(!subsystem.validatePayload())
        return false;

    const usize fieldCount = subsystem.fieldCount();
    Vector<u64, Core::Alloc::ScratchArena> fieldCounts(scratchArena);
    Vector<NameHash, Core::Alloc::ScratchArena> assetHashes(scratchArena);
    fieldCounts.reserve(fieldCount);

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

    usize reserveBytes = sizeof(SubsystemBinaryPayload::SubsystemHeaderBinary);
    const bool canReserve = AddBinaryVectorReserveBytes(reserveBytes, fieldCounts) && AddBinaryVectorReserveBytes(reserveBytes, assetHashes);

    outBinary.clear();
    if(canReserve)
        outBinary.reserve(reserveBytes);

    SubsystemBinaryPayload::SubsystemHeaderBinary header;
    header.subsystemTypeHash = subsystem.assetType().hash();
    header.fieldCount = static_cast<u64>(fieldCounts.size());
    header.assetCount = static_cast<u64>(assetHashes.size());
    AppendPOD(outBinary, header);

    return
        Core::Assets::AppendVectorPayload(outBinary, fieldCounts, SubsystemBinaryPayload::s_SubsystemSerializeContext, SubsystemBinaryPayload::s_FieldCountsPayloadLabel)
        && Core::Assets::AppendVectorPayload(outBinary, assetHashes, SubsystemBinaryPayload::s_SubsystemSerializeContext, SubsystemBinaryPayload::s_AssetsPayloadLabel)
    ;
}

bool SubsystemAssetCodec::serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const{
    const ISubsystem* const subsystem = CastSubsystem(&asset);
    if(!subsystem){
        NWB_LOGGER_ERROR(
            NWB_TEXT("{} failed: invalid asset type '{}'"),
            SubsystemBinaryPayload::s_SubsystemSerializeContext,
            ConvertString(asset.assetType().resolvedText())
        );
        return false;
    }

    return SubsystemAssetSerialization::Serialize(*subsystem, outBinary);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_subsystem_cook{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Core::Metascript;


template<typename TAsset>
[[nodiscard]] static Expected<Core::Assets::AssetRef<TAsset>> ParseAssetRefString(
    const Path& nwbFilePath,
    const Value& itemValue,
    const AStringView fieldLabel,
    Core::Alloc::ScratchArena& scratchArena
){
    if(!itemValue.isString()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': '{}' must be a string"), ConvertString(AssetsSubsystemCook::s_SubsystemDiagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(fieldLabel));
        return MakeUnexpected(Failure{});
    }
    const AStringView text(itemValue.asString().data(), itemValue.asString().size());
    if(TrimView(text).empty() || text != TrimView(text)){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': '{}' entry '{}' must be a non-empty trimmed virtual path"), ConvertString(AssetsSubsystemCook::s_SubsystemDiagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(fieldLabel), ConvertString(text));
        return MakeUnexpected(Failure{});
    }
    if(text.find('\\') != AStringView::npos || !Core::Assets::HasReservedAssetVirtualRoot(text, scratchArena)){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': '{}' entry '{}' must be an engine/- or project/-rooted virtual path"), ConvertString(AssetsSubsystemCook::s_SubsystemDiagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(fieldLabel), ConvertString(text));
        return MakeUnexpected(Failure{});
    }
    const auto ref = Core::Assets::AssetRef<TAsset>::FromText(text);
    if(!ref || !ref->valid()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': '{}' entry '{}' is invalid"), ConvertString(AssetsSubsystemCook::s_SubsystemDiagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(fieldLabel), ConvertString(text));
        return MakeUnexpected(Failure{});
    }
    return *ref;
}

[[nodiscard]] static bool ValidateSubsystemMetadataFields(const ISubsystem& subsystem, const Path& nwbFilePath, const Value& asset){
    const usize count = subsystem.fieldCount();
    return Core::Assets::ValidateMetadataAssetFields(nwbFilePath, asset, AssetsSubsystemCook::s_SubsystemDiagnosticPrefix, [&subsystem, count](const AStringView fieldName)noexcept{
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


bool AssetsSubsystemCook::ParseSubsystemCookMetadata(const Path& nwbFilePath, const Core::Metascript::Value& asset, Core::Alloc::ScratchArena& scratchArena, ISubsystem& outSubsystem){
    using namespace __hidden_subsystem_cook;

    if(!ValidateSubsystemMetadataFields(outSubsystem, nwbFilePath, asset))
        return false;

    const usize count = outSubsystem.fieldCount();
    for(usize i = 0u; i < count; ++i){
        const SubsystemFieldValueDescriptor descriptor = outSubsystem.fieldDescriptor(i);
        const Core::Metascript::Value* const fieldValue = asset.findField(descriptor.fieldName);
        if(!fieldValue){
            NWB_LOGGER_ERROR(
                NWB_TEXT("{} '{}': required field '{}' is missing for '{}'"),
                ConvertString(s_SubsystemDiagnosticPrefix),
                PathToString<tchar>(nwbFilePath),
                ConvertString(descriptor.fieldName),
                ConvertString(outSubsystem.assetType().resolvedText())
            );
            return false;
        }
        outSubsystem.clearFieldValues(i);
        if(descriptor.isArray){
            if(!fieldValue->isList() || fieldValue->asList().empty()){
                NWB_LOGGER_ERROR(
                    NWB_TEXT("{} '{}': '{}' must be a non-empty list of strings"),
                    ConvertString(s_SubsystemDiagnosticPrefix),
                    PathToString<tchar>(nwbFilePath),
                    ConvertString(descriptor.fieldName)
                );
                return false;
            }
            for(const Core::Metascript::Value& itemValue : fieldValue->asList()){
                const auto parsedRef = ParseAssetRefString<Core::Assets::IAsset>(nwbFilePath, itemValue, descriptor.fieldName, scratchArena);
                if(!parsedRef)
                    return false;
                const usize existingCount = outSubsystem.fieldValueCount(i);
                for(usize prevIndex = 0u; prevIndex < existingCount; ++prevIndex){
                    if(outSubsystem.fieldValue(i, prevIndex) == parsedRef->name()){
                        NWB_LOGGER_ERROR(
                            NWB_TEXT("{} '{}': '{}' contains duplicate entry '{}'"),
                            ConvertString(s_SubsystemDiagnosticPrefix),
                            PathToString<tchar>(nwbFilePath),
                            ConvertString(descriptor.fieldName),
                            ConvertString(parsedRef->name().resolvedText())
                        );
                        return false;
                    }
                }
                outSubsystem.appendFieldValue(i, parsedRef->name());
            }
        }
        else{
            const auto parsedRef = ParseAssetRefString<Core::Assets::IAsset>(nwbFilePath, *fieldValue, descriptor.fieldName, scratchArena);
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

