// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "id.h"

#include <global/hash_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_id{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_IntegerByteCount = 8u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


u64 Append(u64 hash, u8 byte)noexcept{
    return (hash ^ byte) * s_Fnv64Prime;
}

u64 AppendInteger(u64 hash, const u64 value)noexcept{
    for(u32 index = 0u; index < s_IntegerByteCount; ++index)
        hash = Append(hash, static_cast<u8>(value >> (index * s_BitsPerByte)));
    return hash;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<WidgetId> MakeRootId(const WidgetRoot& root)noexcept{
    if(root.generation == 0u)
        return MakeUnexpected(Failure{});
    u64 hash = __hidden_ui_id::AppendInteger(s_Fnv64OffsetBasis, root.value);
    hash = __hidden_ui_id::AppendInteger(hash, root.generation);
    return WidgetId{ hash == 0u ? 1u : hash };
}

Expected<WidgetId> MakeWidgetId(const WidgetId parent, const AStringView stableKey)noexcept{
    if(!parent.valid() || stableKey.empty())
        return MakeUnexpected(Failure{});
    u64 hash = __hidden_ui_id::AppendInteger(s_Fnv64OffsetBasis, parent.value);
    hash = __hidden_ui_id::AppendInteger(hash, stableKey.size());
    for(const char byte : stableKey)
        hash = __hidden_ui_id::Append(hash, static_cast<u8>(byte));
    return WidgetId{ hash == 0u ? 1u : hash };
}

Expected<WidgetId> MakeWidgetPartId(const WidgetId parent, const u64 stableKey)noexcept{
    if(!parent.valid() || stableKey == 0u)
        return MakeUnexpected(Failure{});
    u64 hash = __hidden_ui_id::AppendInteger(s_Fnv64OffsetBasis, parent.value);
    hash = __hidden_ui_id::AppendInteger(hash, 0u);
    hash = __hidden_ui_id::AppendInteger(hash, stableKey);
    return WidgetId{ hash == 0u ? 1u : hash };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

