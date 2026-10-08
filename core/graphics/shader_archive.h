// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "rhi/foundation.h"

#include <global/expected.h>


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
        explicit Record(GraphicsArena& arena)
            : variantName(arena)
        {}

        Name shaderName = s_NameNone;
        GraphicsString variantName;
        Name stage = s_NameNone;
        u64 sourceChecksum = 0;
        u64 bytecodeChecksum = 0;
        NameHash virtualPathHash = {};
    };


public:
    [[nodiscard]] static const Name& IndexVirtualPathName()noexcept;
    [[nodiscard]] static Name BuildVirtualPathName(const Name& shaderName, AStringView variantName, const Name& stageName);
    static Expected<GraphicsBytes> SerializeIndex(GraphicsArena& arena, const GraphicsVector<Record>& records);
    [[nodiscard]] static Expected<GraphicsVector<Record>> DeserializeIndex(GraphicsArena& arena, const GraphicsBytes& binary);
    [[nodiscard]] static Expected<Name> FindVirtualPath(const GraphicsVector<Record>& records, const Name& shaderName, AStringView variantName, const Name& stageName);
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

