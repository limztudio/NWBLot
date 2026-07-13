// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "shape_registry.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_shape_registry{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool ValidShapeTypeId(const CsgShapeTypeId id){
    return id != s_InvalidCsgShapeTypeId;
}

[[nodiscard]] bool ValidShaderModuleInclude(const CsgShapeTypeDesc& desc){
    if(!desc.shaderModule)
        return true;
    if(desc.shaderModuleInclude.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("CsgShapeRegistry: rejected shape type '{}' with empty shader module include"), StringConvert(desc.name.c_str()));
        return false;
    }

    const AStringView include = desc.shaderModuleInclude.view();
    for(const char ch : include){
        if(ch != '"' && ch != ';' && ch != '=')
            continue;

        NWB_LOGGER_ERROR(NWB_TEXT("CsgShapeRegistry: rejected shape type '{}' with invalid shader module include '{}'")
            , StringConvert(desc.name.c_str())
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
        NWB_LOGGER_ERROR(NWB_TEXT("CsgShapeRegistry: rejected shape type '{}' with null bounds callback"), StringConvert(desc.name.c_str()));
        return false;
    }
    if(static_cast<usize>(desc.parameterByteSize) > s_CsgShapeInlineParameterMaxBytes){
        NWB_LOGGER_ERROR(NWB_TEXT("CsgShapeRegistry: rejected shape type '{}' with parameter size larger than the inline shader ABI"), StringConvert(desc.name.c_str()));
        return false;
    }
    if(!desc.defaultParameterBytes.empty() && desc.defaultParameterBytes.size() != static_cast<usize>(desc.parameterByteSize)){
        NWB_LOGGER_ERROR(NWB_TEXT("CsgShapeRegistry: rejected shape type '{}' with default parameter size mismatch"), StringConvert(desc.name.c_str()));
        return false;
    }
    return ValidShaderModuleInclude(desc);
}

template<typename ParameterT>
[[nodiscard]] bool LoadShapeParameters(const u8* parameterBytes, const usize parameterByteSize, ParameterT& outParameters){
    if(parameterByteSize != sizeof(ParameterT))
        return false;
    if(!parameterBytes)
        return false;

    NWB_MEMCPY(&outParameters, sizeof(ParameterT), parameterBytes, sizeof(ParameterT));
    return true;
}

[[nodiscard]] bool ValidBoundsVectors(const SIMDVector minBounds, const SIMDVector maxBounds, const bool finiteBounds){
    if(!finiteBounds)
        return true;

    return AabbTests::Valid(minBounds, maxBounds);
}

[[nodiscard]] bool ValidPlaneParameters(const SIMDVector normalDistance){
    return !Vector4IsNaN(normalDistance) && !Vector4IsInfinite(normalDistance);
}

[[nodiscard]] bool LoadPlaneNormalDistance(
    const u8* parameterBytes,
    const usize parameterByteSize,
    SIMDVector& outNormalDistance
){
    CsgPlaneShapeParameters parameters;
    if(!LoadShapeParameters(parameterBytes, parameterByteSize, parameters))
        return false;

    outNormalDistance = LoadFloat(parameters.normalDistance);
    return true;
}

[[nodiscard]] bool LoadBoxHalfExtents(
    const u8* parameterBytes,
    const usize parameterByteSize,
    SIMDVector& outHalfExtents
){
    CsgBoxShapeParameters parameters;
    if(!LoadShapeParameters(parameterBytes, parameterByteSize, parameters))
        return false;

    outHalfExtents = VectorSetW(LoadFloat(parameters.halfExtents), 0.0f);
    return true;
}

[[nodiscard]] bool LoadSphereRadius(
    const u8* parameterBytes,
    const usize parameterByteSize,
    SIMDVector& outRadius
){
    CsgSphereShapeParameters parameters;
    if(!LoadShapeParameters(parameterBytes, parameterByteSize, parameters))
        return false;

    outRadius = VectorSplatX(LoadFloat(parameters.radius));
    return true;
}

[[nodiscard]] bool LoadCapsuleRadiusHalfHeight(
    const u8* parameterBytes,
    const usize parameterByteSize,
    SIMDVector& outRadiusHalfHeight
){
    CsgCapsuleShapeParameters parameters;
    if(!LoadShapeParameters(parameterBytes, parameterByteSize, parameters))
        return false;

    outRadiusHalfHeight = LoadFloat(parameters.radiusHalfHeight);
    return true;
}

