// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline AString NormalizeCookConfiguration(const AStringView configuration){
    if(configuration.empty())
        return "default";

    const AString normalized = CanonicalizeText(configuration);
    if(normalized.empty())
        return "default";
    return normalized;
}


[[nodiscard]] inline bool ResolvePathFromCookRoot(const Path& root, const Path& inputPath, const AStringView pathLabel, Path& outPath, AString& outError){
    ErrorCode errorCode;

    if(ResolveAbsolutePath(root, PathToString(inputPath), outPath, errorCode))
        return true;

    outError = StringFormat("Failed to resolve {} '{}'", pathLabel, PathToString(inputPath));
    return false;
}


[[nodiscard]] inline bool WriteCookSourceChecksum(const Path& checksumPath, const AStringView checksumHex, AString& outError){
    if(!WriteTextFile(checksumPath, checksumHex)){
        outError = StringFormat("Failed to write cook source checksum '{}'", PathToString(checksumPath));
        return false;
    }
    return true;
}

[[nodiscard]] inline bool CookSourceChecksumMatches(const Path& checksumPath, const AStringView checksumHex){
    AString cachedText;
    if(!ReadTextFile(checksumPath, cachedText))
        return false;

    return Trim(cachedText) == checksumHex;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

