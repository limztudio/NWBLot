// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "shape_registry.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_shape_registry{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool ValidShapeTypeId(const CsgShapeTypeId id)noexcept{
    return id != s_InvalidCsgShapeTypeId;
}

[[nodiscard]] bool ValidShaderModuleInclude(const CsgShapeTypeDesc& desc){
    if(!desc.shaderModule)
        return true;
    if(desc.shaderModuleInclude.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("CsgShapeRegistry: rejected shape type '{}' with empty shader module include"), StringConvert(desc.name.resolvedText()));
        return false;
    }

    const AStringView include = desc.shaderModuleInclude.view();
    for(const char ch : include){
        if(ch != '"' && ch != ';' && ch != '=')
            continue;

        NWB_LOGGER_ERROR(NWB_TEXT("CsgShapeRegistry: rejected shape type '{}' with invalid shader module include '{}'")
            , StringConvert(desc.name.resolvedText())
            , StringConvert(include)
        );
        return false;
    }
    return true;
}

[[nodiscard]] bool ValidShapeTypeDesc(const CsgShapeTypeDesc& desc){
    if(!desc.name){
        NWB_LOGGER_ERROR(NWB_TEXT("CsgShapeRegistry: rejected shape type with empty name"));
        return false;
    }
    if(!desc.boundsCallback){
        NWB_LOGGER_ERROR(NWB_TEXT("CsgShapeRegistry: rejected shape type '{}' with null bounds callback"), StringConvert(desc.name.resolvedText()));
        return false;
    }
    if(static_cast<usize>(desc.parameterByteSize) > s_CsgShapeInlineParameterMaxBytes){
        NWB_LOGGER_ERROR(NWB_TEXT("CsgShapeRegistry: rejected shape type '{}' with parameter size larger than the inline shader ABI"), StringConvert(desc.name.resolvedText()));
        return false;
    }
    if(!desc.defaultParameterBytes.empty() && desc.defaultParameterBytes.size() != static_cast<usize>(desc.parameterByteSize)){
        NWB_LOGGER_ERROR(NWB_TEXT("CsgShapeRegistry: rejected shape type '{}' with default parameter size mismatch"), StringConvert(desc.name.resolvedText()));
        return false;
    }
    return ValidShaderModuleInclude(desc);
}

template<typename ParameterT>
[[nodiscard]] Expected<ParameterT> LoadShapeParameters(const u8* parameterBytes, const usize parameterByteSize)noexcept{
    if(parameterByteSize != sizeof(ParameterT) || !parameterBytes)
        return MakeUnexpected(Failure{});

    ParameterT parameters;
    NWB_MEMCPY(&parameters, sizeof(ParameterT), parameterBytes, sizeof(ParameterT));
    return parameters;
}

[[nodiscard]] bool ValidBoundsVectors(const SIMDVector minBounds, const SIMDVector maxBounds, const bool finiteBounds){
    if(!finiteBounds)
        return true;

    return AabbTests::Valid(minBounds, maxBounds);
}

[[nodiscard]] bool ValidPlaneParameters(const SIMDVector normalDistance)noexcept{
    return !Vector4IsNaN(normalDistance) && !Vector4IsInfinite(normalDistance);
}

[[nodiscard]] Expected<AabbTests::Bounds> BuildBoxBounds(
    const SIMDMatrix& shapeToWorld,
    const SIMDVector halfExtents
)noexcept{
    if(
        Vector3IsNaN(halfExtents)
        || Vector3IsInfinite(halfExtents)
        || !Vector3Greater(halfExtents, VectorZero())
    )
        return MakeUnexpected(Failure{});

    return AabbTests::Transform(
        shapeToWorld,
        VectorSetW(VectorNegate(halfExtents), s_CsgShapeBoundsW),
        halfExtents
    );
}

[[nodiscard]] Expected<AabbTests::Bounds> BuildSphereBounds(
    const SIMDMatrix& shapeToWorld,
    const SIMDVector radius
)noexcept{
    if(Vector3IsNaN(radius) || Vector3IsInfinite(radius) || !Vector3Greater(radius, VectorZero()))
        return MakeUnexpected(Failure{});

    const SIMDVector localMax = VectorSetW(radius, s_CsgShapeBoundsW);
    return AabbTests::Transform(
        shapeToWorld,
        VectorSetW(VectorNegate(localMax), s_CsgShapeBoundsW),
        localMax
    );
}

