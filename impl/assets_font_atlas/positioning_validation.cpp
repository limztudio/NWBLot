// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "model.h"

#include <core/common/log.h>

#include <global/bit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_font_atlas_positioning{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Byte offsets and sizes below follow the big-endian OpenType kern, GPOS and GDEF table layouts.
static constexpr u32 s_ValidationWorkBudget = 1048576u;

static constexpr u32 s_OtU16Bytes = 2u;
static constexpr u32 s_OtU32Bytes = 4u;
static constexpr u32 s_OtU16Bits = 16u;
static constexpr u32 s_OtMinorVersionOffset = 2u;
static constexpr u32 s_OtAnyClassCount = 65536u;
static constexpr u32 s_OtRangeRecordBytes = 6u;
static constexpr u32 s_OtRangeRecordEndOffset = 2u;
static constexpr u32 s_OtRangeRecordValueOffset = 4u;
static constexpr u32 s_OtTagRecordBytes = 6u;
// List count plus the first record's tag lands on that record's Offset16.
static constexpr u32 s_OtTagListFirstOffsetField = 6u;

static constexpr u32 s_KernHeaderBytes = 4u;
static constexpr u32 s_KernTableCountOffset = 2u;
static constexpr u32 s_KernMaxTableCount = 65535u;
static constexpr u32 s_KernSubtableHeaderBytes = 6u;
static constexpr u32 s_KernSubtableLengthOffset = 2u;
// Coverage sits at the same offset in the Microsoft and Apple subtable headers.
static constexpr u32 s_KernSubtableCoverageOffset = 4u;
static constexpr u32 s_KernFormatShift = 8u;
static constexpr u32 s_KernPairListHeaderBytes = 8u;
static constexpr u32 s_KernPairBytes = 6u;
static constexpr u32 s_KernPairRightOffset = 2u;
static constexpr u32 s_KernPairKeyLeftScale = 65536u;
static constexpr u32 s_AppleKernVersion = 0x00010000u;
static constexpr u32 s_AppleKernHeaderBytes = 8u;
static constexpr u32 s_AppleKernTableCountOffset = 4u;
static constexpr u32 s_AppleKernSubtableHeaderBytes = 8u;
static constexpr u32 s_AppleKernFormatMask = 255u;

static constexpr u32 s_GposHeaderBytes = 10u;
static constexpr u32 s_GposHeaderV11Bytes = 14u;
static constexpr u32 s_GposScriptListOffset = 4u;
static constexpr u32 s_GposFeatureListOffset = 6u;
static constexpr u32 s_GposLookupListOffset = 8u;
static constexpr u32 s_GposFeatureVariationsOffset = 10u;
static constexpr u32 s_FeatureVariationsHeaderBytes = 8u;
static constexpr u32 s_FeatureVariationsCountOffset = 4u;
static constexpr u32 s_FeatureVariationRecordBytes = 8u;

static constexpr u32 s_GdefHeaderBytes = 12u;
static constexpr u32 s_GdefHeaderV12Bytes = 14u;
static constexpr u32 s_GdefHeaderV13Bytes = 18u;
static constexpr u32 s_GdefMarkGlyphSetsMinorVersion = 2u;
static constexpr u32 s_GdefItemVarStoreMinorVersion = 3u;
static constexpr u32 s_GdefGlyphClassDefOffset = 4u;
static constexpr u32 s_GdefAttachListOffset = 6u;
static constexpr u32 s_GdefLigCaretListOffset = 8u;
static constexpr u32 s_GdefMarkAttachClassDefOffset = 10u;
static constexpr u32 s_GdefMarkGlyphSetsOffset = 12u;
static constexpr u32 s_GdefItemVarStoreOffset = 14u;
static constexpr u32 s_GdefGlyphClassCount = 5u;
static constexpr u32 s_GdefGlyphListHeaderBytes = 4u;
static constexpr u32 s_GdefGlyphListCountOffset = 2u;
static constexpr u32 s_CaretValueBytes = 4u;
static constexpr u32 s_CaretValueDeviceFormat = 3u;
static constexpr u32 s_CaretValueDeviceBytes = 6u;
static constexpr u32 s_CaretValueDeviceOffset = 4u;
static constexpr u32 s_MarkGlyphSetsHeaderBytes = 4u;
static constexpr u32 s_MarkGlyphSetsCountOffset = 2u;
static constexpr u32 s_ItemVarStoreHeaderBytes = 8u;
static constexpr u32 s_ItemVarStoreDataCountOffset = 6u;

