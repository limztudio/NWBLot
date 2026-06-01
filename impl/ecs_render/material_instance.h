// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "components.h"

#include <core/common/log.h>
#include <core/ecs/world.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool SplitMaterialInstanceParameterName(
    const AStringView parameterName,
    Name& outParameterName,
    Name& outBlockName,
    Name& outFieldName
){
    outParameterName = NAME_NONE;
    outBlockName = NAME_NONE;
    outFieldName = NAME_NONE;

    const usize dotIndex = parameterName.find('.');
    if(parameterName.empty() || dotIndex == AStringView::npos || dotIndex == 0u || dotIndex + 1u >= parameterName.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("MaterialInstanceComponent: parameter '{}' must use block.field form")
            , StringConvert(parameterName)
        );
        return false;
    }
    if(parameterName.find('.', dotIndex + 1u) != AStringView::npos){
        NWB_LOGGER_ERROR(NWB_TEXT("MaterialInstanceComponent: parameter '{}' must not contain more than one block separator")
            , StringConvert(parameterName)
        );
        return false;
    }

    outParameterName = Name(parameterName);
    outBlockName = Name(parameterName.substr(0u, dotIndex));
    outFieldName = Name(parameterName.substr(dotIndex + 1u));
    return true;
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

    [[nodiscard]] static UInt4U pack(const f32 value){
        return PackMaterialInstanceBytes(&value, sizeof(value));
    }
};

template<>
struct MaterialInstanceValueTraits<Float4>{
    static constexpr MaterialLayoutFieldType::Enum s_FieldType = MaterialLayoutFieldType::Float4;

    [[nodiscard]] static UInt4U pack(const Float4& value){
        return PackMaterialInstanceBytes(value.raw, sizeof(f32) * 4u);
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
        , StringConvert(materialInterface.c_str())
        , StringConvert(component.materialInterface.c_str())
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
    if(!IsValidMaterialLayoutFieldType(fieldType)){
        NWB_LOGGER_ERROR(NWB_TEXT("MaterialInstanceComponent: parameter '{}' uses invalid field type")
            , StringConvert(parameterNameText)
        );
        return false;
    }
    if(!ValidateMaterialInstanceInterface(component, materialInterface, parameterNameText))
        return false;

    MaterialInstanceParameter parameter;
    if(!SplitMaterialInstanceParameterName(
        parameterNameText,
        parameter.parameterName,
        parameter.blockName,
        parameter.fieldName
    ))
        return false;
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
        MaterialInstanceValueTraits<TValue>::pack(value)
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

