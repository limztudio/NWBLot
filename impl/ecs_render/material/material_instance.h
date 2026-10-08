// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/components.h>

#include <core/common/log.h>
#include <core/ecs/world.h>
#include <global/math/convert.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct MaterialInstanceParameterNames{
    Name parameterName = s_NameNone;
    Name blockName = s_NameNone;
    Name fieldName = s_NameNone;
};

[[nodiscard]] inline Expected<MaterialInstanceParameterNames> SplitMaterialInstanceParameterName(
    const AStringView parameterName
){
    const usize dotIndex = parameterName.find('.');
    if(parameterName.empty() || dotIndex == AStringView::npos || dotIndex == 0u || dotIndex + 1u >= parameterName.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("MaterialInstanceComponent: parameter '{}' must use block.field form")
            , StringConvert(parameterName)
        );
        return MakeUnexpected(Failure{});
    }
    if(parameterName.find('.', dotIndex + 1u) != AStringView::npos){
        NWB_LOGGER_ERROR(NWB_TEXT("MaterialInstanceComponent: parameter '{}' must not contain more than one block separator")
            , StringConvert(parameterName)
        );
        return MakeUnexpected(Failure{});
    }

    return MaterialInstanceParameterNames{ Name(parameterName), Name(parameterName.substr(0u, dotIndex)), Name(parameterName.substr(dotIndex + 1u)) };
}

[[nodiscard]] inline UInt4U PackMaterialInstanceBytes(const void* bytes, const usize byteCount){
    UInt4U packed = {};
    NWB_ASSERT(byteCount <= sizeof(packed.raw));
    NWB_MEMCPY(packed.raw, sizeof(packed.raw), bytes, byteCount);
    return packed;
}

template<typename TValue>
struct MaterialInstanceValueTraits;

template<>
struct MaterialInstanceValueTraits<f32>{
    static constexpr MaterialLayoutFieldType::Enum s_FieldType = MaterialLayoutFieldType::Float;

    [[nodiscard]] static UInt4U Pack(const f32 value){
        return PackMaterialInstanceBytes(&value, sizeof(value));
    }
};

template<>
struct MaterialInstanceValueTraits<Float4>{
    static constexpr MaterialLayoutFieldType::Enum s_FieldType = MaterialLayoutFieldType::Float4;

    [[nodiscard]] static UInt4U Pack(const Float4& value){
        return PackMaterialInstanceBytes(value.raw, sizeof(f32) * 4u);
    }
};

template<>
struct MaterialInstanceValueTraits<Half4U>{
    static constexpr MaterialLayoutFieldType::Enum s_FieldType = MaterialLayoutFieldType::Half4;

    [[nodiscard]] static UInt4U Pack(const Half4U& value){
        return PackMaterialInstanceBytes(value.raw, sizeof(Half) * 4u);
    }
};

[[nodiscard]] inline bool ValidateMaterialInstanceInterface(
    const MaterialInstanceComponent& component,
    const Name& materialInterface,
    const AStringView parameterNameText
){
    if(!materialInterface){
        NWB_LOGGER_ERROR(NWB_TEXT("MaterialInstanceComponent: parameter '{}' requires a material interface")
            , StringConvert(parameterNameText)
        );
        return false;
    }
    if(!component.materialInterface){
        NWB_LOGGER_ERROR(NWB_TEXT("MaterialInstanceComponent: parameter '{}' requires a component material interface")
            , StringConvert(parameterNameText)
        );
        return false;
    }
    if(component.materialInterface == materialInterface)
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("MaterialInstanceComponent: parameter '{}' targets interface '{}' but component expects '{}'")
        , StringConvert(parameterNameText)
        , StringConvert(materialInterface.resolvedText())
        , StringConvert(component.materialInterface.resolvedText())
    );
    return false;
}

