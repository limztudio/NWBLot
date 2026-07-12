
#pragma once


#include "api.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ShaderArchive{
public:
    static constexpr const char* s_IndexVirtualPath = "shader/index.bin";
    static constexpr const char* s_DefaultVariant = "default";
    inline static constexpr Name s_IndexVirtualPathName = Name(s_IndexVirtualPath);


public:
    struct Record{
        explicit Record(GraphicsArena& arena)
            : variantName(arena)
        {}

        Name shaderName = NAME_NONE;
        GraphicsString variantName;
        Name stage = NAME_NONE;
        u64 sourceChecksum = 0;
        u64 bytecodeChecksum = 0;
        NameHash virtualPathHash = {};
    };


public:
    [[nodiscard]] static const Name& IndexVirtualPathName();
    [[nodiscard]] static Name buildVirtualPathName(const Name& shaderName, AStringView variantName, const Name& stageName);
    static bool serializeIndex(const GraphicsVector<Record>& records, GraphicsBytes& outBinary);
    static bool deserializeIndex(const GraphicsBytes& binary, GraphicsVector<Record>& outRecords);
    static bool findVirtualPath(const GraphicsVector<Record>& records, const Name& shaderName, AStringView variantName, const Name& stageName, Name& outVirtualPath);
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

