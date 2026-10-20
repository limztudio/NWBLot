// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "id.h"

#include <global/hash_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<WidgetId> MakeRootId(const WidgetRoot& root)noexcept{
    if(root.generation == 0u)
        return MakeUnexpected(Failure{});
    u64 hash = UpdateFnv64U64LittleEndian(s_Fnv64OffsetBasis, root.value);
    hash = UpdateFnv64U64LittleEndian(hash, root.generation);
    return WidgetId{ hash == 0u ? 1u : hash };
}

Expected<WidgetId> MakeWidgetId(const WidgetId parent, const AStringView stableKey)noexcept{
    if(!parent.valid() || stableKey.empty())
        return MakeUnexpected(Failure{});
    u64 hash = UpdateFnv64U64LittleEndian(s_Fnv64OffsetBasis, parent.value);
    hash = UpdateFnv64U64LittleEndian(hash, stableKey.size());
    for(const char byte : stableKey)
        hash = UpdateFnv64Byte(hash, static_cast<u8>(byte));
    return WidgetId{ hash == 0u ? 1u : hash };
}

Expected<WidgetId> MakeWidgetPartId(const WidgetId parent, const u64 stableKey)noexcept{
    if(!parent.valid() || stableKey == 0u)
        return MakeUnexpected(Failure{});
    u64 hash = UpdateFnv64U64LittleEndian(s_Fnv64OffsetBasis, parent.value);
    hash = UpdateFnv64U64LittleEndian(hash, 0u);
    hash = UpdateFnv64U64LittleEndian(hash, stableKey);
    return WidgetId{ hash == 0u ? 1u : hash };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

