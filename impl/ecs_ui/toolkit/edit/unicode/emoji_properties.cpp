// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "properties.h"
#include "property_table.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_unicode_emoji{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Generated from the pinned Unicode 17.0.0 data in sources.json; see LICENSE.txt.
static constexpr UnicodePropertyRange s_Ranges[] = {
    { 0xA9u, 0xA9u, 1u },
    { 0xAEu, 0xAEu, 1u },
    { 0x203Cu, 0x203Cu, 1u },
    { 0x2049u, 0x2049u, 1u },
    { 0x2122u, 0x2122u, 1u },
    { 0x2139u, 0x2139u, 1u },
    { 0x2194u, 0x2199u, 1u },
    { 0x21A9u, 0x21AAu, 1u },
    { 0x231Au, 0x231Bu, 1u },
    { 0x2328u, 0x2328u, 1u },
    { 0x23CFu, 0x23CFu, 1u },
    { 0x23E9u, 0x23F3u, 1u },
    { 0x23F8u, 0x23FAu, 1u },
    { 0x24C2u, 0x24C2u, 1u },
    { 0x25AAu, 0x25ABu, 1u },
    { 0x25B6u, 0x25B6u, 1u },
    { 0x25C0u, 0x25C0u, 1u },
    { 0x25FBu, 0x25FEu, 1u },
    { 0x2600u, 0x2604u, 1u },
    { 0x260Eu, 0x260Eu, 1u },
    { 0x2611u, 0x2611u, 1u },
    { 0x2614u, 0x2615u, 1u },
    { 0x2618u, 0x2618u, 1u },
    { 0x261Du, 0x261Du, 1u },
    { 0x2620u, 0x2620u, 1u },
    { 0x2622u, 0x2623u, 1u },
    { 0x2626u, 0x2626u, 1u },
    { 0x262Au, 0x262Au, 1u },
    { 0x262Eu, 0x262Fu, 1u },
    { 0x2638u, 0x263Au, 1u },
    { 0x2640u, 0x2640u, 1u },
    { 0x2642u, 0x2642u, 1u },
    { 0x2648u, 0x2653u, 1u },
    { 0x265Fu, 0x2660u, 1u },
    { 0x2663u, 0x2663u, 1u },
    { 0x2665u, 0x2666u, 1u },
    { 0x2668u, 0x2668u, 1u },
    { 0x267Bu, 0x267Bu, 1u },
    { 0x267Eu, 0x267Fu, 1u },
    { 0x2692u, 0x2697u, 1u },
    { 0x2699u, 0x2699u, 1u },
    { 0x269Bu, 0x269Cu, 1u },
    { 0x26A0u, 0x26A1u, 1u },
    { 0x26A7u, 0x26A7u, 1u },
    { 0x26AAu, 0x26ABu, 1u },
    { 0x26B0u, 0x26B1u, 1u },
    { 0x26BDu, 0x26BEu, 1u },
    { 0x26C4u, 0x26C5u, 1u },
    { 0x26C8u, 0x26C8u, 1u },
    { 0x26CEu, 0x26CFu, 1u },
    { 0x26D1u, 0x26D1u, 1u },
    { 0x26D3u, 0x26D4u, 1u },
    { 0x26E9u, 0x26EAu, 1u },
    { 0x26F0u, 0x26F5u, 1u },
    { 0x26F7u, 0x26FAu, 1u },
    { 0x26FDu, 0x26FDu, 1u },
    { 0x2702u, 0x2702u, 1u },
    { 0x2705u, 0x2705u, 1u },
    { 0x2708u, 0x270Du, 1u },
    { 0x270Fu, 0x270Fu, 1u },
    { 0x2712u, 0x2712u, 1u },
    { 0x2714u, 0x2714u, 1u },
    { 0x2716u, 0x2716u, 1u },
    { 0x271Du, 0x271Du, 1u },
    { 0x2721u, 0x2721u, 1u },
    { 0x2728u, 0x2728u, 1u },
    { 0x2733u, 0x2734u, 1u },
    { 0x2744u, 0x2744u, 1u },
    { 0x2747u, 0x2747u, 1u },
    { 0x274Cu, 0x274Cu, 1u },
    { 0x274Eu, 0x274Eu, 1u },
    { 0x2753u, 0x2755u, 1u },
    { 0x2757u, 0x2757u, 1u },
    { 0x2763u, 0x2764u, 1u },
    { 0x2795u, 0x2797u, 1u },
    { 0x27A1u, 0x27A1u, 1u },
    { 0x27B0u, 0x27B0u, 1u },
    { 0x27BFu, 0x27BFu, 1u },
    { 0x2934u, 0x2935u, 1u },
    { 0x2B05u, 0x2B07u, 1u },
    { 0x2B1Bu, 0x2B1Cu, 1u },
    { 0x2B50u, 0x2B50u, 1u },
    { 0x2B55u, 0x2B55u, 1u },
    { 0x3030u, 0x3030u, 1u },
    { 0x303Du, 0x303Du, 1u },
    { 0x3297u, 0x3297u, 1u },
    { 0x3299u, 0x3299u, 1u },
    { 0x1F004u, 0x1F004u, 1u },
    { 0x1F02Cu, 0x1F02Fu, 1u },
    { 0x1F094u, 0x1F09Fu, 1u },
    { 0x1F0AFu, 0x1F0B0u, 1u },
    { 0x1F0C0u, 0x1F0C0u, 1u },
    { 0x1F0CFu, 0x1F0D0u, 1u },
    { 0x1F0F6u, 0x1F0FFu, 1u },
    { 0x1F170u, 0x1F171u, 1u },
    { 0x1F17Eu, 0x1F17Fu, 1u },
    { 0x1F18Eu, 0x1F18Eu, 1u },
    { 0x1F191u, 0x1F19Au, 1u },
    { 0x1F1AEu, 0x1F1E5u, 1u },
    { 0x1F201u, 0x1F20Fu, 1u },
    { 0x1F21Au, 0x1F21Au, 1u },
    { 0x1F22Fu, 0x1F22Fu, 1u },
    { 0x1F232u, 0x1F23Au, 1u },
    { 0x1F23Cu, 0x1F23Fu, 1u },
    { 0x1F249u, 0x1F25Fu, 1u },
    { 0x1F266u, 0x1F321u, 1u },
    { 0x1F324u, 0x1F393u, 1u },
    { 0x1F396u, 0x1F397u, 1u },
    { 0x1F399u, 0x1F39Bu, 1u },
    { 0x1F39Eu, 0x1F3F0u, 1u },
    { 0x1F3F3u, 0x1F3F5u, 1u },
    { 0x1F3F7u, 0x1F3FAu, 1u },
    { 0x1F400u, 0x1F4FDu, 1u },
    { 0x1F4FFu, 0x1F53Du, 1u },
    { 0x1F549u, 0x1F54Eu, 1u },
    { 0x1F550u, 0x1F567u, 1u },
    { 0x1F56Fu, 0x1F570u, 1u },
    { 0x1F573u, 0x1F57Au, 1u },
    { 0x1F587u, 0x1F587u, 1u },
    { 0x1F58Au, 0x1F58Du, 1u },
    { 0x1F590u, 0x1F590u, 1u },
    { 0x1F595u, 0x1F596u, 1u },
    { 0x1F5A4u, 0x1F5A5u, 1u },
    { 0x1F5A8u, 0x1F5A8u, 1u },
    { 0x1F5B1u, 0x1F5B2u, 1u },
    { 0x1F5BCu, 0x1F5BCu, 1u },
    { 0x1F5C2u, 0x1F5C4u, 1u },
    { 0x1F5D1u, 0x1F5D3u, 1u },
    { 0x1F5DCu, 0x1F5DEu, 1u },
    { 0x1F5E1u, 0x1F5E1u, 1u },
    { 0x1F5E3u, 0x1F5E3u, 1u },
    { 0x1F5E8u, 0x1F5E8u, 1u },
    { 0x1F5EFu, 0x1F5EFu, 1u },
    { 0x1F5F3u, 0x1F5F3u, 1u },
    { 0x1F5FAu, 0x1F64Fu, 1u },
    { 0x1F680u, 0x1F6C5u, 1u },
    { 0x1F6CBu, 0x1F6D2u, 1u },
    { 0x1F6D5u, 0x1F6E5u, 1u },
    { 0x1F6E9u, 0x1F6E9u, 1u },
    { 0x1F6EBu, 0x1F6F0u, 1u },
    { 0x1F6F3u, 0x1F6FFu, 1u },
    { 0x1F7DAu, 0x1F7FFu, 1u },
    { 0x1F80Cu, 0x1F80Fu, 1u },
    { 0x1F848u, 0x1F84Fu, 1u },
    { 0x1F85Au, 0x1F85Fu, 1u },
    { 0x1F888u, 0x1F88Fu, 1u },
    { 0x1F8AEu, 0x1F8AFu, 1u },
    { 0x1F8BCu, 0x1F8BFu, 1u },
    { 0x1F8C2u, 0x1F8CFu, 1u },
    { 0x1F8D9u, 0x1F8FFu, 1u },
    { 0x1F90Cu, 0x1F93Au, 1u },
    { 0x1F93Cu, 0x1F945u, 1u },
    { 0x1F947u, 0x1F9FFu, 1u },
    { 0x1FA58u, 0x1FA5Fu, 1u },
    { 0x1FA6Eu, 0x1FAFFu, 1u },
    { 0x1FC00u, 0x1FFFDu, 1u },
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool IsExtendedPictographic(const u32 codePoint)noexcept{
    const u8 property = LookupUnicodePropertyRanges(__hidden_ui_unicode_emoji::s_Ranges, codePoint, 0u);
    return property != 0u;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

