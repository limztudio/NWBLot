// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "name_hash.h"
#include "numeric_text.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class NameHashTextCodec final{
public:
    static constexpr usize s_HexDigitsPerLane = sizeof(u64) * 2u;
    static constexpr usize s_TextLength = s_HexDigitsPerLane * s_NameHashLaneCount + (s_NameHashLaneCount - 1u);


public:
    template<typename CharT>
    [[nodiscard]] static Expected<Array<char, s_TextLength + 1u>> CopyToken(const BasicStringView<CharT> text, const usize offset)noexcept(IsArithmetic_V<CharT>){
        Array<char, s_TextLength + 1u> hashText;
        if(offset > text.size() || s_TextLength > text.size() - offset)
            return MakeUnexpected(Failure{});
        if(offset > 0u && IsTokenChar(text[offset - 1u]))
            return MakeUnexpected(Failure{});
        if(offset + s_TextLength < text.size() && IsTokenChar(text[offset + s_TextLength]))
            return MakeUnexpected(Failure{});

        for(usize i = 0u; i < s_TextLength; ++i){
            const CharT ch = text[offset + i];
            if(((i + 1u) % (s_HexDigitsPerLane + 1u)) == 0u){
                if(ch != static_cast<CharT>('_'))
                    return MakeUnexpected(Failure{});
            }
            else if(!IsTokenChar(ch) || ch == static_cast<CharT>('_')){
                return MakeUnexpected(Failure{});
            }

            hashText[i] = static_cast<char>(ch);
        }
        hashText[s_TextLength] = 0;
        return hashText;
    }

    [[nodiscard]] static Expected<NameHash> Decode(const AStringView text)noexcept{
        if(text.size() != s_TextLength)
            return MakeUnexpected(Failure{});

        NameHash hash;
        usize cursor = 0u;
        for(u32 lane = 0u; lane < s_NameHashLaneCount; ++lane){
            if((lane > 0u && text[cursor++] != '_') || cursor + s_HexDigitsPerLane > text.size())
                return MakeUnexpected(Failure{});

            const auto laneHash = ParseHexU64<char>(AStringView(text.data() + cursor, s_HexDigitsPerLane));
            if(!laneHash)
                return MakeUnexpected(Failure{});
            hash.qwords[lane] = *laneHash;
            cursor += s_HexDigitsPerLane;
        }

        if(cursor != text.size())
            return MakeUnexpected(Failure{});
        return hash;
    }

    template<typename CharT>
    static void Format(CharT* dst, const usize dstSize, const NameHash& hash)noexcept(IsArithmetic_V<CharT>){
        if(dstSize == 0)
            return;

        if(dstSize <= s_TextLength){
            dst[0] = CharT{};
            return;
        }

        CharT* writeCursor = dst;
        for(u32 i = 0u; i < s_NameHashLaneCount; ++i){
            if(i > 0){
                *writeCursor = static_cast<CharT>('_');
                ++writeCursor;
            }
            const u64 value = hash.qwords[i];
            for(i32 bitShift = s_HashHexInitialBitShift; bitShift >= 0; bitShift -= s_HashHexBitShiftStep){
                *writeCursor = NumericTextDetail::HexDigit<CharT>(static_cast<usize>((value >> bitShift) & s_HexNibbleMask));
                ++writeCursor;
            }
        }
        *writeCursor = CharT{};
    }


private:
    static constexpr i32 s_HashHexInitialBitShift = static_cast<i32>((s_HexU64DigitCount - 1u) * s_HexNibbleBits);
    static constexpr i32 s_HashHexBitShiftStep = static_cast<i32>(s_HexNibbleBits);


private:
    template<typename CharT>
    [[nodiscard]] static b8 IsTokenChar(const CharT ch)noexcept(IsArithmetic_V<CharT>){
        return (ch >= static_cast<CharT>('0') && ch <= static_cast<CharT>('9')) || (ch >= static_cast<CharT>('a') && ch <= static_cast<CharT>('f')) || (ch >= static_cast<CharT>('A') && ch <= static_cast<CharT>('F')) || ch == static_cast<CharT>('_');
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

