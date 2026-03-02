// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "shader_compiler.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_shader_compile{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Futex s_FactoryMutex;
static ShaderCompilerFactory* s_FactoryHead = nullptr;


static bool AlreadyRegistered(const ShaderCompilerFactory& factory){
    ShaderCompilerFactory* node = s_FactoryHead;
    while(node != nullptr){
        if(node == &factory)
            return true;

        node = node->next;
    }

    return false;
}


static bool BackendTypeAlreadyRegistered(const AStringView backendType){
    ShaderCompilerFactory* node = s_FactoryHead;
    while(node != nullptr){
        if(node->createFunction != nullptr && CanonicalizeText(node->backendType) == backendType)
            return true;

        node = node->next;
    }

    return false;
}


static ShaderCompilerFactory* FindFirstFactory(){
    ShaderCompilerFactory* node = s_FactoryHead;
    while(node != nullptr){
        if(node->createFunction != nullptr)
            return node;

        node = node->next;
    }

    return nullptr;
}


static ShaderCompilerFactory* FindFactoryByBackendType(const AStringView backendType){
    ShaderCompilerFactory* node = s_FactoryHead;
    while(node != nullptr){
        if(node->createFunction != nullptr && CanonicalizeText(node->backendType) == backendType)
            return node;

        node = node->next;
    }

    return nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


AutoShaderCompilerFactoryRegistration::AutoShaderCompilerFactoryRegistration(ShaderCompilerFactory& factory){
    RegisterShaderCompilerFactory(factory);
}


void RegisterShaderCompilerFactory(ShaderCompilerFactory& factory){
    if(factory.createFunction == nullptr)
        return;

    const AString canonicalBackendType = CanonicalizeText(factory.backendType);
    if(canonicalBackendType.empty())
        return;

    ScopedLock lock(__hidden_shader_compile::s_FactoryMutex);
    if(__hidden_shader_compile::AlreadyRegistered(factory))
        return;
    if(__hidden_shader_compile::BackendTypeAlreadyRegistered(canonicalBackendType))
        return;

    factory.next = __hidden_shader_compile::s_FactoryHead;
    __hidden_shader_compile::s_FactoryHead = &factory;
}


UniquePtr<IShaderCompiler> CreateShaderCompiler(const AStringView backendType, AString& outError){
    outError.clear();

    const AString canonicalBackendType = CanonicalizeText(backendType);

    ShaderCompilerFactory::CreateFunction createFunction = nullptr;
    AString selectedBackendType;
    {
        ScopedLock lock(__hidden_shader_compile::s_FactoryMutex);

        ShaderCompilerFactory* selectedFactory = nullptr;
        if(!canonicalBackendType.empty())
            selectedFactory = __hidden_shader_compile::FindFactoryByBackendType(canonicalBackendType);
        if(selectedFactory == nullptr)
            selectedFactory = __hidden_shader_compile::FindFirstFactory();

        if(selectedFactory == nullptr){
            outError = "No shader compiler factory is registered";
            return {};
        }

        createFunction = selectedFactory->createFunction;
        selectedBackendType = AString(selectedFactory->backendType);
    }

    if(createFunction == nullptr){
        outError = "No shader compiler factory is registered";
        return {};
    }

    UniquePtr<IShaderCompiler> compiler = createFunction();
    if(compiler == nullptr){
        outError = StringFormat(
            "Shader compiler factory '{}' returned null compiler instance",
            selectedBackendType
        );
        return {};
    }

    return compiler;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ShaderCompile::ShaderCompile(const AStringView backendType)
    : m_compiler(CreateShaderCompiler(backendType, m_error))
{}


ShaderCompile::~ShaderCompile() = default;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

