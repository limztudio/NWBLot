// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/assets_shader/shader_cook.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace MaterialBindNames{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline AStringView SourceExtensionText(){
    static constexpr AStringView s_ExtensionText = ".bind";
    return s_ExtensionText;
}

inline AStringView GeneratedIncludeCacheDirectoryText(){
    static constexpr AStringView s_DirectoryText = "material_bind_includes";
    return s_DirectoryText;
}

inline AStringView TypedBindingImplicitDefineText(){
    static constexpr AStringView s_DefineText = "NWB_MATERIAL_TYPED_BINDING";
    return s_DefineText;
}

inline AStringView TypedBindingImplicitDefineValueText(){
    static constexpr AStringView s_DefineValueText = "1";
    return s_DefineValueText;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct MaterialBindAttribute{
    ShaderCook::CookString name;
    ShaderCook::CookVector<ShaderCook::CookString> arguments;

    explicit MaterialBindAttribute(ShaderCook::CookArena& memoryArena)
        : name(memoryArena)
        , arguments(memoryArena)
    {}
};

struct MaterialBindField{
    ShaderCook::CookString type;
    ShaderCook::CookString name;
    ShaderCook::CookVector<MaterialBindAttribute> attributes;

    explicit MaterialBindField(ShaderCook::CookArena& memoryArena)
        : type(memoryArena)
        , name(memoryArena)
        , attributes(memoryArena)
    {}

    [[nodiscard]] const MaterialBindAttribute* findAttribute(AStringView attributeName)const;
    [[nodiscard]] AStringView defaultArgument()const;
};

struct MaterialBindStruct{
    ShaderCook::CookString name;
    ShaderCook::CookVector<MaterialBindAttribute> attributes;
    ShaderCook::CookVector<MaterialBindField> fields;

    explicit MaterialBindStruct(ShaderCook::CookArena& memoryArena)
        : name(memoryArena)
        , attributes(memoryArena)
        , fields(memoryArena)
    {}

    [[nodiscard]] const MaterialBindField* findField(AStringView fieldName)const;
    [[nodiscard]] const MaterialBindAttribute* findAttribute(AStringView attributeName)const;
};

struct MaterialBindInstance{
    ShaderCook::CookString type;
    ShaderCook::CookString name;

    explicit MaterialBindInstance(ShaderCook::CookArena& memoryArena)
        : type(memoryArena)
        , name(memoryArena)
    {}
};

struct MaterialBindEntry{
    ShaderCook::CookString source;
    ShaderCook::CookString virtualPath;
    ShaderCook::CookVector<MaterialBindStruct> structs;
    ShaderCook::CookVector<MaterialBindInstance> instances;

    explicit MaterialBindEntry(ShaderCook::CookArena& memoryArena)
        : source(memoryArena)
        , virtualPath(memoryArena)
        , structs(memoryArena)
        , instances(memoryArena)
    {}

    void reset(){
        source.clear();
        virtualPath.clear();
        structs.clear();
        instances.clear();
    }

    [[nodiscard]] const MaterialBindStruct* findStruct(AStringView typeName)const;
    [[nodiscard]] const MaterialBindInstance* findInstance(AStringView instanceName)const;
    [[nodiscard]] const MaterialBindField* findParameter(
        AStringView parameterName,
        const MaterialBindInstance** outInstance = nullptr,
        const MaterialBindStruct** outStruct = nullptr
    )const;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool ParseMaterialBindSource(const Path& bindFilePath, MaterialBindEntry& outEntry);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

