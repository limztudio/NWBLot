// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/components.adhd.h>
#include <impl/ecs_render/material/pipeline_types.adhd.h>

#include <core/ecs/world.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Expected<MaterialInstanceParameterNames> SplitMaterialInstanceParameterName(AStringView parameterName);

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

[[nodiscard]] b8 ValidateMaterialInstanceInterface(const MaterialInstanceComponent& component, const Name& materialInterface, AStringView parameterNameText);
[[nodiscard]] b8 StoreMaterialMutableParameter(MaterialInstanceComponent& component, MaterialInstanceParameter parameter, AStringView parameterNameText);
[[nodiscard]] MaterialInstanceComponent* ResolveMaterialInstanceComponentForSet(Core::ECS::World& world, Core::ECS::EntityID entity);
[[nodiscard]] b8 SetMaterialMutableParameter(MaterialInstanceComponent& component, const Name& materialInterface, AStringView parameterNameText, MaterialLayoutFieldType::Enum fieldType, UInt4U value);
[[nodiscard]] b8 SetMaterialMutableParameter(Core::ECS::World& world, Core::ECS::EntityID entity, const Name& materialInterface, AStringView parameterName, MaterialLayoutFieldType::Enum fieldType, UInt4U value);

template<typename TValue>
[[nodiscard]] inline b8 SetMaterialMutableValue(Core::ECS::World& world, const Core::ECS::EntityID entity, const Name& materialInterface, const AStringView parameterName, const TValue& value){
    return SetMaterialMutableParameter(world, entity, materialInterface, parameterName, MaterialInstanceValueTraits<TValue>::s_FieldType, MaterialInstanceValueTraits<TValue>::Pack(value));
}

[[nodiscard]] b8 SetMaterialMutableHalf4(Core::ECS::World& world, Core::ECS::EntityID entity, const Name& materialInterface, AStringView parameterName, const Float4& value);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

