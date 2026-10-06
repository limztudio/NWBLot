// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "grapheme.h"
#include "unicode/properties.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_grapheme{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool Decode(const AStringView text, const usize offset, u32& scalar, usize& length){
    const i32 decoded = DecodeUtf8CodePoint(text.substr(offset, 4u), scalar);
    if(decoded <= 0 || (scalar >= 0xD800u && scalar <= 0xDFFFu))
        return false;
    length = static_cast<usize>(decoded);
    return true;
}

static bool IsSingleLineScalar(const u32 scalar)noexcept{
    return scalar != 0u && scalar != 0xAu && scalar != 0xDu && scalar != 0x2028u && scalar != 0x2029u;
}

static bool IsControl(const GraphemeBreak::Enum property)noexcept{
    return property == GraphemeBreak::CR || property == GraphemeBreak::LF || property == GraphemeBreak::Control;
}

struct BoundaryState{
    GraphemeBreak::Enum previous = GraphemeBreak::Other;
    usize regionalRun = 0u;
    bool pictographExtendRun = false;
    bool previousZwjAfterPictograph = false;
    bool indicConsonantRun = false;
    bool indicLinker = false;
    bool first = true;

    [[nodiscard]] bool breaksBefore(const GraphemeBreak::Enum current, const IndicConjunct::Enum indic, const bool pictograph)const noexcept{
        if(first)
            return true;
        if(previous == GraphemeBreak::CR && current == GraphemeBreak::LF)
            return false;
        if(IsControl(previous) || IsControl(current))
            return true;
        if(
            previous == GraphemeBreak::L && (current == GraphemeBreak::L || current == GraphemeBreak::V
                || current == GraphemeBreak::LV || current == GraphemeBreak::LVT)
        )
            return false;
        if(
            (previous == GraphemeBreak::LV || previous == GraphemeBreak::V)
            && (current == GraphemeBreak::V || current == GraphemeBreak::T)
        )
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
        if(
            previous == GraphemeBreak::Regional_Indicator && current == GraphemeBreak::Regional_Indicator
            && regionalRun % 2u != 0u
        )
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


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GraphemeSegmentation::Build(const AStringView text, EditBoundaryVector& output, const bool singleLine){
    if(!Validate(text, singleLine))
        return false;
    EditBoundaryVector candidate(output.get_allocator());
    candidate.reserve(text.size() + 1u);
    __hidden_ui_grapheme::BoundaryState state;
    usize offset = 0u;
    while(offset < text.size()){
        u32 scalar = 0u;
        usize length = 0u;
        if(!__hidden_ui_grapheme::Decode(text, offset, scalar, length))
            return false;
        const GraphemeBreak::Enum current = LookupGraphemeBreak(scalar);
        const IndicConjunct::Enum indic = LookupIndicConjunct(scalar);
        const bool pictograph = IsExtendedPictographic(scalar);
        if(state.breaksBefore(current, indic, pictograph))
            candidate.push_back(offset);
        state.advance(current, indic, pictograph);
        offset += length;
    }
    candidate.push_back(text.size());
    output = Move(candidate);
    return true;
}

bool GraphemeSegmentation::Validate(const AStringView text, const bool singleLine){
    if(text.size() > Limit<i32>::s_Max || (!text.empty() && text.data() == nullptr))
        return false;
    usize offset = 0u;
    while(offset < text.size()){
        u32 scalar = 0u;
        usize length = 0u;
        if(!__hidden_ui_grapheme::Decode(text, offset, scalar, length))
            return false;
        if(singleLine && !__hidden_ui_grapheme::IsSingleLineScalar(scalar))
            return false;
        offset += length;
    }
    return true;
}

bool GraphemeSegmentation::IsScalarBoundary(const AStringView text, const usize position)noexcept{
    if(position > text.size())
        return false;
    return position == text.size() || !IsUtf8Continuation(static_cast<u8>(text[position]));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

