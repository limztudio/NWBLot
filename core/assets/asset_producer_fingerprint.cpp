// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


#include "asset_producer_fingerprint.h"

#include <core/common/log.h>

#include <global/filesystem/executable_paths.h>
#include <global/filesystem/operations.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


void AssetProducerFingerprint::Resolve(ScratchArena& scratchArena, b8& useAssetCache, Sha256Digest& producerDigest){
    ::Path<ScratchArena> executablePath(scratchArena);
#if defined(NWB_PLATFORM_LINUX)
    executablePath = ::Path<ScratchArena>(scratchArena, "/proc/self/exe");
#else
    auto resolvedExecutablePath = ExecutablePaths(scratchArena).path();
    if(!resolvedExecutablePath){
        NWB_LOGGER_WARNING(NWB_TEXT("AssetCook: disabling asset cache because the producer executable path is unavailable: {}"), ConvertString(resolvedExecutablePath.error().message()));
        useAssetCache = false;
    }
    else
        executablePath = Move(*resolvedExecutablePath);
#endif
    if(useAssetCache){
        Vector<u8, ScratchArena> executableBytes(scratchArena);
        const auto read = ReadBinaryFile(executablePath, executableBytes);
        if(!read){
            NWB_LOGGER_WARNING(NWB_TEXT("AssetCook: disabling asset cache because producer executable '{}' could not be read: {}"), PathToString<tchar>(executablePath), ConvertString(read.error().message()));
            useAssetCache = false;
        }
        else if(executableBytes.empty()){
            NWB_LOGGER_WARNING(NWB_TEXT("AssetCook: disabling asset cache because producer executable '{}' is empty"), PathToString<tchar>(executablePath));
            useAssetCache = false;
        }
        else
            producerDigest = ComputeSha256(BinaryByteView{ executableBytes.data(), executableBytes.size() });
    }
}


NWB_ASSETS_END


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

