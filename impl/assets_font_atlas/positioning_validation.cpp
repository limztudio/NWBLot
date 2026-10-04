// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "model.h"

#include <logger/client/logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_font_atlas_positioning{

class TableValidator final{
public:
    TableValidator(const Core::Assets::AssetBytes& bytes, u32 glyphCount)
        : m_bytes(bytes)
        , m_glyphCount(glyphCount)
    {}


public:
    [[nodiscard]] bool kern(){
        if(!range(0u, 4u))
            return false;
        const bool apple = u32At(0u) == 0x00010000u;
        if(!apple && u16At(0u) != 0u)
            return false;
        if(apple && !range(0u, 8u))
            return false;
        const u32 tableCount = apple ? u32At(4u) : u16At(2u);
        if(tableCount > 65535u)
            return false;
        u64 offset = apple ? 8u : 4u;
        for(u32 table = 0u; table < tableCount; ++table){
            const u32 header = apple ? 8u : 6u;
            if(!range(offset, header))
                return false;
            const u32 length = apple ? u32At(offset) : u16At(offset + 2u);
            const u16 coverage = u16At(offset + 4u);
            const u16 format = apple ? coverage & 255u : coverage >> 8u;
            if(length < header || !range(offset, length))
                return false;
            if(format == 0u){
                if(length < header + 8u)
                    return false;
                const u32 count = u16At(offset + header);
                if(static_cast<u64>(count) * 6u > length - header - 8u || !spend(count))
                    return false;
                u32 previous = 0u;
                for(u32 index = 0u; index < count; ++index){
                    const u64 record = offset + header + 8u + index * 6u;
                    const u32 left = u16At(record);
                    const u32 right = u16At(record + 2u);
                    const u32 pair = left * 65536u + right;
                    if(left >= m_glyphCount || right >= m_glyphCount || (index > 0u && pair <= previous))
                        return false;
                    previous = pair;
                }
            }
            offset += length;
        }
        return offset == m_bytes.size();
    }

    [[nodiscard]] bool gpos(){
        if(!range(0u, 10u) || u16At(0u) != 1u || u16At(2u) > 1u)
            return false;
        const u64 scripts = u16At(4u);
        const u64 features = u16At(6u);
        const u64 lookups = u16At(8u);
        if(scripts < 10u || features < 10u || lookups < 10u || !range(features, 2u) || !range(lookups, 2u))
            return false;
        const u32 featureCount = u16At(features);
        const u32 lookupCount = u16At(lookups);
        if(!scriptList(scripts, featureCount) || !featureList(features, lookupCount) || !lookupList(lookups))
            return false;
        if(u16At(2u) == 1u){
            if(!range(0u, 14u))
                return false;
            const u64 variations = u32At(10u);
            if(
                variations != 0u && (!range(variations, 8u) || u16At(variations) != 1u || u16At(variations + 2u) != 0u
                || !range(variations + 8u, static_cast<u64>(u32At(variations + 4u)) * 8u))
            )
                return false;
        }
        return true;
    }