[[nodiscard]] Expected<AabbTests::Bounds> BuildCapsuleBounds(
    const SIMDMatrix& shapeToWorld,
    const SIMDVector radiusHalfHeight
)noexcept{
    const SIMDVector radius = VectorSplatX(radiusHalfHeight);
    const SIMDVector halfHeight = VectorSplatY(radiusHalfHeight);
    if(
        Vector3IsNaN(radius)
        || Vector3IsInfinite(radius)
        || Vector3IsNaN(halfHeight)
        || Vector3IsInfinite(halfHeight)
        || !Vector3Greater(radius, VectorZero())
        || !Vector3GreaterOrEqual(halfHeight, VectorZero())
    )
        return MakeUnexpected(Failure{});

    const SIMDVector yExtent = VectorAdd(halfHeight, radius);
    const SIMDVector capsuleSelect = VectorSelectControl(
        s_CsgCapsuleAxisSelectX,
        s_CsgCapsuleAxisSelectY,
        s_CsgCapsuleAxisSelectZ,
        s_CsgCapsuleAxisSelectW
    );
    const SIMDVector localMax = VectorSetW(VectorSelect(radius, yExtent, capsuleSelect), s_CsgShapeBoundsW);
    return AabbTests::Transform(
        shapeToWorld,
        VectorSetW(VectorNegate(localMax), s_CsgShapeBoundsW),
        localMax
    );
}

[[nodiscard]] Expected<CsgShapeBounds> BuildShapeBoundsForShapeType(
    const CsgShapeTypeInfo& shapeType,
    const SIMDMatrix& shapeToWorld,
    const u8* parameterBytes,
    usize parameterByteSize
){
    if(parameterByteSize == 0u && !parameterBytes && !shapeType.desc.defaultParameterBytes.empty()){
        parameterBytes = shapeType.desc.defaultParameterBytes.data();
        parameterByteSize = shapeType.desc.defaultParameterBytes.size();
    }

    if(shapeType.desc.parameterByteSize != parameterByteSize)
        return MakeUnexpected(Failure{});

    auto bounds = shapeType.desc.boundsCallback(shapeToWorld, parameterBytes, parameterByteSize);
    if(!bounds || !ValidBoundsVectors(bounds->minBounds, bounds->maxBounds, bounds->finiteBounds))
        return MakeUnexpected(Failure{});
    return bounds;
}

[[nodiscard]] Expected<CsgShapeBounds> PlaneBounds(
    const SIMDMatrix& shapeToWorld,
    const u8* parameterBytes,
    const usize parameterByteSize
)noexcept{
    static_cast<void>(shapeToWorld);
    const auto parameters = LoadShapeParameters<CsgPlaneShapeParameters>(parameterBytes, parameterByteSize);
    if(!parameters || !ValidPlaneParameters(LoadFloat(parameters->normalDistance)))
        return MakeUnexpected(Failure{});
    return CsgShapeBounds{};
}

[[nodiscard]] SIMDVector LoadBoxHalfExtents(const Float4& halfExtentsStorage)noexcept{
    return VectorSetW(LoadFloat(halfExtentsStorage), s_CsgShapeBoundsW);
}

[[nodiscard]] Expected<CsgShapeBounds> BoxBounds(
    const SIMDMatrix& shapeToWorld,
    const u8* parameterBytes,
    const usize parameterByteSize
)noexcept{
    const auto parameters = LoadShapeParameters<CsgBoxShapeParameters>(parameterBytes, parameterByteSize);
    if(!parameters)
        return MakeUnexpected(Failure{});
    const auto bounds = BuildBoxBounds(shapeToWorld, LoadBoxHalfExtents(parameters->halfExtents));
    if(!bounds)
        return MakeUnexpected(Failure{});
    return CsgShapeBounds{ bounds->minBounds, bounds->maxBounds, true };
}

[[nodiscard]] SIMDVector LoadSphereRadius(const Float4& radiusStorage)noexcept{
    return VectorSplatX(LoadFloat(radiusStorage));
}