[[nodiscard]] bool BuildBoxBounds(
    const SIMDMatrix& shapeToWorld,
    const SIMDVector halfExtents,
    SIMDVector& outMinBounds,
    SIMDVector& outMaxBounds
){
    if(
        Vector3IsNaN(halfExtents)
        || Vector3IsInfinite(halfExtents)
        || !Vector3Greater(halfExtents, VectorZero())
    )
        return false;

    return AabbTests::Transform(
        shapeToWorld,
        VectorSetW(VectorNegate(halfExtents), 0.0f),
        halfExtents,
        outMinBounds,
        outMaxBounds
    );
}

[[nodiscard]] bool BuildSphereBounds(
    const SIMDMatrix& shapeToWorld,
    const SIMDVector radius,
    SIMDVector& outMinBounds,
    SIMDVector& outMaxBounds
){
    if(Vector3IsNaN(radius) || Vector3IsInfinite(radius) || !Vector3Greater(radius, VectorZero()))
        return false;

    const SIMDVector localMax = VectorSetW(radius, 0.0f);
    return AabbTests::Transform(
        shapeToWorld,
        VectorSetW(VectorNegate(localMax), 0.0f),
        localMax,
        outMinBounds,
        outMaxBounds
    );
}

[[nodiscard]] bool BuildCapsuleBounds(
    const SIMDMatrix& shapeToWorld,
    const SIMDVector radiusHalfHeight,
    SIMDVector& outMinBounds,
    SIMDVector& outMaxBounds
){
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
        return false;

    const SIMDVector yExtent = VectorAdd(halfHeight, radius);
    const SIMDVector localMax = VectorSetW(VectorSelect(radius, yExtent, VectorSelectControl(0u, 1u, 0u, 0u)), 0.0f);
    return AabbTests::Transform(
        shapeToWorld,
        VectorSetW(VectorNegate(localMax), 0.0f),
        localMax,
        outMinBounds,
        outMaxBounds
    );
}

[[nodiscard]] bool BuildShapeBoundsForShapeType(
    const CsgShapeTypeInfo& shapeType,
    const SIMDMatrix& shapeToWorld,
    const u8* parameterBytes,
    usize parameterByteSize,
    SIMDVector& outMinBounds,
    SIMDVector& outMaxBounds,
    bool& outFiniteBounds
){
    if(parameterByteSize == 0u && !parameterBytes && !shapeType.desc.defaultParameterBytes.empty()){
        parameterBytes = shapeType.desc.defaultParameterBytes.data();
        parameterByteSize = shapeType.desc.defaultParameterBytes.size();
    }

    if(shapeType.desc.parameterByteSize != parameterByteSize)
        return false;

    if(!shapeType.desc.boundsCallback(
        shapeToWorld,
        parameterBytes,
        parameterByteSize,
        outMinBounds,
        outMaxBounds,
        outFiniteBounds
    ))
        return false;

    return ValidBoundsVectors(outMinBounds, outMaxBounds, outFiniteBounds);
}

[[nodiscard]] bool PlaneBounds(
    const SIMDMatrix& shapeToWorld,
    const u8* parameterBytes,
    const usize parameterByteSize,
    SIMDVector& outMinBounds,
    SIMDVector& outMaxBounds,
    bool& outFiniteBounds
){
    static_cast<void>(shapeToWorld);

    outMinBounds = VectorZero();
    outMaxBounds = VectorZero();
    outFiniteBounds = false;

    SIMDVector normalDistance;
    if(!LoadPlaneNormalDistance(parameterBytes, parameterByteSize, normalDistance))
        return false;

    return ValidPlaneParameters(normalDistance);
}

[[nodiscard]] bool BoxBounds(
    const SIMDMatrix& shapeToWorld,
    const u8* parameterBytes,
    const usize parameterByteSize,
    SIMDVector& outMinBounds,
    SIMDVector& outMaxBounds,
    bool& outFiniteBounds
){
    outMinBounds = VectorZero();
    outMaxBounds = VectorZero();
    outFiniteBounds = false;

    SIMDVector halfExtents;
    if(!LoadBoxHalfExtents(parameterBytes, parameterByteSize, halfExtents))
        return false;
    if(!BuildBoxBounds(shapeToWorld, halfExtents, outMinBounds, outMaxBounds))
        return false;

    outFiniteBounds = true;
    return true;
}