[[nodiscard]] inline bool StoreMaterialMutableParameter(
    MaterialInstanceComponent& component,
    MaterialInstanceParameter parameter,
    const AStringView parameterNameText
){
    for(MaterialInstanceParameter& existingParameter : component.overrides){
        if(existingParameter.parameterName != parameter.parameterName)
            continue;

        if(existingParameter.fieldType != parameter.fieldType){
            NWB_LOGGER_ERROR(NWB_TEXT("MaterialInstanceComponent: parameter '{}' was already set with a different type")
                , StringConvert(parameterNameText)
            );
            return false;
        }

        existingParameter = parameter;
        ++component.revision;
        return true;
    }

    component.overrides.push_back(parameter);
    ++component.revision;
    return true;
}

[[nodiscard]] inline bool SetMaterialMutableParameter(
    MaterialInstanceComponent& component,
    const Name& materialInterface,
    const AStringView parameterNameText,
    const MaterialLayoutFieldType::Enum fieldType,
    const UInt4U value
){
    if(!IsMaterialLayoutNumericFieldType(fieldType)){
        NWB_LOGGER_ERROR(NWB_TEXT("MaterialInstanceComponent: parameter '{}' must use a numeric mutable field type")
            , StringConvert(parameterNameText)
        );
        return false;
    }
    if(!ValidateMaterialInstanceInterface(component, materialInterface, parameterNameText))
        return false;

    const auto names = SplitMaterialInstanceParameterName(parameterNameText);
    if(!names)
        return false;
    MaterialInstanceParameter parameter;
    parameter.parameterName = names->parameterName;
    parameter.blockName = names->blockName;
    parameter.fieldName = names->fieldName;
    parameter.fieldType = fieldType;
    parameter.value = value;

    return StoreMaterialMutableParameter(component, parameter, parameterNameText);
}

[[nodiscard]] inline MaterialInstanceComponent* ResolveMaterialInstanceComponentForSet(
    Core::ECS::World& world,
    const Core::ECS::EntityID entity
){
    MaterialInstanceComponent* component = world.tryGetComponent<MaterialInstanceComponent>(entity);
    if(component)
        return component;

    NWB_LOGGER_ERROR(NWB_TEXT("MaterialInstanceComponent: entity {} has no material instance component"), entity.id);
    return nullptr;
}

[[nodiscard]] inline bool SetMaterialMutableParameter(
    Core::ECS::World& world,
    const Core::ECS::EntityID entity,
    const Name& materialInterface,
    const AStringView parameterName,
    const MaterialLayoutFieldType::Enum fieldType,
    const UInt4U value
){
    MaterialInstanceComponent* component = ResolveMaterialInstanceComponentForSet(world, entity);
    if(!component)
        return false;

    return SetMaterialMutableParameter(*component, materialInterface, parameterName, fieldType, value);
}

template<typename TValue>
[[nodiscard]] inline bool SetMaterialMutableValue(
    Core::ECS::World& world,
    const Core::ECS::EntityID entity,
    const Name& materialInterface,
    const AStringView parameterName,
    const TValue& value
){
    return SetMaterialMutableParameter(
        world,
        entity,
        materialInterface,
        parameterName,
        MaterialInstanceValueTraits<TValue>::s_FieldType,
        MaterialInstanceValueTraits<TValue>::Pack(value)
    );
}

[[nodiscard]] inline bool SetMaterialMutableFloat(
    Core::ECS::World& world,
    const Core::ECS::EntityID entity,
    const Name& materialInterface,
    const AStringView parameterName,
    const f32 value
){
    return SetMaterialMutableValue(world, entity, materialInterface, parameterName, value);
}

[[nodiscard]] inline bool SetMaterialMutableFloat4(
    Core::ECS::World& world,
    const Core::ECS::EntityID entity,
    const Name& materialInterface,
    const AStringView parameterName,
    const Float4& value
){
    return SetMaterialMutableValue(world, entity, materialInterface, parameterName, value);
}

[[nodiscard]] inline bool SetMaterialMutableHalf4(
    Core::ECS::World& world,
    const Core::ECS::EntityID entity,
    const Name& materialInterface,
    const AStringView parameterName,
    const Float4& value
){
    const Half4U packedValue = MakeHalf4U(value.x, value.y, value.z, value.w);
    return SetMaterialMutableValue(world, entity, materialInterface, parameterName, packedValue);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

