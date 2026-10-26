// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"

#include <core/assets/auto_registration.h>
#include <core/assets/module.h>
#include <core/assets/ref.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename TAsset>
using SubsystemAssetRefVector = Core::Assets::AssetVector<Core::Assets::AssetRef<TAsset>>;

struct SubsystemFieldValueDescriptor{
    AStringView fieldName;
    AStringView expectedAssetTypeText;
    bool isArray = false;
};


interface ISubsystem : Core::Assets::IAsset{
public:
    NWB_DEFINE_ASSET_TYPE("subsystem")


public:
    virtual ~ISubsystem()noexcept override = 0;
protected:
    explicit ISubsystem(const Name& assetType, const Name& virtualPath = s_NameNone)noexcept
        : Core::Assets::IAsset(assetType, virtualPath)
    {}
    ISubsystem(const ISubsystem&) = default;
    ISubsystem(ISubsystem&&)noexcept = default;

protected:
    ISubsystem& operator=(const ISubsystem&) = default;
    ISubsystem& operator=(ISubsystem&&)noexcept = default;


public:
    [[nodiscard]] virtual bool loadBinary(Span<const u8> binary) = 0;
    [[nodiscard]] virtual bool validatePayload()const = 0;
    [[nodiscard]] virtual usize fieldCount()const noexcept = 0;
    [[nodiscard]] virtual SubsystemFieldValueDescriptor fieldDescriptor(usize index)const noexcept = 0;
    [[nodiscard]] virtual usize fieldValueCount(usize index)const noexcept = 0;
    [[nodiscard]] virtual Name fieldValue(usize index, usize elementIndex = 0u)const noexcept = 0;
    virtual void clearFieldValues(usize index)noexcept = 0;
    virtual void appendFieldValue(usize index, const Name& value) = 0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename SubsystemT>
struct SubsystemFieldDescriptor{
    AStringView fieldName;
    AStringView expectedAssetTypeText;
    bool isArray = false;
    usize (*count)(const SubsystemT& subsystem)noexcept = nullptr;
    Name (*get)(const SubsystemT& subsystem, usize elementIndex)noexcept = nullptr;
    void (*clear)(SubsystemT& subsystem)noexcept = nullptr;
    void (*append)(SubsystemT& subsystem, const Name& value) = nullptr;
};

template<typename SubsystemT, typename TAsset, Core::Assets::AssetRef<TAsset> SubsystemT::* Member>
[[nodiscard]] constexpr SubsystemFieldDescriptor<SubsystemT> MakeSubsystemFieldDescriptor(const AStringView fieldName)noexcept{
    static_assert(!TAsset::s_AssetTypeText.empty(), "Subsystem field asset type must define a non-empty s_AssetTypeText");
    return SubsystemFieldDescriptor<SubsystemT>{
        fieldName,
        TAsset::s_AssetTypeText,
        false,
        [](const SubsystemT&)noexcept -> usize{
            return 1u;
        },
        [](const SubsystemT& subsystem, const usize elementIndex)noexcept -> Name{
            NWB_ASSERT(elementIndex == 0u);
            return (subsystem.*Member).name();
        },
        [](SubsystemT& subsystem)noexcept{
            subsystem.*Member = {};
        },
        [](SubsystemT& subsystem, const Name& value){
            subsystem.*Member = Core::Assets::AssetRef<TAsset>(value);
        }
    };
}

template<typename SubsystemT, typename TAsset, SubsystemAssetRefVector<TAsset> SubsystemT::* Member>
[[nodiscard]] constexpr SubsystemFieldDescriptor<SubsystemT> MakeSubsystemFieldDescriptor(const AStringView fieldName)noexcept{
    static_assert(!TAsset::s_AssetTypeText.empty(), "Subsystem field asset type must define a non-empty s_AssetTypeText");
    return SubsystemFieldDescriptor<SubsystemT>{
        fieldName,
        TAsset::s_AssetTypeText,
        true,
        [](const SubsystemT& subsystem)noexcept -> usize{
            return (subsystem.*Member).size();
        },
        [](const SubsystemT& subsystem, const usize elementIndex)noexcept -> Name{
            NWB_ASSERT(elementIndex < (subsystem.*Member).size());
            return (subsystem.*Member)[elementIndex].name();
        },
        [](SubsystemT& subsystem)noexcept{
            (subsystem.*Member).clear();
        },
        [](SubsystemT& subsystem, const Name& value){
            (subsystem.*Member).push_back(Core::Assets::AssetRef<TAsset>(value));
        }
    };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SubsystemAssetRuntime{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool ValidatePayload(const ISubsystem& subsystem);
[[nodiscard]] bool LoadBinary(ISubsystem& subsystem, Span<const u8> binary);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename SubsystemT>
class TypedSubsystem : public ISubsystem{
public:
    [[nodiscard]] static Span<const SubsystemFieldDescriptor<SubsystemT>> FieldDescriptors()noexcept{
        static_assert(LengthOf(SubsystemT::s_Fields) > 0u, "Concrete subsystem must declare at least one asset field");
        return Span<const SubsystemFieldDescriptor<SubsystemT>>(SubsystemT::s_Fields, LengthOf(SubsystemT::s_Fields));
    }


public:
    explicit TypedSubsystem(Core::Assets::AssetArena&)noexcept(noexcept(DeclVal<void(*)(const Name&)noexcept>()(SubsystemT::AssetTypeName())))
        : ISubsystem(SubsystemT::AssetTypeName())
    {}
    TypedSubsystem(Core::Assets::AssetArena&, const Name& virtualPath)noexcept(noexcept(DeclVal<void(*)(const Name&)noexcept>()(SubsystemT::AssetTypeName())))
        : ISubsystem(SubsystemT::AssetTypeName(), virtualPath)
    {}
    TypedSubsystem(const TypedSubsystem&) = default;
    TypedSubsystem(TypedSubsystem&&)noexcept = default;
    virtual ~TypedSubsystem()noexcept override = 0;


public:
    TypedSubsystem& operator=(const TypedSubsystem&) = default;
    TypedSubsystem& operator=(TypedSubsystem&&)noexcept = default;


public:
    [[nodiscard]] virtual bool loadBinary(const Span<const u8> binary)override{
        return SubsystemAssetRuntime::LoadBinary(*this, binary);
    }

    [[nodiscard]] virtual bool validatePayload()const override{
        return SubsystemAssetRuntime::ValidatePayload(*this);
    }

    [[nodiscard]] virtual usize fieldCount()const noexcept override{
        return LengthOf(SubsystemT::s_Fields);
    }

    [[nodiscard]] virtual SubsystemFieldValueDescriptor fieldDescriptor(const usize index)const noexcept override{
        const auto descriptors = FieldDescriptors();
        NWB_ASSERT(index < descriptors.size());
        return SubsystemFieldValueDescriptor{ descriptors[index].fieldName, descriptors[index].expectedAssetTypeText, descriptors[index].isArray };
    }

    [[nodiscard]] virtual usize fieldValueCount(const usize index)const noexcept override{
        const auto descriptors = FieldDescriptors();
        NWB_ASSERT(index < descriptors.size());
        return descriptors[index].count(static_cast<const SubsystemT&>(*this));
    }

    [[nodiscard]] virtual Name fieldValue(const usize index, const usize elementIndex = 0u)const noexcept override{
        const auto descriptors = FieldDescriptors();
        NWB_ASSERT(index < descriptors.size());
        return descriptors[index].get(static_cast<const SubsystemT&>(*this), elementIndex);
    }

    virtual void clearFieldValues(const usize index)noexcept override{
        const auto descriptors = FieldDescriptors();
        NWB_ASSERT(index < descriptors.size());
        descriptors[index].clear(static_cast<SubsystemT&>(*this));
    }

    virtual void appendFieldValue(const usize index, const Name& value)override{
        const auto descriptors = FieldDescriptors();
        NWB_ASSERT(index < descriptors.size());
        descriptors[index].append(static_cast<SubsystemT&>(*this), value);
    }
};

template<typename SubsystemT>
TypedSubsystem<SubsystemT>::~TypedSubsystem()noexcept = default;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using SubsystemInstanceFactory = UniquePtr<ISubsystem>(*)(Core::Assets::AssetArena& arena, const Name& virtualPath);

class SubsystemTypeAutoRegistrar final : NoCopy{
public:
    SubsystemTypeAutoRegistrar(const Name& assetType, SubsystemInstanceFactory create)noexcept;


public:
    [[nodiscard]] const Name& assetType()const noexcept{ return m_assetType; }
    [[nodiscard]] SubsystemInstanceFactory create()const noexcept{ return m_create; }
    [[nodiscard]] const SubsystemTypeAutoRegistrar* next()const noexcept{ return m_next; }


private:
    Name m_assetType = s_NameNone;
    SubsystemInstanceFactory m_create = nullptr;
    const SubsystemTypeAutoRegistrar* m_next = nullptr;
};


[[nodiscard]] bool IsSubsystemAssetType(const Name& assetType)noexcept;
[[nodiscard]] const ISubsystem* CastSubsystem(const Core::Assets::IAsset* asset)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class SubsystemAssetCodec final : public Core::Assets::IAssetCodec{
public:
    SubsystemAssetCodec()noexcept
        : Core::Assets::IAssetCodec(ISubsystem::AssetTypeName())
    {}


public:
    [[nodiscard]] virtual Expected<UniquePtr<Core::Assets::IAsset>> deserialize(Core::Assets::AssetArena& arena, const Name& virtualPath, Span<const u8> binary)const override;
#if defined(NWB_COOK)
    virtual bool serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const override;
#endif
};


#if defined(NWB_COOK)
namespace SubsystemAssetSerialization{
[[nodiscard]] bool Serialize(const ISubsystem& subsystem, Core::Assets::AssetBytes& outBinary);
};
#endif


template<typename SubsystemT>
class TypedSubsystemAssetCodec final : public Core::Assets::AssetCodec<SubsystemT>{
public:
    TypedSubsystemAssetCodec()noexcept(noexcept(DeclVal<void(*)(const Name&)noexcept>()(SubsystemT::AssetTypeName()))) = default;


#if defined(NWB_COOK)
public:
    virtual bool serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const override{
        if(!this->checkSerializeAssetType(asset, NWB_TEXT("TypedSubsystemAssetCodec::serialize")))
            return false;

        return SubsystemAssetSerialization::Serialize(*MakeNotNull(checked_cast<const SubsystemT*>(&asset)), outBinary);
    }
#endif
};


template<typename SubsystemT>
class TypedSubsystemAutoRegistrar final : NoCopy{
private:
    [[nodiscard]] static UniquePtr<ISubsystem> CreateInstance(Core::Assets::AssetArena& arena, const Name& virtualPath){
        return MakeUnique<SubsystemT>(arena, virtualPath);
    }


public:
    TypedSubsystemAutoRegistrar()noexcept
        : m_typeRegistrar(SubsystemT::AssetTypeName(), &CreateInstance)
        , m_codecRegistrar(&Core::Assets::CreateAssetCodec<TypedSubsystemAssetCodec<SubsystemT>>)
    {}


private:
    SubsystemTypeAutoRegistrar m_typeRegistrar;
    Core::Assets::AssetCodecAutoRegistrar m_codecRegistrar;
};

#define NWB_DEFINE_SUBSYSTEM_ASSET_REGISTRAR(registrarVariable, subsystemType) \
    TypedSubsystemAutoRegistrar<subsystemType> registrarVariable


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