[[nodiscard]] Expected<CsgShapeBounds> SphereBounds(
    const SIMDMatrix& shapeToWorld,
    const u8* parameterBytes,
    const usize parameterByteSize
)noexcept{
    const auto parameters = LoadShapeParameters<CsgSphereShapeParameters>(parameterBytes, parameterByteSize);
    if(!parameters)
        return MakeUnexpected(Failure{});
    const auto bounds = BuildSphereBounds(shapeToWorld, LoadSphereRadius(parameters->radius));
    if(!bounds)
        return MakeUnexpected(Failure{});
    return CsgShapeBounds{ bounds->minBounds, bounds->maxBounds, true };
}

[[nodiscard]] SIMDVector LoadCapsuleRadiusHalfHeight(const Float4& radiusHalfHeightStorage)noexcept{
    return LoadFloat(radiusHalfHeightStorage);
}

[[nodiscard]] Expected<CsgShapeBounds> CapsuleBounds(
    const SIMDMatrix& shapeToWorld,
    const u8* parameterBytes,
    const usize parameterByteSize
)noexcept{
    const auto parameters = LoadShapeParameters<CsgCapsuleShapeParameters>(parameterBytes, parameterByteSize);
    if(!parameters)
        return MakeUnexpected(Failure{});
    const auto bounds = BuildCapsuleBounds(shapeToWorld, LoadCapsuleRadiusHalfHeight(parameters->radiusHalfHeight));
    if(!bounds)
        return MakeUnexpected(Failure{});
    return CsgShapeBounds{ bounds->minBounds, bounds->maxBounds, true };
}

