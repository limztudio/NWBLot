// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "material_instance.h"

#include <core/common/log.h>

#include <global/simd/convert.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<MaterialInstanceParameterNames> SplitMaterialInstanceParameterName(const AStringView parameterName){
    const usize dotIndex = parameterName.find('.');
    if(parameterName.empty() || dotIndex == AStringView::npos || dotIndex == 0u || dotIndex + 1u >= parameterName.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("MaterialInstanceComponent: parameter '{}' must use block.field form"), ConvertString(parameterName));
        return MakeUnexpected(Failure{});
    }
    if(parameterName.find('.', dotIndex + 1u) != AStringView::npos){
        NWB_LOGGER_ERROR(NWB_TEXT("MaterialInstanceComponent: parameter '{}' must not contain more than one block separator"), ConvertString(parameterName));
        return MakeUnexpected(Failure{});
    }

    const auto name = Name::FromText(parameterName);
    const auto blockName = Name::FromText(parameterName.substr(0u, dotIndex));
    const auto fieldName = Name::FromText(parameterName.substr(dotIndex + 1u));
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

b8 ValidateMaterialInstanceInterface(const MaterialInstanceComponent& component, const Name& materialInterface, const AStringView parameterNameText){
    if(!materialInterface){
        NWB_LOGGER_ERROR(NWB_TEXT("MaterialInstanceComponent: parameter '{}' requires a material interface"), ConvertString(parameterNameText));
        return false;
    }
    if(!component.materialInterface){
        NWB_LOGGER_ERROR(NWB_TEXT("MaterialInstanceComponent: parameter '{}' requires a component material interface"), ConvertString(parameterNameText));
        return false;
    }
    if(component.materialInterface == materialInterface)
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("MaterialInstanceComponent: parameter '{}' targets interface '{}' but component expects '{}'")
        , ConvertString(parameterNameText)
        , ConvertString(materialInterface.resolvedText())
        , ConvertString(component.materialInterface.resolvedText())
    );
    return false;
}

b8 StoreMaterialMutableParameter(MaterialInstanceComponent& component, MaterialInstanceParameter parameter, const AStringView parameterNameText){
    if(TryStoreMaterialMutableParameter(component, parameter))
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("MaterialInstanceComponent: parameter '{}' was already set with a different type"), ConvertString(parameterNameText));
    return false;
}

MaterialInstanceComponent* ResolveMaterialInstanceComponentForSet(Core::ECS::World& world, const Core::ECS::EntityID entity){
    MaterialInstanceComponent* component = world.tryGetComponent<MaterialInstanceComponent>(entity);
    if(component)
        return component;

    NWB_LOGGER_ERROR(NWB_TEXT("MaterialInstanceComponent: entity {} has no material instance component"), entity.id);
    return nullptr;
}

b8 SetMaterialMutableParameter(MaterialInstanceComponent& component, const Name& materialInterface, const AStringView parameterNameText, const MaterialLayoutFieldType::Enum fieldType, const UInt4U value){
    if(!IsMaterialLayoutNumericFieldType(fieldType)){
        NWB_LOGGER_ERROR(NWB_TEXT("MaterialInstanceComponent: parameter '{}' must use a numeric mutable field type"), ConvertString(parameterNameText));
        return false;
    }
    if(!ValidateMaterialInstanceInterface(component, materialInterface, parameterNameText))
        return false;

    const auto names = SplitMaterialInstanceParameterName(parameterNameText);
    if(!names)
        return false;

    const MaterialInstanceParameter parameter = BuildMaterialInstanceParameter(*names, fieldType, value);
    return StoreMaterialMutableParameter(component, parameter, parameterNameText);
}

b8 SetMaterialMutableParameter(Core::ECS::World& world, const Core::ECS::EntityID entity, const Name& materialInterface, const AStringView parameterName, const MaterialLayoutFieldType::Enum fieldType, const UInt4U value){
    MaterialInstanceComponent* component = ResolveMaterialInstanceComponentForSet(world, entity);
    if(!component)
        return false;

    return SetMaterialMutableParameter(*component, materialInterface, parameterName, fieldType, value);
}

b8 SetMaterialMutableHalf4(Core::ECS::World& world, const Core::ECS::EntityID entity, const Name& materialInterface, const AStringView parameterName, const Float4& value){
    const Half4U packedValue = MakeHalf4U(value.x, value.y, value.z, value.w);
    return SetMaterialMutableValue(world, entity, materialInterface, parameterName, packedValue);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

