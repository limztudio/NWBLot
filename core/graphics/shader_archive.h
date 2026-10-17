// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "rhi/foundation.h"

#include <core/alloc/scratch.h>

#include <global/binary.h>
#include <global/expected.h>
#include <global/span.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ShaderArchive{
public:
    static constexpr StringView s_IndexVirtualPath = "shader/index.bin";
    static constexpr StringView s_DefaultVariant = "default";
    inline static constexpr Name s_IndexVirtualPathName = Name(s_IndexVirtualPath);


public:
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


public:
    [[nodiscard]] static const Name& IndexVirtualPathName()noexcept;
    [[nodiscard]] static Name BuildVirtualPathName(const Name& shaderName, AStringView variantName, const Name& stageName);
    [[nodiscard]] static Expected<GraphicsBytes> SerializeIndex(GraphicsArena& arena, Span<const Record> records, Alloc::ScratchArena& scratchArena);
    [[nodiscard]] static Expected<GraphicsVector<Record>> DeserializeIndex(GraphicsArena& arena, BinaryByteView binary);
    [[nodiscard]] static Expected<Name> FindVirtualPath(const GraphicsVector<Record>& records, const Name& shaderName, AStringView variantName, const Name& stageName);
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

