// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "shape_registry.h"

#include <core/common/log.h>

#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_shape_registry{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 ValidShaderModuleInclude(const CsgShapeTypeDesc& desc){
    if(!desc.shaderModule)
        return true;
    if(desc.shaderModuleInclude.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("CsgShapeRegistry: rejected shape type '{}' with empty shader module include"), ConvertString(desc.name.resolvedText()));
        return false;
    }

    const AStringView include = AStringView(desc.shaderModuleInclude);
    if(!ValidCsgShaderModuleIncludeText(include)){
        NWB_LOGGER_ERROR(NWB_TEXT("CsgShapeRegistry: rejected shape type '{}' with invalid shader module include '{}'"), ConvertString(desc.name.resolvedText()), ConvertString(include));
        return false;
    }
    return true;
}

[[nodiscard]] b8 ValidShapeTypeDesc(const CsgShapeTypeDesc& desc){
    if(!desc.name){
        NWB_LOGGER_ERROR(NWB_TEXT("CsgShapeRegistry: rejected shape type with empty name"));
        return false;
    }
    if(!desc.boundsCallback){
        NWB_LOGGER_ERROR(NWB_TEXT("CsgShapeRegistry: rejected shape type '{}' with null bounds callback"), ConvertString(desc.name.resolvedText()));
        return false;
    }
    if(!ValidCsgShapeParameterByteCounts(desc.parameterByteSize, desc.defaultParameterBytes.size())){
        if(static_cast<usize>(desc.parameterByteSize) > s_CsgShapeInlineParameterMaxBytes)
            NWB_LOGGER_ERROR(NWB_TEXT("CsgShapeRegistry: rejected shape type '{}' with parameter size larger than the inline shader ABI"), ConvertString(desc.name.resolvedText()));
        else
            NWB_LOGGER_ERROR(NWB_TEXT("CsgShapeRegistry: rejected shape type '{}' with default parameter size mismatch"), ConvertString(desc.name.resolvedText()));
        return false;
    }
    return ValidShaderModuleInclude(desc);
}

[[nodiscard]] b8 ValidBoundsVectors(const SIMDVectorSingle minBounds, const SIMDVectorSingle maxBounds, const b8 finiteBounds)noexcept{
    if(!finiteBounds)
        return true;

    return AabbTests::Valid(minBounds, maxBounds);
}

[[nodiscard]] Expected<Span<const u8>> ResolveShapeParameterBytes(const CsgShapeTypeInfo& shapeType, Span<const u8> parameterBytes)noexcept{
    if(parameterBytes.empty() && !parameterBytes.data() && !shapeType.desc.defaultParameterBytes.empty())
        parameterBytes = Span<const u8>(shapeType.desc.defaultParameterBytes.data(), shapeType.desc.defaultParameterBytes.size());

    if(shapeType.desc.parameterByteSize != parameterBytes.size())
        return MakeUnexpected(Failure{});
    return parameterBytes;
}

[[nodiscard]] Expected<CsgShapeBounds> BuildShapeBoundsForShapeType(const CsgShapeTypeInfo& shapeType, const SIMDMatrix& shapeToWorld, const Span<const u8> parameterBytes){
    const auto resolvedBytes = ResolveShapeParameterBytes(shapeType, parameterBytes);
    if(!resolvedBytes)
        return MakeUnexpected(Failure{});

    auto bounds = shapeType.desc.boundsCallback(shapeToWorld, *resolvedBytes);
    if(!bounds || !ValidBoundsVectors(bounds->minBounds, bounds->maxBounds, bounds->finiteBounds))
        return MakeUnexpected(Failure{});
    return bounds;
}