[[nodiscard]] bool SphereBounds(
    const SIMDMatrix& shapeToWorld,
    const u8* parameterBytes,
    const usize parameterByteSize,
    SIMDVector& outMinBounds,
    SIMDVector& outMaxBounds,
    bool& outFiniteBounds
){
    outMinBounds = VectorZero();
    outMaxBounds = VectorZero();
    outFiniteBounds = false;

    SIMDVector radius;
    if(!LoadSphereRadius(parameterBytes, parameterByteSize, radius))
        return false;
    if(!BuildSphereBounds(shapeToWorld, radius, outMinBounds, outMaxBounds))
        return false;

    outFiniteBounds = true;
    return true;
}

[[nodiscard]] bool CapsuleBounds(
    const SIMDMatrix& shapeToWorld,
    const u8* parameterBytes,
    const usize parameterByteSize,
    SIMDVector& outMinBounds,
    SIMDVector& outMaxBounds,
    bool& outFiniteBounds
){
    outMinBounds = VectorZero();
    outMaxBounds = VectorZero();
    outFiniteBounds = false;

    SIMDVector radiusHalfHeight;
    if(!LoadCapsuleRadiusHalfHeight(parameterBytes, parameterByteSize, radiusHalfHeight))
        return false;
    if(!BuildCapsuleBounds(shapeToWorld, radiusHalfHeight, outMinBounds, outMaxBounds))
        return false;

    outFiniteBounds = true;
    return true;
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
{}


bool CsgShapeRegistry::registerShapeType(const CsgShapeTypeDesc& desc, CsgShapeTypeId& outTypeId, const bool replaceExisting){
    outTypeId = s_InvalidCsgShapeTypeId;
    if(!__hidden_shape_registry::ValidShapeTypeDesc(desc))
        return false;

    ScopedLock lock(m_mutex);

    const auto found = m_shapeTypeIds.find(desc.name);
    if(found != m_shapeTypeIds.end()){
        if(!replaceExisting){
            NWB_LOGGER_ERROR(NWB_TEXT("CsgShapeRegistry: shape type '{}' is already registered"), StringConvert(desc.name.c_str()));
            return false;
        }

        const CsgShapeTypeId existingId = found.value();
        const usize existingIndex = static_cast<usize>(existingId - 1u);
        if(!__hidden_shape_registry::ValidShapeTypeId(existingId) || existingIndex >= m_shapeTypes.size()){
            NWB_LOGGER_ERROR(NWB_TEXT("CsgShapeRegistry: shape type '{}' has an outdated registry index"), StringConvert(desc.name.c_str()));
            return false;
        }
    }

    if(found != m_shapeTypeIds.end()){
        const CsgShapeTypeId existingId = found.value();
        CsgShapeTypeInfo& shapeType = m_shapeTypes[static_cast<usize>(existingId - 1u)];
        shapeType.desc = desc;
        outTypeId = existingId;
        return true;
    }

    if(m_shapeTypes.size() >= static_cast<usize>(Limit<CsgShapeTypeId>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("CsgShapeRegistry: rejected shape type '{}' because the registry is full"), StringConvert(desc.name.c_str()));
        return false;
    }

    const CsgShapeTypeId id = static_cast<CsgShapeTypeId>(m_shapeTypes.size()) + 1u;
    m_shapeTypes.push_back(CsgShapeTypeInfo{ id, desc });
    m_shapeTypeIds.emplace(desc.name, id);
    outTypeId = id;
    return true;
}


CsgShapeTypeId CsgShapeRegistry::findShapeTypeId(const Name& name)const{
    if(!name)
        return s_InvalidCsgShapeTypeId;

    ScopedLock lock(m_mutex);
    const auto found = m_shapeTypeIds.find(name);
    return found != m_shapeTypeIds.end() ? found.value() : s_InvalidCsgShapeTypeId;
}

bool CsgShapeRegistry::findShapeType(const Name& name, CsgShapeTypeInfo& outShapeType)const{
    outShapeType = CsgShapeTypeInfo{};
    if(!name)
        return false;

    ScopedLock lock(m_mutex);
    const auto found = m_shapeTypeIds.find(name);
    if(found == m_shapeTypeIds.end())
        return false;

    return shapeTypeById(found.value(), outShapeType);
}

bool CsgShapeRegistry::findShapeType(const CsgShapeTypeId typeId, CsgShapeTypeInfo& outShapeType)const{
    outShapeType = CsgShapeTypeInfo{};

    ScopedLock lock(m_mutex);
    return shapeTypeById(typeId, outShapeType);
}

usize CsgShapeRegistry::shapeTypeCount()const{
    ScopedLock lock(m_mutex);
    return m_shapeTypes.size();
}

bool CsgShapeRegistry::findShaderModuleInclude(const Name& shaderModule, ACompactString& outShaderModuleInclude)const{
    outShaderModuleInclude.clear();
    if(!shaderModule)
        return false;

    bool found = false;
    ScopedLock lock(m_mutex);
    for(const CsgShapeTypeInfo& shapeType : m_shapeTypes){
        if(shapeType.desc.shaderModule != shaderModule)
            continue;

        found = true;
        if(shapeType.desc.shaderModuleInclude.empty())
            continue;
        if(outShaderModuleInclude.empty()){
            outShaderModuleInclude = shapeType.desc.shaderModuleInclude;
            continue;
        }
        if(outShaderModuleInclude != shapeType.desc.shaderModuleInclude)
            return false;
    }

    return found;
}


bool CsgShapeRegistry::buildShapeBounds(
    const Name& name,
    const SIMDMatrix& shapeToWorld,
    const u8* parameterBytes,
    const usize parameterByteSize,
    SIMDVector& outMinBounds,
    SIMDVector& outMaxBounds,
    bool& outFiniteBounds
)const{
    return buildShapeBounds(
        findShapeTypeId(name),
        shapeToWorld,
        parameterBytes,
        parameterByteSize,
        outMinBounds,
        outMaxBounds,
        outFiniteBounds
    );
}

bool CsgShapeRegistry::buildShapeBounds(
    const CsgShapeTypeId typeId,
    const SIMDMatrix& shapeToWorld,
    const u8* parameterBytes,
    const usize parameterByteSize,
    SIMDVector& outMinBounds,
    SIMDVector& outMaxBounds,
    bool& outFiniteBounds
)const{
    outMinBounds = VectorZero();
    outMaxBounds = VectorZero();
    outFiniteBounds = false;

    CsgShapeTypeInfo shapeType;
    if(!findShapeType(typeId, shapeType))
        return false;
    return __hidden_shape_registry::BuildShapeBoundsForShapeType(
        shapeType,
        shapeToWorld,
        parameterBytes,
        parameterByteSize,
        outMinBounds,
        outMaxBounds,
        outFiniteBounds
    );
}


bool CsgShapeRegistry::shapeTypeById(const CsgShapeTypeId typeId, CsgShapeTypeInfo& outShapeType)const{
    outShapeType = CsgShapeTypeInfo{};
    if(!__hidden_shape_registry::ValidShapeTypeId(typeId))
        return false;

    const usize index = static_cast<usize>(typeId - 1u);
    if(index >= m_shapeTypes.size())
        return false;

    outShapeType = m_shapeTypes[index];
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RegisterBuiltInCsgShapeTypes(CsgShapeRegistry& registry){
    CsgShapeTypeId shapeTypeId = s_InvalidCsgShapeTypeId;
    bool result = true;

    result = registry.registerShapeType(
        __hidden_shape_registry::BuiltInShapeDesc(
            Name("engine/csg/box"),
            CsgBoxShapeParameters{},
            &__hidden_shape_registry::BoxBounds
        ),
        shapeTypeId,
        true
    ) && result;
    result = registry.registerShapeType(
        __hidden_shape_registry::BuiltInShapeDesc(
            Name("engine/csg/capsule"),
            CsgCapsuleShapeParameters{},
            &__hidden_shape_registry::CapsuleBounds
        ),
        shapeTypeId,
        true
    ) && result;
    result = registry.registerShapeType(
        __hidden_shape_registry::BuiltInShapeDesc(
            Name("engine/csg/plane"),
            CsgPlaneShapeParameters{},
            &__hidden_shape_registry::PlaneBounds
        ),
        shapeTypeId,
        true
    ) && result;
    result = registry.registerShapeType(
        __hidden_shape_registry::BuiltInShapeDesc(
            Name("engine/csg/sphere"),
            CsgSphereShapeParameters{},
            &__hidden_shape_registry::SphereBounds
        ),
        shapeTypeId,
        true
    ) && result;

    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

