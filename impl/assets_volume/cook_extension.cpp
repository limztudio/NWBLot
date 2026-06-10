// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook_extension.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_asset_volume_cook_extension{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct AutoPrepareQueue{
    ShaderCook::CookArena arena;
    Futex mutex;
    ShaderCook::CookVector<AssetsVolumeCookDetail::AssetVolumePrepareFunction> functions;

    AutoPrepareQueue()
        : arena("NWB::AssetsVolumeCookDetail::AssetVolumePrepareQueue")
        , functions(arena)
    {}
};

struct AutoMetadataParser{
    AssetsVolumeCookDetail::AssetVolumeDocumentMetadataParseFunction documentFunction = nullptr;
    AssetsVolumeCookDetail::AssetVolumeValueMetadataParseFunction valueFunction = nullptr;
};

struct AutoMetadataParserQueue{
    ShaderCook::CookArena arena;
    Futex mutex;
    ShaderCook::CookVector<AutoMetadataParser> parsers;

    AutoMetadataParserQueue()
        : arena("NWB::AssetsVolumeCookDetail::AssetVolumeMetadataParserQueue")
        , parsers(arena)
    {}
};

AutoPrepareQueue& QueryAutoPrepareQueue(){
    static AutoPrepareQueue queue;
    return queue;
}

AutoMetadataParserQueue& QueryAutoMetadataParserQueue(){
    static AutoMetadataParserQueue queue;
    return queue;
}

static bool ContainsPrepareFunction(
    const ShaderCook::CookVector<AssetsVolumeCookDetail::AssetVolumePrepareFunction>& functions,
    const AssetsVolumeCookDetail::AssetVolumePrepareFunction function
){
    for(const AssetsVolumeCookDetail::AssetVolumePrepareFunction current : functions){
        if(current == function)
            return true;
    }

    return false;
}

static bool ContainsMetadataParser(
    const ShaderCook::CookVector<AutoMetadataParser>& parsers,
    const AssetsVolumeCookDetail::AssetVolumeDocumentMetadataParseFunction documentFunction,
    const AssetsVolumeCookDetail::AssetVolumeValueMetadataParseFunction valueFunction
){
    for(const AutoMetadataParser& current : parsers){
        if(current.documentFunction == documentFunction && current.valueFunction == valueFunction)
            return true;
    }

    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AssetsVolumeCookDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool AddPlannedFileCount(const u64 additionalFileCount, u64& inOutPlannedFileCount){
    if(inOutPlannedFileCount > Limit<u64>::s_Max - additionalFileCount){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: planned file count overflow"));
        return false;
    }

    inOutPlannedFileCount += additionalFileCount;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


AssetVolumePrepareAutoRegistrar::AssetVolumePrepareAutoRegistrar(const AssetVolumePrepareFunction function){
    if(function == nullptr)
        return;

    auto& queue = __hidden_asset_volume_cook_extension::QueryAutoPrepareQueue();
    ScopedLock lock(queue.mutex);
    if(!__hidden_asset_volume_cook_extension::ContainsPrepareFunction(queue.functions, function))
        queue.functions.push_back(function);
}

AssetVolumeMetadataParserAutoRegistrar::AssetVolumeMetadataParserAutoRegistrar(
    const AssetVolumeDocumentMetadataParseFunction documentFunction,
    const AssetVolumeValueMetadataParseFunction valueFunction
){
    if(documentFunction == nullptr && valueFunction == nullptr)
        return;

    auto& queue = __hidden_asset_volume_cook_extension::QueryAutoMetadataParserQueue();
    ScopedLock lock(queue.mutex);
    if(!__hidden_asset_volume_cook_extension::ContainsMetadataParser(queue.parsers, documentFunction, valueFunction)){
        queue.parsers.push_back(__hidden_asset_volume_cook_extension::AutoMetadataParser{
            documentFunction,
            valueFunction
        });
    }
}

bool RegisterAutoCollectedAssetVolumePreparers(AssetVolumePrepareContext& context){
    Core::Alloc::ScratchArena scratchArena;
    Vector<AssetVolumePrepareFunction, Core::Alloc::ScratchArena> functions{scratchArena};
    {
        auto& queue = __hidden_asset_volume_cook_extension::QueryAutoPrepareQueue();
        ScopedLock lock(queue.mutex);
        AssignTriviallyCopyableVector(functions, queue.functions);
    }

    for(const AssetVolumePrepareFunction function : functions){
        if(function == nullptr)
            continue;
        if(function(context))
            continue;

        NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: failed to run auto-collected volume prepare step"));
        return false;
    }

    return true;
}

AssetVolumeMetadataParseResult TryAutoCollectedDocumentMetadataParsers(AssetVolumeDocumentMetadataParseContext& context){
    Core::Alloc::ScratchArena scratchArena;
    Vector<__hidden_asset_volume_cook_extension::AutoMetadataParser, Core::Alloc::ScratchArena> parsers{scratchArena};
    {
        auto& queue = __hidden_asset_volume_cook_extension::QueryAutoMetadataParserQueue();
        ScopedLock lock(queue.mutex);
        AssignTriviallyCopyableVector(parsers, queue.parsers);
    }

    for(const __hidden_asset_volume_cook_extension::AutoMetadataParser& parser : parsers){
        if(parser.documentFunction == nullptr)
            continue;

        const AssetVolumeMetadataParseResult result = parser.documentFunction(context);
        if(result == AssetVolumeMetadataParseResult::Unsupported)
            continue;
        return result;
    }

    return AssetVolumeMetadataParseResult::Unsupported;
}

AssetVolumeMetadataParseResult TryAutoCollectedValueMetadataParsers(AssetVolumeValueMetadataParseContext& context){
    Core::Alloc::ScratchArena scratchArena;
    Vector<__hidden_asset_volume_cook_extension::AutoMetadataParser, Core::Alloc::ScratchArena> parsers{scratchArena};
    {
        auto& queue = __hidden_asset_volume_cook_extension::QueryAutoMetadataParserQueue();
        ScopedLock lock(queue.mutex);
        AssignTriviallyCopyableVector(parsers, queue.parsers);
    }

    for(const __hidden_asset_volume_cook_extension::AutoMetadataParser& parser : parsers){
        if(parser.valueFunction == nullptr)
            continue;

        const AssetVolumeMetadataParseResult result = parser.valueFunction(context);
        if(result == AssetVolumeMetadataParseResult::Unsupported)
            continue;
        return result;
    }

    return AssetVolumeMetadataParseResult::Unsupported;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