template<typename ParameterT>
[[nodiscard]] CsgShapeTypeDesc BuiltInShapeDesc(
    const Name& name,
    const ParameterT& defaultParameters,
    const CsgShapeBoundsCallback boundsCallback
){
    CsgShapeTypeDesc desc;
    desc.name = name;
    desc.parameterByteSize = sizeof(ParameterT);
    desc.defaultParameterBytes.resize(sizeof(ParameterT));
    NWB_MEMCPY(desc.defaultParameterBytes.data(), desc.defaultParameterBytes.size(), &defaultParameters, sizeof(ParameterT));
    desc.boundsCallback = boundsCallback;
    return desc;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CsgShapeRegistry::CsgShapeRegistry(Core::Alloc::GlobalArena& arena)
    : m_shapeTypes(arena)
    , m_shapeTypeIds(0, Hasher<Name>(), EqualTo<Name>(), arena)
    , m_shapeTypeIndices(0, Hasher<CsgShapeTypeId>(), EqualTo<CsgShapeTypeId>(), arena)
{}


Expected<CsgShapeTypeId> CsgShapeRegistry::registerShapeType(const CsgShapeTypeDesc& desc, const bool replaceExisting){
    if(!__hidden_shape_registry::ValidShapeTypeDesc(desc))
        return MakeUnexpected(Failure{});

    const CsgShapeTypeId canonicalId = CsgShapeTypeIdFromName(desc.name);
    if(!__hidden_shape_registry::ValidShapeTypeId(canonicalId)){
        NWB_LOGGER_ERROR(NWB_TEXT("CsgShapeRegistry: rejected shape type '{}' with invalid canonical GPU id"), StringConvert(desc.name.resolvedText()));
        return MakeUnexpected(Failure{});
    }

    ScopedLock lock(m_mutex);

    const auto found = m_shapeTypeIds.find(desc.name);
    if(found != m_shapeTypeIds.end()){
        if(!replaceExisting){
            NWB_LOGGER_ERROR(NWB_TEXT("CsgShapeRegistry: shape type '{}' is already registered"), StringConvert(desc.name.resolvedText()));
            return MakeUnexpected(Failure{});
        }

        NWB_ASSERT(__hidden_shape_registry::ValidShapeTypeId(found.value()) && found.value() == canonicalId && m_shapeTypeIndices.find(found.value()) != m_shapeTypeIndices.end());
    }

    if(found != m_shapeTypeIds.end()){
        const CsgShapeTypeId existingId = found.value();
        const auto foundIndex = m_shapeTypeIndices.find(existingId);
        if(foundIndex == m_shapeTypeIndices.end()){
            NWB_ASSERT(false);
            return MakeUnexpected(Failure{});
        }

        CsgShapeTypeInfo& shapeType = m_shapeTypes[foundIndex.value()];
        shapeType.desc = desc;
        ++m_revision;
        return existingId;
    }

    if(m_shapeTypes.size() >= static_cast<usize>(Limit<CsgShapeTypeId>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("CsgShapeRegistry: rejected shape type '{}' because the registry is full"), StringConvert(desc.name.resolvedText()));
        return MakeUnexpected(Failure{});
    }

    const auto foundCanonicalId = m_shapeTypeIndices.find(canonicalId);
    if(foundCanonicalId != m_shapeTypeIndices.end()){
        const CsgShapeTypeInfo& conflictingShapeType = m_shapeTypes[foundCanonicalId.value()];
        NWB_LOGGER_ERROR(NWB_TEXT("CsgShapeRegistry: shape types '{}' and '{}' collide on canonical GPU id {}")
            , StringConvert(conflictingShapeType.desc.name.resolvedText())
            , StringConvert(desc.name.resolvedText())
            , canonicalId
        );
        return MakeUnexpected(Failure{});
    }

    const CsgShapeTypeId id = canonicalId;
    m_shapeTypes.push_back(CsgShapeTypeInfo{ id, desc });
    m_shapeTypeIds.emplace(desc.name, id);
    m_shapeTypeIndices.emplace(id, m_shapeTypes.size() - 1u);
    ++m_revision;
    return id;
}


CsgShapeTypeId CsgShapeRegistry::findShapeTypeId(const Name& name)const{
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

usize CsgShapeRegistry::shapeTypeCount()const{
    ScopedLock lock(m_mutex);
    return m_shapeTypes.size();
}

u64 CsgShapeRegistry::revision()const{
    ScopedLock lock(m_mutex);
    return m_revision;
}

Expected<ACompactString> CsgShapeRegistry::findShaderModuleInclude(const Name& shaderModule)const{
    if(!shaderModule)
        return MakeUnexpected(Failure{});

    ACompactString shaderModuleInclude;
    bool found = false;
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


Expected<CsgShapeBounds> CsgShapeRegistry::buildShapeBounds(
    const Name& name,
    const SIMDMatrix& shapeToWorld,
    const u8* parameterBytes,
    const usize parameterByteSize
)const{
    return buildShapeBounds(findShapeTypeId(name), shapeToWorld, parameterBytes, parameterByteSize);
}

Expected<CsgShapeBounds> CsgShapeRegistry::buildShapeBounds(
    const CsgShapeTypeId typeId,
    const SIMDMatrix& shapeToWorld,
    const u8* parameterBytes,
    const usize parameterByteSize
)const{
    const auto shapeType = findShapeType(typeId);
    if(!shapeType)
        return MakeUnexpected(Failure{});
    return __hidden_shape_registry::BuildShapeBoundsForShapeType(*shapeType, shapeToWorld, parameterBytes, parameterByteSize);
}


Expected<CsgShapeTypeInfo> CsgShapeRegistry::shapeTypeById(const CsgShapeTypeId typeId)const{
    if(!__hidden_shape_registry::ValidShapeTypeId(typeId))
        return MakeUnexpected(Failure{});

    const auto foundIndex = m_shapeTypeIndices.find(typeId);
    if(foundIndex == m_shapeTypeIndices.end())
        return MakeUnexpected(Failure{});

    return m_shapeTypes[foundIndex.value()];
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RegisterBuiltInCsgShapeTypes(CsgShapeRegistry& registry){
    bool result = true;

    result = registry.registerShapeType(
        __hidden_shape_registry::BuiltInShapeDesc(
            s_CsgBoxShapeName,
            CsgBoxShapeParameters{},
            &__hidden_shape_registry::BoxBounds
        ),
        true
    ) && result;
    result = registry.registerShapeType(
        __hidden_shape_registry::BuiltInShapeDesc(
            s_CsgCapsuleShapeName,
            CsgCapsuleShapeParameters{},
            &__hidden_shape_registry::CapsuleBounds
        ),
        true
    ) && result;
    result = registry.registerShapeType(
        __hidden_shape_registry::BuiltInShapeDesc(
            s_CsgPlaneShapeName,
            CsgPlaneShapeParameters{},
            &__hidden_shape_registry::PlaneBounds
        ),
        true
    ) && result;
    result = registry.registerShapeType(
        __hidden_shape_registry::BuiltInShapeDesc(
            s_CsgSphereShapeName,
            CsgSphereShapeParameters{},
            &__hidden_shape_registry::SphereBounds
        ),
        true
    ) && result;

    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

