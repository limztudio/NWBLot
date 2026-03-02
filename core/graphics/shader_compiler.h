// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/global.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ShaderCompilerRequest{
    AStringView shaderName;
    AStringView compiler;
    AStringView stage;
    AStringView targetProfile;
    AStringView entryPoint;
    const HashMap<AString, AString>& defineCombo;
    const Vector<Path>& includeDirectories;
    const Path& sourcePath;
    const Path& cachePath;
};


class IShaderCompiler : NoCopy{
public:
    virtual ~IShaderCompiler() = default;


public:
    virtual bool compileVariant(const ShaderCompilerRequest& request, AString& outError) = 0;
};


struct ShaderCompilerFactory{
    using CreateFunction = IShaderCompiler* (*)();

    AStringView backendType;
    CreateFunction createFunction = nullptr;
    ShaderCompilerFactory* next = nullptr;
};


class AutoShaderCompilerFactoryRegistration final : NoCopy{
public:
    explicit AutoShaderCompilerFactoryRegistration(ShaderCompilerFactory& factory);
};


void RegisterShaderCompilerFactory(ShaderCompilerFactory& factory);
[[nodiscard]] UniquePtr<IShaderCompiler> CreateShaderCompiler(AStringView backendType, AString& outError);


class ShaderCompile : NoCopy{
public:
    explicit ShaderCompile(AStringView backendType = "vulkan");
    ~ShaderCompile();


public:
    [[nodiscard]] IShaderCompiler* getCompiler()const noexcept{ return m_compiler.get(); }
    [[nodiscard]] const AString& error()const noexcept{ return m_error; }
    [[nodiscard]] bool valid()const noexcept{ return m_compiler != nullptr; }


private:
    UniquePtr<IShaderCompiler> m_compiler;
    AString m_error;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

