// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "rhi/foundation.h"


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
    [[nodiscard]] static const Name& IndexVirtualPathName();
    [[nodiscard]] static Name BuildVirtualPathName(const Name& shaderName, AStringView variantName, const Name& stageName);
    static bool SerializeIndex(const GraphicsVector<Record>& records, GraphicsBytes& outBinary);
    static bool DeserializeIndex(const GraphicsBytes& binary, GraphicsVector<Record>& outRecords);
    static bool FindVirtualPath(const GraphicsVector<Record>& records, const Name& shaderName, AStringView variantName, const Name& stageName, Name& outVirtualPath);
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