    [[nodiscard]] bool gdef(){
        if(!range(0u, 12u) || u16At(0u) != 1u || u16At(2u) > 3u || u16At(2u) == 1u)
            return false;
        const u32 minor = u16At(2u);
        const u32 headerSize = minor == 3u ? 18u : minor == 2u ? 14u : 12u;
        if(!range(0u, headerSize))
            return false;
        if(!classDef(u16At(4u), 5u) || !classDef(u16At(10u), 65536u))
            return false;
        for(u32 field = 6u; field <= 8u; field += 2u){
            const u64 list = u16At(field);
            if(list == 0u)
                continue;
            if(!range(list, 4u))
                return false;
            u32 coverageCount = 0u;
            const u32 count = u16At(list + 2u);
            if(!coverage(list + u16At(list), coverageCount) || count != coverageCount || !range(list + 4u, count * 2u))
                return false;
            for(u32 index = 0u; index < count; ++index){
                const u64 item = list + u16At(list + 4u + index * 2u);
                if(item == list || !range(item, 2u) || !range(item + 2u, u16At(item) * 2u))
                    return false;
                if(field == 8u){
                    for(u32 caret = 0u; caret < u16At(item); ++caret){
                        const u64 target = item + u16At(item + 2u + caret * 2u);
                        if(target == item || !range(target, 4u))
                            return false;
                        const u16 format = u16At(target);
                        if(format < 1u || format > 3u || (format == 3u && (!range(target, 6u) || !device(target, u16At(target + 4u)))))
                            return false;
                    }
                }
            }
        }
        if(minor >= 2u && u16At(12u) != 0u){
            const u64 sets = u16At(12u);
            if(!range(sets, 4u) || u16At(sets) != 1u || !range(sets + 4u, u16At(sets + 2u) * 4u))
                return false;
            for(u32 index = 0u; index < u16At(sets + 2u); ++index){
                u32 count = 0u;
                const u32 offset = u32At(sets + 4u + index * 4u);
                if(offset == 0u || !coverage(sets + offset, count))
                    return false;
            }
        }
        if(minor == 3u && u32At(14u) != 0u){
            const u64 store = u32At(14u);
            if(!range(store, 8u) || u16At(store) != 1u || !range(store + 8u, u16At(store + 6u) * 4u))
                return false;
        }
        return true;
    }


private:
    [[nodiscard]] bool range(u64 offset, u64 count){
        return spend(1u) && offset <= m_bytes.size() && count <= m_bytes.size() - offset;
    }
    [[nodiscard]] bool spend(u32 count){
        if(count > m_budget)
            return false;
        m_budget -= count;
        return true;
    }
    [[nodiscard]] u16 u16At(u64 offset)const{
        return static_cast<u16>((static_cast<u16>(m_bytes[offset]) << 8u) | m_bytes[offset + 1u]);
    }
    [[nodiscard]] u32 u32At(u64 offset)const{
        return (static_cast<u32>(u16At(offset)) << 16u) | u16At(offset + 2u);
    }
    [[nodiscard]] bool coverage(u64 offset, u32& outCount){
        if(!range(offset, 4u))
            return false;
        const u16 format = u16At(offset);
        const u32 count = u16At(offset + 2u);
        outCount = 0u;
        if(format != 1u && format != 2u)
            return false;
        if(!range(offset + 4u, count * (format == 1u ? 2u : 6u)) || !spend(count))
            return false;
        u32 previous = 0u;
        for(u32 index = 0u; index < count; ++index){
            const u64 record = offset + 4u + index * (format == 1u ? 2u : 6u);
            const u32 start = u16At(record);
            const u32 end = format == 1u ? start : u16At(record + 2u);
            if(
                start > end || end >= m_glyphCount || (index > 0u && start <= previous)
                || (format == 2u && u16At(record + 4u) != outCount)
            )
                return false;
            outCount += end - start + 1u;
            previous = end;
        }
        return true;
    }
    [[nodiscard]] bool classDef(u64 offset, u32 classCount){
        if(offset == 0u)
            return true;
        if(!range(offset, 4u))
            return false;
        const u16 format = u16At(offset);
        if(format == 1u){
            if(!range(offset, 6u))
                return false;
            const u32 start = u16At(offset + 2u);
            const u32 count = u16At(offset + 4u);
            if(start + count > m_glyphCount || !range(offset + 6u, count * 2u) || !spend(count))
                return false;
            for(u32 index = 0u; index < count; ++index){
                if(u16At(offset + 6u + index * 2u) >= classCount)
                    return false;
            }
            return true;
        }
        if(format != 2u)
            return false;
        const u32 count = u16At(offset + 2u);
        if(!range(offset + 4u, count * 6u) || !spend(count))
            return false;
        u32 previous = 0u;
        for(u32 index = 0u; index < count; ++index){
            const u64 record = offset + 4u + index * 6u;
            const u32 start = u16At(record);
            const u32 end = u16At(record + 2u);
            if(start > end || end >= m_glyphCount || (index > 0u && start <= previous) || u16At(record + 4u) >= classCount)
                return false;
            previous = end;
        }
        return true;
    }
    [[nodiscard]] bool device(u64 parent, u32 offset){
        if(offset == 0u)
            return true;
        const u64 table = parent + offset;
        if(!range(table, 6u))
            return false;
        const u32 format = u16At(table + 4u);
        if(format == 0x8000u)
            return true;
        const u32 start = u16At(table);
        const u32 end = u16At(table + 2u);
        if(format < 1u || format > 3u || start > end)
            return false;
        const u32 bits = 1u << format;
        return range(table + 6u, ((end - start + 1u) * bits + 15u) / 16u * 2u);
    }
    [[nodiscard]] u32 valueSize(u16 format)const{
        u32 size = 0u;
        for(u32 bit = 0u; bit < 8u; ++bit)
            size += (format & (1u << bit)) != 0u ? 2u : 0u;
        return size;
    }
    [[nodiscard]] bool value(u64 offset, u16 format, u64 parent){
        if((format & 0xff00u) != 0u || !range(offset, valueSize(format)))
            return false;
        u32 field = 0u;
        for(u32 bit = 0u; bit < 8u; ++bit){
            if((format & (1u << bit)) == 0u)
                continue;
            if(bit >= 4u && !device(parent, u16At(offset + field)))
                return false;
            field += 2u;
        }
        return true;
    }
    [[nodiscard]] bool langSys(u64 offset, u32 featureCount){
        if(!range(offset, 6u) || u16At(offset) != 0u)
            return false;
        const u32 required = u16At(offset + 2u);
        const u32 count = u16At(offset + 4u);
        if((required != 65535u && required >= featureCount) || !range(offset + 6u, count * 2u) || !spend(count))
            return false;
        for(u32 index = 0u; index < count; ++index){
            if(u16At(offset + 6u + index * 2u) >= featureCount)
                return false;
        }
        return true;
    }
    [[nodiscard]] bool scriptList(u64 offset, u32 featureCount){
        if(!range(offset, 2u))
            return false;
        const u32 count = u16At(offset);
        if(!range(offset + 2u, count * 6u) || !spend(count))
            return false;
        for(u32 index = 0u; index < count; ++index){
            const u32 displacement = u16At(offset + 6u + index * 6u);
            const u64 script = offset + displacement;
            if(displacement == 0u || !range(script, 4u))
                return false;
            const u32 defaultLang = u16At(script);
            const u32 languageCount = u16At(script + 2u);
            if((defaultLang != 0u && !langSys(script + defaultLang, featureCount)) || !range(script + 4u, languageCount * 6u))
                return false;
            for(u32 language = 0u; language < languageCount; ++language){
                const u32 delta = u16At(script + 8u + language * 6u);
                if(delta == 0u || !langSys(script + delta, featureCount))
                    return false;
            }
        }
        return true;
    }
    [[nodiscard]] bool featureList(u64 offset, u32 lookupCount){
        if(!range(offset, 2u))
            return false;
        const u32 count = u16At(offset);
        if(!range(offset + 2u, count * 6u) || !spend(count))
            return false;
        for(u32 index = 0u; index < count; ++index){
            const u32 delta = u16At(offset + 6u + index * 6u);
            const u64 feature = offset + delta;
            if(delta == 0u || !range(feature, 4u))
                return false;
            const u32 params = u16At(feature);
            const u32 itemCount = u16At(feature + 2u);
            if((params != 0u && !range(feature + params, 2u)) || !range(feature + 4u, itemCount * 2u) || !spend(itemCount))
                return false;
            for(u32 item = 0u; item < itemCount; ++item){
                if(u16At(feature + 4u + item * 2u) >= lookupCount)
                    return false;
            }
        }
        return true;
    }
    [[nodiscard]] bool pairPos(u64 offset){
        if(!range(offset, 10u))
            return false;
        const u16 format = u16At(offset);
        const u16 firstFormat = u16At(offset + 4u);
        const u16 secondFormat = u16At(offset + 6u);
        u32 coverageCount = 0u;
        if(
            u16At(offset + 2u) == 0u || !coverage(offset + u16At(offset + 2u), coverageCount)
            || ((firstFormat | secondFormat) & 0xff00u) != 0u
        )
            return false;
        const u32 stride = valueSize(firstFormat) + valueSize(secondFormat);
        if(format == 1u){
            const u32 count = u16At(offset + 8u);
            if(count != coverageCount || !range(offset + 10u, count * 2u))
                return false;
            for(u32 index = 0u; index < count; ++index){
                const u32 delta = u16At(offset + 10u + index * 2u);
                const u64 set = offset + delta;
                if(delta == 0u || !range(set, 2u))
                    return false;
                const u32 pairs = u16At(set);
                if(!range(set + 2u, static_cast<u64>(pairs) * (stride + 2u)) || !spend(pairs))
                    return false;
                u32 previous = 0u;
                for(u32 pair = 0u; pair < pairs; ++pair){
                    const u64 record = set + 2u + pair * (stride + 2u);
                    const u32 second = u16At(record);
                    if(
                        second >= m_glyphCount || (pair > 0u && second <= previous)
                        || !value(record + 2u, firstFormat, set) || !value(record + 2u + valueSize(firstFormat), secondFormat, set)
                    )
                        return false;
                    previous = second;
                }
            }
            return true;
        }
        if(format != 2u || !range(offset, 16u))
            return false;
        const u32 firstClasses = u16At(offset + 12u);
        const u32 secondClasses = u16At(offset + 14u);
        const u64 pairs = static_cast<u64>(firstClasses) * secondClasses;
        if(
            firstClasses == 0u || secondClasses == 0u || pairs > 262144u
            || u16At(offset + 8u) == 0u || u16At(offset + 10u) == 0u
            || !classDef(offset + u16At(offset + 8u), firstClasses) || !classDef(offset + u16At(offset + 10u), secondClasses)
            || !range(offset + 16u, pairs * stride) || !spend(static_cast<u32>(pairs))
        )
            return false;
        for(u32 pair = 0u; pair < pairs; ++pair){
            const u64 record = offset + 16u + static_cast<u64>(pair) * stride;
            if(!value(record, firstFormat, offset) || !value(record + valueSize(firstFormat), secondFormat, offset))
                return false;
        }
        return true;
    }
    [[nodiscard]] bool subtable(u64 offset, u32 type){
        if(!range(offset, 4u))
            return false;
        if(type == 9u){
            if(u16At(offset) != 1u || !range(offset, 8u))
                return false;
            const u32 actualType = u16At(offset + 2u);
            const u32 delta = u32At(offset + 4u);
            return actualType >= 1u && actualType <= 8u && delta != 0u && subtable(offset + delta, actualType);
        }
        if(type == 2u)
            return pairPos(offset);
        const u32 format = u16At(offset);
        if(type == 1u){
            if(!range(offset, 6u))
                return false;
            u32 count = 0u;
            const u16 valueFormat = u16At(offset + 4u);
            if(u16At(offset + 2u) == 0u || !coverage(offset + u16At(offset + 2u), count))
                return false;
            if(format == 1u)
                return value(offset + 6u, valueFormat, offset);
            if(format != 2u || !range(offset, 8u) || count != u16At(offset + 6u) || !spend(count))
                return false;
            for(u32 index = 0u; index < count; ++index){
                if(!value(offset + 8u + static_cast<u64>(index) * valueSize(valueFormat), valueFormat, offset))
                    return false;
            }
            return true;
        }
        // Non-pair positioning is retained opaque after bounded format/root admission; runtime executes the source font.
        if(type >= 3u && type <= 6u){
            u32 count = 0u;
            return format == 1u && u16At(offset + 2u) != 0u && coverage(offset + u16At(offset + 2u), count);
        }
        return (type == 7u || type == 8u) && format >= 1u && format <= 3u;
    }
    [[nodiscard]] bool lookupList(u64 offset){
        if(!range(offset, 2u))
            return false;
        const u32 count = u16At(offset);
        if(!range(offset + 2u, count * 2u) || !spend(count))
            return false;
        for(u32 index = 0u; index < count; ++index){
            const u32 delta = u16At(offset + 2u + index * 2u);
            const u64 lookup = offset + delta;
            if(delta == 0u || !range(lookup, 6u))
                return false;
            const u32 type = u16At(lookup);
            const u32 flags = u16At(lookup + 2u);
            const u32 itemCount = u16At(lookup + 4u);
            if(
                type < 1u || type > 9u || (flags & 0x00e0u) != 0u
                || !range(lookup + 6u, itemCount * 2u + ((flags & 0x10u) != 0u ? 2u : 0u))
            )
                return false;
            for(u32 item = 0u; item < itemCount; ++item){
                const u32 displacement = u16At(lookup + 6u + item * 2u);
                if(displacement == 0u || !subtable(lookup + displacement, type))
                    return false;
            }
        }
        return true;
    }


private:
    const Core::Assets::AssetBytes& m_bytes;
    u32 m_glyphCount;
    u32 m_budget = 1048576u;
};

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ValidateFontAtlasPositioningTable(const FontAtlasPositioningTable& table, const u32 glyphCount){
    if(table.bytes.empty() || table.bytes.size() > s_FontAtlasMaxPositioningBytes || glyphCount == 0u || glyphCount > s_FontAtlasMaxGlyphCount)
        return false;
    __hidden_font_atlas_positioning::TableValidator validator(table.bytes, glyphCount);
    const bool valid = table.tag == s_FontAtlasKernTag ? validator.kern()
        : table.tag == s_FontAtlasGposTag ? validator.gpos()
        : table.tag == s_FontAtlasGdefTag ? validator.gdef()
        : false;
    if(!valid)
        NWB_LOGGER_ERROR(GLOBAL_TEXT("font_atlas: invalid or over-budget raw positioning table {}"), table.tag);
    return valid;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