static constexpr u32 s_CoverageHeaderBytes = 4u;
static constexpr u32 s_CoverageCountOffset = 2u;
static constexpr u32 s_CoverageRangeFormat = 2u;

static constexpr u32 s_ClassDefArrayHeaderBytes = 6u;
static constexpr u32 s_ClassDefStartGlyphOffset = 2u;
static constexpr u32 s_ClassDefGlyphCountOffset = 4u;
static constexpr u32 s_ClassDefRangeFormat = 2u;
static constexpr u32 s_ClassDefRangeHeaderBytes = 4u;
static constexpr u32 s_ClassDefRangeCountOffset = 2u;

static constexpr u32 s_DeviceHeaderBytes = 6u;
static constexpr u32 s_DeviceEndSizeOffset = 2u;
static constexpr u32 s_DeviceDeltaFormatOffset = 4u;
static constexpr u32 s_DeviceMaxDeltaFormat = 3u;
static constexpr u32 s_DeviceDeltaWordBits = 16u;
static constexpr u32 s_DeviceDeltaWordRoundUp = 15u;
static constexpr u32 s_VariationIndexDeltaFormat = 0x8000u;

static constexpr u32 s_ValueFormatFieldMask = 0xffu;
static constexpr u32 s_ValueFormatReservedMask = 0xff00u;
static constexpr u32 s_ValueFormatFieldBitCount = 8u;
static constexpr u32 s_ValueFormatFirstDeviceBit = 4u;

static constexpr u32 s_LangSysHeaderBytes = 6u;
static constexpr u32 s_LangSysRequiredFeatureOffset = 2u;
static constexpr u32 s_LangSysFeatureCountOffset = 4u;
static constexpr u32 s_LangSysNoRequiredFeature = 65535u;
static constexpr u32 s_ScriptHeaderBytes = 4u;
static constexpr u32 s_ScriptLangSysCountOffset = 2u;
// Script header plus the first LangSysRecord tag lands on that record's Offset16.
static constexpr u32 s_ScriptFirstLangSysOffsetField = 8u;
static constexpr u32 s_FeatureHeaderBytes = 4u;
static constexpr u32 s_FeatureLookupCountOffset = 2u;
static constexpr u32 s_FeatureParamsMinBytes = 2u;

static constexpr u32 s_PosSubtableHeaderBytes = 4u;
static constexpr u32 s_PosCoverageOffset = 2u;
static constexpr u32 s_SinglePosHeaderBytes = 6u;
static constexpr u32 s_SinglePosValueFormatOffset = 4u;
static constexpr u32 s_SinglePosArrayFormat = 2u;
static constexpr u32 s_SinglePosArrayHeaderBytes = 8u;
static constexpr u32 s_SinglePosValueCountOffset = 6u;
static constexpr u32 s_PairPosSetHeaderBytes = 10u;
static constexpr u32 s_PairPosValueFormat1Offset = 4u;
static constexpr u32 s_PairPosValueFormat2Offset = 6u;
static constexpr u32 s_PairPosSetCountOffset = 8u;
static constexpr u32 s_PairPosClassFormat = 2u;
static constexpr u32 s_PairPosClassHeaderBytes = 16u;
static constexpr u32 s_PairPosClassDef1Offset = 8u;
static constexpr u32 s_PairPosClassDef2Offset = 10u;
static constexpr u32 s_PairPosClass1CountOffset = 12u;
static constexpr u32 s_PairPosClass2CountOffset = 14u;
static constexpr u32 s_PairPosMaxClassPairs = 262144u;
static constexpr u32 s_ExtensionPosBytes = 8u;
static constexpr u32 s_ExtensionPosTypeOffset = 2u;
static constexpr u32 s_ExtensionPosOffsetField = 4u;
static constexpr u32 s_ContextMaxFormat = 3u;

