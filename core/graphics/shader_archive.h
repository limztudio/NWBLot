// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "common.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ShaderArchive{
public:
    static constexpr const char* s_IndexVirtualPath = "shader/index.bin";
    static constexpr const char* s_DefaultVariant = "default";


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
    [[nodiscard]] static const Name& IndexVirtualPathName();
    [[nodiscard]] static Name buildVirtualPathName(const Name& shaderName, const CompactString& variantName, const Name& stageName);
    static bool serializeIndex(const Vector<Record>& records, Vector<u8>& outBinary);
    static bool deserializeIndex(const Vector<u8>& binary, Vector<Record>& outRecords);
    static bool findVirtualPath(const Vector<Record>& records, const Name& shaderName, const CompactString& variantName, const Name& stageName, Name& outVirtualPath);
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