[[nodiscard]] Expected<f32> EvaluateSignedDistanceForShapeType(const CsgShapeTypeInfo& shapeType, const SIMDVectorSingle shapePosition, const Span<const u8> parameterBytes){
    if(!shapeType.desc.signedDistanceCallback)
        return MakeUnexpected(Failure{});

    const auto resolvedBytes = ResolveShapeParameterBytes(shapeType, parameterBytes);
    if(!resolvedBytes)
        return MakeUnexpected(Failure{});

    return shapeType.desc.signedDistanceCallback(shapePosition, *resolvedBytes);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<CsgShapeTypeId> CsgShapeTypeIdFromName(const Name& shapeName)noexcept{
    CsgShapeTypeId typeId = s_InvalidCsgShapeTypeId;
    if(!TryResolveCsgShapeTypeIdFromName(shapeName, typeId))
        return MakeUnexpected(Failure{});
    return typeId;
}

Expected<CsgShapeBounds> BuildCsgShapeWorldBoundsFromLocal(const SIMDMatrix& shapeToWorld, const CsgShapeLocalBounds& localBounds)noexcept{
    if(!localBounds.finiteBounds)
        return CsgShapeBounds{};

    const SIMDVectorSingle localMin = VectorSetW(LoadFloat(localBounds.minBounds), s_CsgShapeBoundsW);
    const SIMDVectorSingle localMax = VectorSetW(LoadFloat(localBounds.maxBounds), s_CsgShapeBoundsW);
    const auto transformed = AabbTests::Transform(shapeToWorld, localMin, localMax);
    if(!transformed)
        return MakeUnexpected(Failure{});
    return CsgShapeBounds{ transformed->minBounds, transformed->maxBounds, true };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<CsgShapeTypeId> CsgShapeRegistry::registerShapeType(const CsgShapeTypeDesc& desc, const b8 replaceExisting){
    if(!__hidden_shape_registry::ValidShapeTypeDesc(desc))
        return MakeUnexpected(Failure{});

    const Expected<CsgShapeTypeId> canonicalIdResult = CsgShapeTypeIdFromName(desc.name);
    if(!canonicalIdResult){
        NWB_LOGGER_ERROR(NWB_TEXT("CsgShapeRegistry: rejected shape type '{}' with invalid canonical GPU id"), ConvertString(desc.name.resolvedText()));
        return MakeUnexpected(Failure{});
    }
    const CsgShapeTypeId canonicalId = *canonicalIdResult;

    ScopedLock lock(m_mutex);

    const auto found = m_shapeTypeIds.find(desc.name);
    if(found != m_shapeTypeIds.end()){
        if(!replaceExisting){
            NWB_LOGGER_ERROR(NWB_TEXT("CsgShapeRegistry: shape type '{}' is already registered"), ConvertString(desc.name.resolvedText()));
            return MakeUnexpected(Failure{});
        }

        const CsgShapeTypeId existingId = found.value();
        const auto foundIndex = m_shapeTypeIndices.find(existingId);
        NWB_ASSERT(ValidCsgShapeTypeId(existingId) && existingId == canonicalId && foundIndex != m_shapeTypeIndices.end());

        CsgShapeTypeInfo& shapeType = m_shapeTypes[foundIndex.value()];
        shapeType.desc = desc;
        ++m_revision;
        return existingId;
    }

    if(m_shapeTypes.size() >= static_cast<usize>(Limit<CsgShapeTypeId>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("CsgShapeRegistry: rejected shape type '{}' because the registry is full"), ConvertString(desc.name.resolvedText()));
        return MakeUnexpected(Failure{});
    }

    const auto foundCanonicalId = m_shapeTypeIndices.find(canonicalId);
    if(foundCanonicalId != m_shapeTypeIndices.end()){
        const CsgShapeTypeInfo& conflictingShapeType = m_shapeTypes[foundCanonicalId.value()];
        NWB_LOGGER_ERROR(NWB_TEXT("CsgShapeRegistry: shape types '{}' and '{}' collide on canonical GPU id {}"), ConvertString(conflictingShapeType.desc.name.resolvedText()), ConvertString(desc.name.resolvedText()), canonicalId);
        return MakeUnexpected(Failure{});
    }

    const CsgShapeTypeId id = canonicalId;
    CsgShapeTypeInfo shapeType(m_shapeTypes.get_allocator().arena());
    shapeType.id = id;
    shapeType.desc = desc;
    m_shapeTypes.push_back(Move(shapeType));
    ScopeExit discardShapeType([&]()noexcept{ m_shapeTypes.pop_back(); });
    const auto [nameEntry, nameInserted] = m_shapeTypeIds.emplace(desc.name, id);
    if(!nameInserted){
        NWB_ASSERT(false);
        return MakeUnexpected(Failure{});
    }
    ScopeExit discardNameEntry([&]()noexcept{ m_shapeTypeIds.erase(nameEntry); });
    if(!m_shapeTypeIndices.emplace(id, m_shapeTypes.size() - 1u).second){
        NWB_ASSERT(false);
        return MakeUnexpected(Failure{});
    }
    discardNameEntry.release();
    discardShapeType.release();
    ++m_revision;
    return id;
}


CsgShapeTypeId CsgShapeRegistry::findShapeTypeId(const Name& name)const noexcept{
    if(!name)
        return s_InvalidCsgShapeTypeId;

    ScopedLock lock(m_mutex);
    const auto found = m_shapeTypeIds.find(name);
    return found != m_shapeTypeIds.end() ? found.value() : s_InvalidCsgShapeTypeId;
}

Expected<CsgShapeTypeInfo> CsgShapeRegistry::findShapeType(const Name& name)const{
    if(!name)
        return MakeUnexpected(Failure{});

    ScopedLock lock(m_mutex);
    const auto found = m_shapeTypeIds.find(name);
    if(found == m_shapeTypeIds.end())
        return MakeUnexpected(Failure{});

    return shapeTypeById(found.value());
}

Expected<CsgShapeTypeInfo> CsgShapeRegistry::findShapeType(const CsgShapeTypeId typeId)const{
    ScopedLock lock(m_mutex);
    return shapeTypeById(typeId);
}

usize CsgShapeRegistry::shapeTypeCount()const noexcept{
    ScopedLock lock(m_mutex);
    return m_shapeTypes.size();
}

u64 CsgShapeRegistry::revision()const noexcept{
    ScopedLock lock(m_mutex);
    return m_revision;
}

Expected<AString<Core::Alloc::GlobalArena>> CsgShapeRegistry::findShaderModuleInclude(const Name& shaderModule, Core::Alloc::GlobalArena& resultArena)const{
    if(!shaderModule)
        return MakeUnexpected(Failure{});

    AString<Core::Alloc::GlobalArena> shaderModuleInclude(resultArena);
    b8 found = false;
    ScopedLock lock(m_mutex);
    for(const CsgShapeTypeInfo& shapeType : m_shapeTypes){
        if(shapeType.desc.shaderModule != shaderModule)
            continue;

        found = true;
        if(shapeType.desc.shaderModuleInclude.empty())
            continue;
        if(shaderModuleInclude.empty()){
            shaderModuleInclude = shapeType.desc.shaderModuleInclude;
            continue;
        }
        if(shaderModuleInclude != shapeType.desc.shaderModuleInclude)
            return MakeUnexpected(Failure{});
    }

    if(!found)
        return MakeUnexpected(Failure{});
    return shaderModuleInclude;
}


Expected<CsgShapeBounds> CsgShapeRegistry::buildShapeBounds(const CsgShapeTypeId typeId, const SIMDMatrix& shapeToWorld, const Span<const u8> parameterBytes)const{
    const auto shapeType = findShapeType(typeId);
    if(!shapeType)
        return MakeUnexpected(Failure{});
    return __hidden_shape_registry::BuildShapeBoundsForShapeType(*shapeType, shapeToWorld, parameterBytes);
}

Expected<f32> CsgShapeRegistry::evaluateSignedDistance(const CsgShapeTypeId typeId, const SIMDVectorSingle shapePosition, const Span<const u8> parameterBytes)const{
    const auto shapeType = findShapeType(typeId);
    if(!shapeType)
        return MakeUnexpected(Failure{});
    return __hidden_shape_registry::EvaluateSignedDistanceForShapeType(*shapeType, shapePosition, parameterBytes);
}


Expected<CsgShapeTypeInfo> CsgShapeRegistry::shapeTypeById(const CsgShapeTypeId typeId)const{
    if(!ValidCsgShapeTypeId(typeId))
        return MakeUnexpected(Failure{});

    const auto foundIndex = m_shapeTypeIndices.find(typeId);
    if(foundIndex == m_shapeTypeIndices.end())
        return MakeUnexpected(Failure{});

    return m_shapeTypes[foundIndex.value()];
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RegisterBuiltInCsgShapeTypes(CsgShapeRegistry& registry){
    b8 result = true;

    result = static_cast<b8>(registry.registerShape<CsgBoxShape>(true)) && result;
    result = static_cast<b8>(registry.registerShape<CsgCapsuleShape>(true)) && result;
    result = static_cast<b8>(registry.registerShape<CsgPlaneShape>(true)) && result;
    result = static_cast<b8>(registry.registerShape<CsgSphereShape>(true)) && result;

    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