static constexpr u32 s_GposPairLookupType = 2u;
static constexpr u32 s_GposCursiveLookupType = 3u;
static constexpr u32 s_GposMarkMarkLookupType = 6u;
static constexpr u32 s_GposContextLookupType = 7u;
static constexpr u32 s_GposChainedContextLookupType = 8u;
static constexpr u32 s_GposExtensionLookupType = 9u;
static constexpr u32 s_LookupHeaderBytes = 6u;
static constexpr u32 s_LookupFlagOffset = 2u;
static constexpr u32 s_LookupSubtableCountOffset = 4u;
static constexpr u32 s_LookupFlagReservedMask = 0x00e0u;
static constexpr u32 s_LookupFlagUseMarkFilteringSet = 0x10u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class TableValidator final{
public:
    TableValidator(const Core::Assets::AssetBytes& bytes, u32 glyphCount)noexcept
        : m_bytes(bytes)
        , m_glyphCount(glyphCount)
    {}


public:
    [[nodiscard]] bool kern()noexcept{
        if(!range(0u, s_KernHeaderBytes))
            return false;
        const bool apple = u32At(0u) == s_AppleKernVersion;
        if(!apple && u16At(0u) != 0u)
            return false;
        if(apple && !range(0u, s_AppleKernHeaderBytes))
            return false;
        const u32 tableCount = apple ? u32At(s_AppleKernTableCountOffset) : u16At(s_KernTableCountOffset);
        if(tableCount > s_KernMaxTableCount)
            return false;
        u64 offset = apple ? s_AppleKernHeaderBytes : s_KernHeaderBytes;
        for(u32 table = 0u; table < tableCount; ++table){
            const u32 header = apple ? s_AppleKernSubtableHeaderBytes : s_KernSubtableHeaderBytes;
            if(!range(offset, header))
                return false;
            const u32 length = apple ? u32At(offset) : u16At(offset + s_KernSubtableLengthOffset);
            const u16 coverage = u16At(offset + s_KernSubtableCoverageOffset);
            const u16 format = apple ? coverage & s_AppleKernFormatMask : coverage >> s_KernFormatShift;
            if(length < header || !range(offset, length))
                return false;
            if(format == 0u){
                if(length < header + s_KernPairListHeaderBytes)
                    return false;
                const u32 count = u16At(offset + header);
                if(static_cast<u64>(count) * s_KernPairBytes > length - header - s_KernPairListHeaderBytes || !spend(count))
                    return false;
                u32 previous = 0u;
                for(u32 index = 0u; index < count; ++index){
                    const u64 record = offset + header + s_KernPairListHeaderBytes + index * s_KernPairBytes;
                    const u32 left = u16At(record);
                    const u32 right = u16At(record + s_KernPairRightOffset);
                    const u32 pair = left * s_KernPairKeyLeftScale + right;
                    if(left >= m_glyphCount || right >= m_glyphCount || (index > 0u && pair <= previous))
                        return false;
                    previous = pair;
                }
            }
            offset += length;
        }
        return offset == m_bytes.size();
    }

    [[nodiscard]] bool gpos()noexcept{
        if(!range(0u, s_GposHeaderBytes) || u16At(0u) != 1u || u16At(s_OtMinorVersionOffset) > 1u)
            return false;
        const u64 scripts = u16At(s_GposScriptListOffset);
        const u64 features = u16At(s_GposFeatureListOffset);
        const u64 lookups = u16At(s_GposLookupListOffset);
        if(scripts < s_GposHeaderBytes || features < s_GposHeaderBytes || lookups < s_GposHeaderBytes || !range(features, s_OtU16Bytes) || !range(lookups, s_OtU16Bytes))
            return false;
        const u32 featureCount = u16At(features);
        const u32 lookupCount = u16At(lookups);
        if(!scriptList(scripts, featureCount) || !featureList(features, lookupCount) || !lookupList(lookups))
            return false;
        if(u16At(s_OtMinorVersionOffset) == 1u){
            if(!range(0u, s_GposHeaderV11Bytes))
                return false;
            const u64 variations = u32At(s_GposFeatureVariationsOffset);
            if(
                variations != 0u
                && (
                    !range(variations, s_FeatureVariationsHeaderBytes)
                    || u16At(variations) != 1u
                    || u16At(variations + s_OtMinorVersionOffset) != 0u
                    || !range(variations + s_FeatureVariationsHeaderBytes, static_cast<u64>(u32At(variations + s_FeatureVariationsCountOffset)) * s_FeatureVariationRecordBytes)
                )
            )
                return false;
        }
        return true;
    }

    [[nodiscard]] bool gdef()noexcept{
        if(!range(0u, s_GdefHeaderBytes) || u16At(0u) != 1u || u16At(s_OtMinorVersionOffset) > s_GdefItemVarStoreMinorVersion || u16At(s_OtMinorVersionOffset) == 1u)
            return false;
        const u32 minor = u16At(s_OtMinorVersionOffset);
        const u32 headerSize = minor == s_GdefItemVarStoreMinorVersion ? s_GdefHeaderV13Bytes : minor == s_GdefMarkGlyphSetsMinorVersion ? s_GdefHeaderV12Bytes : s_GdefHeaderBytes;
        if(!range(0u, headerSize))
            return false;
        if(!classDef(u16At(s_GdefGlyphClassDefOffset), s_GdefGlyphClassCount) || !classDef(u16At(s_GdefMarkAttachClassDefOffset), s_OtAnyClassCount))
            return false;
        for(u32 field = s_GdefAttachListOffset; field <= s_GdefLigCaretListOffset; field += s_OtU16Bytes){
            const u64 list = u16At(field);
            if(list == 0u)
                continue;
            if(!range(list, s_GdefGlyphListHeaderBytes))
                return false;
            const u32 count = u16At(list + s_GdefGlyphListCountOffset);
            const auto coverageCount = coverage(list + u16At(list));
            if(!coverageCount || count != *coverageCount || !range(list + s_GdefGlyphListHeaderBytes, count * s_OtU16Bytes))
                return false;
            for(u32 index = 0u; index < count; ++index){
                const u64 item = list + u16At(list + s_GdefGlyphListHeaderBytes + index * s_OtU16Bytes);
                if(item == list || !range(item, s_OtU16Bytes) || !range(item + s_OtU16Bytes, u16At(item) * s_OtU16Bytes))
                    return false;
                if(field == s_GdefLigCaretListOffset){
                    for(u32 caret = 0u; caret < u16At(item); ++caret){
                        const u64 target = item + u16At(item + s_OtU16Bytes + caret * s_OtU16Bytes);
                        if(target == item || !range(target, s_CaretValueBytes))
                            return false;
                        const u16 format = u16At(target);
                        if(format < 1u || format > s_CaretValueDeviceFormat || (format == s_CaretValueDeviceFormat && (!range(target, s_CaretValueDeviceBytes) || !device(target, u16At(target + s_CaretValueDeviceOffset)))))
                            return false;
                    }
                }
            }
        }
        if(minor >= s_GdefMarkGlyphSetsMinorVersion && u16At(s_GdefMarkGlyphSetsOffset) != 0u){
            const u64 sets = u16At(s_GdefMarkGlyphSetsOffset);
            if(!range(sets, s_MarkGlyphSetsHeaderBytes) || u16At(sets) != 1u || !range(sets + s_MarkGlyphSetsHeaderBytes, u16At(sets + s_MarkGlyphSetsCountOffset) * s_OtU32Bytes))
                return false;
            for(u32 index = 0u; index < u16At(sets + s_MarkGlyphSetsCountOffset); ++index){
                const u32 offset = u32At(sets + s_MarkGlyphSetsHeaderBytes + index * s_OtU32Bytes);
                if(offset == 0u || !coverage(sets + offset))
                    return false;
            }
        }
        if(minor == s_GdefItemVarStoreMinorVersion && u32At(s_GdefItemVarStoreOffset) != 0u){
            const u64 store = u32At(s_GdefItemVarStoreOffset);
            if(!range(store, s_ItemVarStoreHeaderBytes) || u16At(store) != 1u || !range(store + s_ItemVarStoreHeaderBytes, u16At(store + s_ItemVarStoreDataCountOffset) * s_OtU32Bytes))
                return false;
        }
        return true;
    }


private:
    [[nodiscard]] bool range(u64 offset, u64 count)noexcept{
        return spend(1u) && offset <= m_bytes.size() && count <= m_bytes.size() - offset;
    }

    [[nodiscard]] bool spend(u32 count)noexcept{
        if(count > m_budget)
            return false;
        m_budget -= count;
        return true;
    }

    [[nodiscard]] u16 u16At(u64 offset)const noexcept{
        return static_cast<u16>((static_cast<u16>(m_bytes[offset]) << s_BitsPerByte) | m_bytes[offset + 1u]);
    }

    [[nodiscard]] u32 u32At(u64 offset)const noexcept{
        return (static_cast<u32>(u16At(offset)) << s_OtU16Bits) | u16At(offset + s_OtU16Bytes);
    }

    [[nodiscard]] Expected<u32> coverage(u64 offset)noexcept{
        if(!range(offset, s_CoverageHeaderBytes))
            return MakeUnexpected(Failure{});
        const u16 format = u16At(offset);
        const u32 count = u16At(offset + s_CoverageCountOffset);
        u32 glyphCount = 0u;
        if(format != 1u && format != s_CoverageRangeFormat)
            return MakeUnexpected(Failure{});
        if(!range(offset + s_CoverageHeaderBytes, count * (format == 1u ? s_OtU16Bytes : s_OtRangeRecordBytes)) || !spend(count))
            return MakeUnexpected(Failure{});
        u32 previous = 0u;
        for(u32 index = 0u; index < count; ++index){
            const u64 record = offset + s_CoverageHeaderBytes + index * (format == 1u ? s_OtU16Bytes : s_OtRangeRecordBytes);
            const u32 start = u16At(record);
            const u32 end = format == 1u ? start : u16At(record + s_OtRangeRecordEndOffset);
            if(start > end || end >= m_glyphCount || (index > 0u && start <= previous) || (format == s_CoverageRangeFormat && u16At(record + s_OtRangeRecordValueOffset) != glyphCount))
                return MakeUnexpected(Failure{});
            glyphCount += end - start + 1u;
            previous = end;
        }
        return glyphCount;
    }

    [[nodiscard]] bool classDef(u64 offset, u32 classCount)noexcept{
        if(offset == 0u)
            return true;
        if(!range(offset, s_ClassDefRangeHeaderBytes))
            return false;
        const u16 format = u16At(offset);
        if(format == 1u){
            if(!range(offset, s_ClassDefArrayHeaderBytes))
                return false;
            const u32 start = u16At(offset + s_ClassDefStartGlyphOffset);
            const u32 count = u16At(offset + s_ClassDefGlyphCountOffset);
            if(start + count > m_glyphCount || !range(offset + s_ClassDefArrayHeaderBytes, count * s_OtU16Bytes) || !spend(count))
                return false;
            for(u32 index = 0u; index < count; ++index){
                if(u16At(offset + s_ClassDefArrayHeaderBytes + index * s_OtU16Bytes) >= classCount)
                    return false;
            }
            return true;
        }
        if(format != s_ClassDefRangeFormat)
            return false;
        const u32 count = u16At(offset + s_ClassDefRangeCountOffset);
        if(!range(offset + s_ClassDefRangeHeaderBytes, count * s_OtRangeRecordBytes) || !spend(count))
            return false;
        u32 previous = 0u;
        for(u32 index = 0u; index < count; ++index){
            const u64 record = offset + s_ClassDefRangeHeaderBytes + index * s_OtRangeRecordBytes;
            const u32 start = u16At(record);
            const u32 end = u16At(record + s_OtRangeRecordEndOffset);
            if(start > end || end >= m_glyphCount || (index > 0u && start <= previous) || u16At(record + s_OtRangeRecordValueOffset) >= classCount)
                return false;
            previous = end;
        }
        return true;
    }

    [[nodiscard]] bool device(u64 parent, u32 offset)noexcept{
        if(offset == 0u)
            return true;
        const u64 table = parent + offset;
        if(!range(table, s_DeviceHeaderBytes))
            return false;
        const u32 format = u16At(table + s_DeviceDeltaFormatOffset);
        if(format == s_VariationIndexDeltaFormat)
            return true;
        const u32 start = u16At(table);
        const u32 end = u16At(table + s_DeviceEndSizeOffset);
        if(format < 1u || format > s_DeviceMaxDeltaFormat || start > end)
            return false;
        const u32 bits = 1u << format;
        return range(table + s_DeviceHeaderBytes, ((end - start + 1u) * bits + s_DeviceDeltaWordRoundUp) / s_DeviceDeltaWordBits * s_OtU16Bytes);
    }

    [[nodiscard]] u32 valueSize(u16 format)const noexcept{
        return static_cast<u32>(CountSetBits(static_cast<u32>(format & s_ValueFormatFieldMask))) * s_OtU16Bytes;
    }

    [[nodiscard]] bool value(u64 offset, u16 format, u64 parent)noexcept{
        if((format & s_ValueFormatReservedMask) != 0u || !range(offset, valueSize(format)))
            return false;
        u32 field = 0u;
        for(u32 bit = 0u; bit < s_ValueFormatFieldBitCount; ++bit){
            if((format & (1u << bit)) == 0u)
                continue;
            if(bit >= s_ValueFormatFirstDeviceBit && !device(parent, u16At(offset + field)))
                return false;
            field += s_OtU16Bytes;
        }
        return true;
    }

    [[nodiscard]] bool langSys(u64 offset, u32 featureCount)noexcept{
        if(!range(offset, s_LangSysHeaderBytes) || u16At(offset) != 0u)
            return false;
        const u32 required = u16At(offset + s_LangSysRequiredFeatureOffset);
        const u32 count = u16At(offset + s_LangSysFeatureCountOffset);
        if((required != s_LangSysNoRequiredFeature && required >= featureCount) || !range(offset + s_LangSysHeaderBytes, count * s_OtU16Bytes) || !spend(count))
            return false;
        for(u32 index = 0u; index < count; ++index){
            if(u16At(offset + s_LangSysHeaderBytes + index * s_OtU16Bytes) >= featureCount)
                return false;
        }
        return true;
    }

    [[nodiscard]] bool scriptList(u64 offset, u32 featureCount)noexcept{
        if(!range(offset, s_OtU16Bytes))
            return false;
        const u32 count = u16At(offset);
        if(!range(offset + s_OtU16Bytes, count * s_OtTagRecordBytes) || !spend(count))
            return false;
        for(u32 index = 0u; index < count; ++index){
            const u32 displacement = u16At(offset + s_OtTagListFirstOffsetField + index * s_OtTagRecordBytes);
            const u64 script = offset + displacement;
            if(displacement == 0u || !range(script, s_ScriptHeaderBytes))
                return false;
            const u32 defaultLang = u16At(script);
            const u32 languageCount = u16At(script + s_ScriptLangSysCountOffset);
            if((defaultLang != 0u && !langSys(script + defaultLang, featureCount)) || !range(script + s_ScriptHeaderBytes, languageCount * s_OtTagRecordBytes))
                return false;
            for(u32 language = 0u; language < languageCount; ++language){
                const u32 delta = u16At(script + s_ScriptFirstLangSysOffsetField + language * s_OtTagRecordBytes);
                if(delta == 0u || !langSys(script + delta, featureCount))
                    return false;
            }
        }
        return true;
    }

    [[nodiscard]] bool featureList(u64 offset, u32 lookupCount)noexcept{
        if(!range(offset, s_OtU16Bytes))
            return false;
        const u32 count = u16At(offset);
        if(!range(offset + s_OtU16Bytes, count * s_OtTagRecordBytes) || !spend(count))
            return false;
        for(u32 index = 0u; index < count; ++index){
            const u32 delta = u16At(offset + s_OtTagListFirstOffsetField + index * s_OtTagRecordBytes);
            const u64 feature = offset + delta;
            if(delta == 0u || !range(feature, s_FeatureHeaderBytes))
                return false;
            const u32 params = u16At(feature);
            const u32 itemCount = u16At(feature + s_FeatureLookupCountOffset);
            if((params != 0u && !range(feature + params, s_FeatureParamsMinBytes)) || !range(feature + s_FeatureHeaderBytes, itemCount * s_OtU16Bytes) || !spend(itemCount))
                return false;
            for(u32 item = 0u; item < itemCount; ++item){
                if(u16At(feature + s_FeatureHeaderBytes + item * s_OtU16Bytes) >= lookupCount)
                    return false;
            }
        }
        return true;
    }

    [[nodiscard]] bool pairPos(u64 offset)noexcept{
        if(!range(offset, s_PairPosSetHeaderBytes))
            return false;
        const u16 format = u16At(offset);
        const u16 firstFormat = u16At(offset + s_PairPosValueFormat1Offset);
        const u16 secondFormat = u16At(offset + s_PairPosValueFormat2Offset);
        if(u16At(offset + s_PosCoverageOffset) == 0u)
            return false;
        const auto coverageCount = coverage(offset + u16At(offset + s_PosCoverageOffset));
        if(!coverageCount || ((firstFormat | secondFormat) & s_ValueFormatReservedMask) != 0u)
            return false;
        const u32 stride = valueSize(firstFormat) + valueSize(secondFormat);
        if(format == 1u){
            const u32 count = u16At(offset + s_PairPosSetCountOffset);
            if(count != *coverageCount || !range(offset + s_PairPosSetHeaderBytes, count * s_OtU16Bytes))
                return false;
            for(u32 index = 0u; index < count; ++index){
                const u32 delta = u16At(offset + s_PairPosSetHeaderBytes + index * s_OtU16Bytes);
                const u64 set = offset + delta;
                if(delta == 0u || !range(set, s_OtU16Bytes))
                    return false;
                const u32 pairs = u16At(set);
                if(!range(set + s_OtU16Bytes, static_cast<u64>(pairs) * (stride + s_OtU16Bytes)) || !spend(pairs))
                    return false;
                u32 previous = 0u;
                for(u32 pair = 0u; pair < pairs; ++pair){
                    const u64 record = set + s_OtU16Bytes + pair * (stride + s_OtU16Bytes);
                    const u32 second = u16At(record);
                    if(second >= m_glyphCount || (pair > 0u && second <= previous) || !value(record + s_OtU16Bytes, firstFormat, set) || !value(record + s_OtU16Bytes + valueSize(firstFormat), secondFormat, set))
                        return false;
                    previous = second;
                }
            }
            return true;
        }
        if(format != s_PairPosClassFormat || !range(offset, s_PairPosClassHeaderBytes))
            return false;
        const u32 firstClasses = u16At(offset + s_PairPosClass1CountOffset);
        const u32 secondClasses = u16At(offset + s_PairPosClass2CountOffset);
        const u64 pairs = static_cast<u64>(firstClasses) * secondClasses;
        if(
            firstClasses == 0u || secondClasses == 0u || pairs > s_PairPosMaxClassPairs
            || u16At(offset + s_PairPosClassDef1Offset) == 0u || u16At(offset + s_PairPosClassDef2Offset) == 0u
            || !classDef(offset + u16At(offset + s_PairPosClassDef1Offset), firstClasses) || !classDef(offset + u16At(offset + s_PairPosClassDef2Offset), secondClasses)
            || !range(offset + s_PairPosClassHeaderBytes, pairs * stride) || !spend(static_cast<u32>(pairs))
        )
            return false;
        for(u32 pair = 0u; pair < pairs; ++pair){
            const u64 record = offset + s_PairPosClassHeaderBytes + static_cast<u64>(pair) * stride;
            if(!value(record, firstFormat, offset) || !value(record + valueSize(firstFormat), secondFormat, offset))
                return false;
        }
        return true;
    }

    [[nodiscard]] bool subtable(u64 offset, u32 type)noexcept{
        if(!range(offset, s_PosSubtableHeaderBytes))
            return false;
        if(type == s_GposExtensionLookupType){
            if(u16At(offset) != 1u || !range(offset, s_ExtensionPosBytes))
                return false;
            const u32 actualType = u16At(offset + s_ExtensionPosTypeOffset);
            const u32 delta = u32At(offset + s_ExtensionPosOffsetField);
            return actualType >= 1u && actualType <= s_GposChainedContextLookupType && delta != 0u && subtable(offset + delta, actualType);
        }
        if(type == s_GposPairLookupType)
            return pairPos(offset);
        const u32 format = u16At(offset);
        if(type == 1u){
            if(!range(offset, s_SinglePosHeaderBytes))
                return false;
            const u16 valueFormat = u16At(offset + s_SinglePosValueFormatOffset);
            if(u16At(offset + s_PosCoverageOffset) == 0u)
                return false;
            const auto count = coverage(offset + u16At(offset + s_PosCoverageOffset));
            if(!count)
                return false;
            if(format == 1u)
                return value(offset + s_SinglePosHeaderBytes, valueFormat, offset);
            if(format != s_SinglePosArrayFormat || !range(offset, s_SinglePosArrayHeaderBytes) || *count != u16At(offset + s_SinglePosValueCountOffset) || !spend(*count))
                return false;
            for(u32 index = 0u; index < *count; ++index){
                if(!value(offset + s_SinglePosArrayHeaderBytes + static_cast<u64>(index) * valueSize(valueFormat), valueFormat, offset))
                    return false;
            }
            return true;
        }
        // Non-pair positioning is retained opaque after bounded format/root admission; runtime executes the source font.
        if(type >= s_GposCursiveLookupType && type <= s_GposMarkMarkLookupType){
            return format == 1u && u16At(offset + s_PosCoverageOffset) != 0u && coverage(offset + u16At(offset + s_PosCoverageOffset));
        }
        return (type == s_GposContextLookupType || type == s_GposChainedContextLookupType) && format >= 1u && format <= s_ContextMaxFormat;
    }

    [[nodiscard]] bool lookupList(u64 offset)noexcept{
        if(!range(offset, s_OtU16Bytes))
            return false;
        const u32 count = u16At(offset);
        if(!range(offset + s_OtU16Bytes, count * s_OtU16Bytes) || !spend(count))
            return false;
        for(u32 index = 0u; index < count; ++index){
            const u32 delta = u16At(offset + s_OtU16Bytes + index * s_OtU16Bytes);
            const u64 lookup = offset + delta;
            if(delta == 0u || !range(lookup, s_LookupHeaderBytes))
                return false;
            const u32 type = u16At(lookup);
            const u32 flags = u16At(lookup + s_LookupFlagOffset);
            const u32 itemCount = u16At(lookup + s_LookupSubtableCountOffset);
            if(type < 1u || type > s_GposExtensionLookupType || (flags & s_LookupFlagReservedMask) != 0u || !range(lookup + s_LookupHeaderBytes, itemCount * s_OtU16Bytes + ((flags & s_LookupFlagUseMarkFilteringSet) != 0u ? s_OtU16Bytes : 0u)))
                return false;
            for(u32 item = 0u; item < itemCount; ++item){
                const u32 displacement = u16At(lookup + s_LookupHeaderBytes + item * s_OtU16Bytes);
                if(displacement == 0u || !subtable(lookup + displacement, type))
                    return false;
            }
        }
        return true;
    }


private:
    const Core::Assets::AssetBytes& m_bytes;
    u32 m_glyphCount;
    u32 m_budget = s_ValidationWorkBudget;
};

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ValidateFontAtlasPositioningTable(const FontAtlasPositioningTable& table, const u32 glyphCount){
    if(table.bytes.empty() || table.bytes.size() > s_FontAtlasMaxPositioningBytes || glyphCount == 0u || glyphCount > s_FontAtlasMaxGlyphCount)
        return false;
    __hidden_font_atlas_positioning::TableValidator validator(table.bytes, glyphCount);
    const bool valid = table.tag == s_FontAtlasKernTag ? validator.kern() : table.tag == s_FontAtlasGposTag ? validator.gpos() : table.tag == s_FontAtlasGdefTag ? validator.gdef() : false;
    if(!valid)
        NWB_LOGGER_ERROR(NWB_TEXT("font_atlas: invalid or over-budget raw positioning table {}"), table.tag);
    return valid;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

