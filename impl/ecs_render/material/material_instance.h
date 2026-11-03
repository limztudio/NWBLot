// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/components.adhd.h>
#include <impl/ecs_render/material/pipeline_types.adhd.h>

#include <core/common/log.h>
#include <core/ecs/world.h>

#include <global/simd/convert.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline Expected<MaterialInstanceParameterNames> SplitMaterialInstanceParameterName(const AStringView parameterName){
    const Expected<usize> dotIndex = TryFindMaterialParameterNameDotIndex(parameterName);
    if(!dotIndex)
        return MakeUnexpected(Failure{});

    const auto name = Name::FromText(parameterName);
    const auto blockName = Name::FromText(parameterName.substr(0u, *dotIndex));
    const auto fieldName = Name::FromText(parameterName.substr(*dotIndex + 1u));
    if(!name || !blockName || !fieldName){
        NWB_LOGGER_ERROR(NWB_TEXT("MaterialInstanceComponent: parameter '{}' contains an embedded null"), ConvertString(parameterName));
        return MakeUnexpected(Failure{});
    }
    MaterialInstanceParameterNames result;
    result.parameterName = *name;
    result.blockName = *blockName;
    result.fieldName = *fieldName;
    return result;
}

template<typename TElement>
[[nodiscard]] inline UInt4U PackMaterialInstanceBytes(const Span<const TElement> elements)noexcept{
    return PackMaterialInstanceBytes(Span<const u8>(reinterpret_cast<const u8*>(elements.data()), elements.size() * sizeof(TElement)));
}

template<typename TValue>
[[nodiscard]] inline UInt4U PackMaterialInstanceValue(const TValue& value)noexcept{
    return PackMaterialInstanceBytes(Span<const u8>(reinterpret_cast<const u8*>(&value), sizeof(TValue)));
}

template<typename TValue>
struct MaterialInstanceValueTraits;

template<>
struct MaterialInstanceValueTraits<f32>{
    static constexpr MaterialLayoutFieldType::Enum s_FieldType = MaterialLayoutFieldType::f32;


public:
    [[nodiscard]] static UInt4U Pack(const f32 value)noexcept{
        return PackMaterialInstanceValue(value);
    }
};

template<>
struct MaterialInstanceValueTraits<Half>{
    static constexpr MaterialLayoutFieldType::Enum s_FieldType = MaterialLayoutFieldType::f16;


public:
    [[nodiscard]] static UInt4U Pack(const Half value)noexcept{
        return PackMaterialInstanceValue(value);
    }
};

template<>
struct MaterialInstanceValueTraits<Float4>{
    static constexpr MaterialLayoutFieldType::Enum s_FieldType = MaterialLayoutFieldType::f32x4;


public:
    [[nodiscard]] static UInt4U Pack(const Float4& value)noexcept{
        return PackMaterialInstanceBytes(Span<const u8>(reinterpret_cast<const u8*>(value.raw), sizeof(f32) * s_MaterialInstanceVectorComponentCount));
    }
};

template<>
struct MaterialInstanceValueTraits<Half4U>{
    static constexpr MaterialLayoutFieldType::Enum s_FieldType = MaterialLayoutFieldType::f16x4;


public:
    [[nodiscard]] static UInt4U Pack(const Half4U& value)noexcept{
        return PackMaterialInstanceBytes(Span<const u8>(reinterpret_cast<const u8*>(value.raw), sizeof(Half) * s_MaterialInstanceVectorComponentCount));
    }
};

[[nodiscard]] inline MaterialInstanceComponent* ResolveMaterialInstanceComponentForSet(Core::ECS::World& world, const Core::ECS::EntityID entity){
    MaterialInstanceComponent* component = world.tryGetComponent<MaterialInstanceComponent>(entity);
    if(component)
        return component;

    NWB_LOGGER_ERROR(NWB_TEXT("MaterialInstanceComponent: entity {} has no material instance component"), entity.id);
    return nullptr;
}

[[nodiscard]] inline b8 SetMaterialMutableParameter(MaterialInstanceComponent& component, const Name& materialInterface, const AStringView parameterNameText, const MaterialLayoutFieldType::Enum fieldType, const UInt4U value){
    if(!ValidateMaterialInstanceNumericFieldType(fieldType, parameterNameText))
        return false;
    if(!ValidateMaterialInstanceInterface(component, materialInterface, parameterNameText))
        return false;

    const auto names = SplitMaterialInstanceParameterName(parameterNameText);
    if(!names)
        return false;

    const MaterialInstanceParameter parameter = BuildMaterialInstanceParameter(*names, fieldType, value);
    return StoreMaterialMutableParameter(component, parameter, parameterNameText);
}

[[nodiscard]] inline b8 SetMaterialMutableParameter(Core::ECS::World& world, const Core::ECS::EntityID entity, const Name& materialInterface, const AStringView parameterName, const MaterialLayoutFieldType::Enum fieldType, const UInt4U value){
    MaterialInstanceComponent* component = ResolveMaterialInstanceComponentForSet(world, entity);
    if(!component)
        return false;

    return SetMaterialMutableParameter(*component, materialInterface, parameterName, fieldType, value);
}

template<typename TValue>
[[nodiscard]] inline b8 SetMaterialMutableValue(Core::ECS::World& world, const Core::ECS::EntityID entity, const Name& materialInterface, const AStringView parameterName, const TValue& value){
    return SetMaterialMutableParameter(world, entity, materialInterface, parameterName, MaterialInstanceValueTraits<TValue>::s_FieldType, MaterialInstanceValueTraits<TValue>::Pack(value));
}

[[nodiscard]] inline b8 SetMaterialMutableHalf4(Core::ECS::World& world, const Core::ECS::EntityID entity, const Name& materialInterface, const AStringView parameterName, const Float4& value){
    const Half4U packedValue = MakeHalf4U(value.x, value.y, value.z, value.w);
    return SetMaterialMutableValue(world, entity, materialInterface, parameterName, packedValue);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

