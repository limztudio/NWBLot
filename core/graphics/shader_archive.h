// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "common.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ShaderArchive{
public:
    struct Record{
        Name shaderName = NAME_NONE;
        Name variantName = NAME_NONE;
        Name stage = NAME_NONE;
        Name entryPoint = NAME_NONE;
        u64 sourceChecksum = 0;
        u64 bytecodeChecksum = 0;
        NameHash virtualPathHash = {};
    };


public:
    static constexpr const char* s_IndexVirtualPath = "shader/index.bin";
    static constexpr const char* s_DefaultVariant = "default";


public:
    [[nodiscard]] static AString buildVirtualPath(AStringView shaderName, AStringView variantName);
    static bool serializeIndex(const Vector<Record>& records, Vector<u8>& outBinary, AString& outError);
    static bool deserializeIndex(const Vector<u8>& binary, Vector<Record>& outRecords, AString& outError);
    static bool findVirtualPath(
        const Vector<Record>& records,
        AStringView shaderName,
        AStringView variantName,
        AString& outVirtualPath
    );
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

