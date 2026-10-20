// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "properties.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Unicode 17.0.0 UAX #29 revision 47 default extended grapheme boundaries, including GB9c.
struct GraphemeBoundaryState{
    GraphemeBreak::Enum previous = GraphemeBreak::Other;
    usize regionalRun = 0u;
    bool pictographExtendRun = false;
    bool previousZwjAfterPictograph = false;
    bool indicConsonantRun = false;
    bool indicLinker = false;
    bool first = true;


private:
    static bool IsControl(const GraphemeBreak::Enum property)noexcept{
        return property == GraphemeBreak::CR || property == GraphemeBreak::LF || property == GraphemeBreak::Control;
    }


public:
    [[nodiscard]] bool breaksBefore(const GraphemeBreak::Enum current, const IndicConjunct::Enum indic, const bool pictograph)const noexcept{
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

    void advance(const GraphemeBreak::Enum current, const IndicConjunct::Enum indic, const bool pictograph)noexcept{
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

