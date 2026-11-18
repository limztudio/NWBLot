// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "rhi/foundation.h"

#include <core/alloc/scratch.h>
#include <core/common/binary_update.h>

#include <global/binary.h>
#include <global/expected.h>
#include <global/span.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ShaderArchive{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using PathResolver = Function<Expected<Name>(const Name& shaderName, AStringView variantName, const Name& stageName)>;

inline constexpr StringView s_IndexVirtualPath = "shader/index.bin";
inline constexpr StringView s_DefaultVariant = "default";
inline constexpr Name s_IndexVirtualPathName = Name(s_IndexVirtualPath);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct Record{
    Name shaderName = s_NameNone;
    GraphicsString variantName;
    Name stage = s_NameNone;
    u64 sourceChecksum = 0;
    u64 bytecodeChecksum = 0;
    NameHash virtualPathHash = {};


public:
    explicit Record(GraphicsArena& arena)noexcept
        : variantName(arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Expected<Name> BuildVirtualPathName(const Name& shaderName, AStringView variantName, const Name& stageName);
[[nodiscard]] Expected<GraphicsBytes> SerializeIndex(Span<const Record> records, GraphicsArena& arena, Alloc::ScratchArena& scratchArena);
[[nodiscard]] Expected<GraphicsVector<Record>> DeserializeIndex(GraphicsArena& arena, BinaryByteView binary);
[[nodiscard]] Expected<Name> FindVirtualPath(Span<const Record> records, const Name& shaderName, AStringView variantName, const Name& stageName);


[[nodiscard]] inline Expected<GraphicsVector<Record>> ReadIndex(const BinaryUpdateContext& context, const BinaryByteView binary){
    return DeserializeIndex(context.output.get_allocator().arena(), binary);
}

[[nodiscard]] inline b8 WriteIndex(BinaryUpdateContext& context, const Span<const Record> records){
    auto bytes = SerializeIndex(records, context.output.get_allocator().arena(), context.scratch);
    if(!bytes)
        return false;
    context.output.swap(*bytes);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

