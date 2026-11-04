// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "properties.h"

#include "../containers.h"
#include "../text_trim_parse.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Unicode 17.0.0 UAX #29 revision 47 default extended grapheme boundaries, including GB9c.
struct GraphemeBoundaryState{
    usize regionalRun = 0u;
    GraphemeBreak::Enum previous = GraphemeBreak::Other;
    b8 pictographExtendRun = false;
    b8 previousZwjAfterPictograph = false;
    b8 indicConsonantRun = false;
    b8 indicLinker = false;
    b8 first = true;


private:
    static b8 IsControl(const GraphemeBreak::Enum property)noexcept{
        return property == GraphemeBreak::CR || property == GraphemeBreak::LF || property == GraphemeBreak::Control;
    }


public:
    [[nodiscard]] b8 breaksBefore(const GraphemeBreak::Enum current, const IndicConjunct::Enum indic, const b8 pictograph)const noexcept{
        if(first)
            return true;
        if(previous == GraphemeBreak::CR && current == GraphemeBreak::LF)
            return false;
        if(IsControl(previous) || IsControl(current))
            return true;
        if(previous == GraphemeBreak::L && (current == GraphemeBreak::L || current == GraphemeBreak::V || current == GraphemeBreak::LV || current == GraphemeBreak::LVT))
            return false;
        if((previous == GraphemeBreak::LV || previous == GraphemeBreak::V) && (current == GraphemeBreak::V || current == GraphemeBreak::T))
            return false;
        if((previous == GraphemeBreak::LVT || previous == GraphemeBreak::T) && current == GraphemeBreak::T)
            return false;
        if(current == GraphemeBreak::Extend || current == GraphemeBreak::ZWJ || current == GraphemeBreak::SpacingMark)
            return false;
        if(previous == GraphemeBreak::Prepend)
            return false;
        if(indic == IndicConjunct::Consonant && indicConsonantRun && indicLinker)
            return false;
        if(pictograph && previous == GraphemeBreak::ZWJ && previousZwjAfterPictograph)
            return false;
        if(previous == GraphemeBreak::Regional_Indicator && current == GraphemeBreak::Regional_Indicator && regionalRun % 2u != 0u)
            return false;
        return true;
    }

    void advance(const GraphemeBreak::Enum current, const IndicConjunct::Enum indic, const b8 pictograph)noexcept{
        previousZwjAfterPictograph = current == GraphemeBreak::ZWJ && pictographExtendRun;
        if(pictograph)
            pictographExtendRun = true;
        else if(current != GraphemeBreak::Extend)
            pictographExtendRun = false;
        if(indic == IndicConjunct::Consonant){
            indicConsonantRun = true;
            indicLinker = false;
        }
        else if(indic == IndicConjunct::Linker){
            if(indicConsonantRun)
                indicLinker = true;
        }
        else if(indic != IndicConjunct::Extend){
            indicConsonantRun = false;
            indicLinker = false;
        }
        regionalRun = current == GraphemeBreak::Regional_Indicator ? regionalRun + 1u : 0u;
        previous = current;
        first = false;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
[[nodiscard]] inline Expected<Vector<usize, ArenaT>> BuildGraphemeBoundaries(ArenaT& arena, const AStringView text){
    if(text.size() == Limit<usize>::s_Max)
        return MakeUnexpected(Failure{});
    Vector<usize, ArenaT> candidate(arena);
    candidate.reserve(text.size() + 1u);
    GraphemeBoundaryState state;
    usize offset = 0u;
    while(offset < text.size()){
        const auto decoded = DecodeUtf8Scalar(text.substr(offset, s_Utf8MaxSequenceBytes));
        if(!decoded)
            return MakeUnexpected(Failure{});
        const u32 scalar = decoded->codePoint;
        const GraphemeBreak::Enum current = LookupGraphemeBreak(scalar);
        const IndicConjunct::Enum indic = LookupIndicConjunct(scalar);
        const b8 pictograph = IsExtendedPictographic(scalar);
        if(state.breaksBefore(current, indic, pictograph))
            candidate.push_back(offset);
        state.advance(current, indic, pictograph);
        offset += static_cast<usize>(decoded->byteCount);
    }
    candidate.push_back(text.size());
    return candidate;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

