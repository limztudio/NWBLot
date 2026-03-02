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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


AutoShaderCompilerFactoryRegistration::AutoShaderCompilerFactoryRegistration(ShaderCompilerFactory& factory){
    RegisterShaderCompilerFactory(factory);
}


void RegisterShaderCompilerFactory(ShaderCompilerFactory& factory){
    if(factory.backendType.empty())
        return;
    if(factory.createFunction == nullptr)
        return;

    ScopedLock lock(__hidden_shader_compile::s_FactoryMutex);
    if(__hidden_shader_compile::AlreadyRegistered(factory))
        return;

    factory.next = __hidden_shader_compile::s_FactoryHead;
    __hidden_shader_compile::s_FactoryHead = &factory;
}


UniquePtr<IShaderCompiler> CreateShaderCompiler(const AStringView backendType, AString& outError){
    outError.clear();

    const AString canonicalBackendType = CanonicalizeText(backendType);

    ScopedLock lock(__hidden_shader_compile::s_FactoryMutex);

    ShaderCompilerFactory* selectedFactory = nullptr;
    ShaderCompilerFactory* firstFactory = nullptr;
    ShaderCompilerFactory* node = __hidden_shader_compile::s_FactoryHead;
    while(node != nullptr){
        if(node->createFunction == nullptr){
            node = node->next;
            continue;
        }

        if(firstFactory == nullptr)
            firstFactory = node;

        if(canonicalBackendType.empty()){
            selectedFactory = firstFactory;
            break;
        }

        if(CanonicalizeText(node->backendType) == canonicalBackendType){
            selectedFactory = node;
            break;
        }

        node = node->next;
    }

    if(selectedFactory == nullptr)
        selectedFactory = firstFactory;

    if(selectedFactory == nullptr){
        outError = "No shader compiler factory is registered";
        return {};
    }

    IShaderCompiler* compiler = selectedFactory->createFunction();
    if(compiler == nullptr){
        outError = StringFormat(
            "Shader compiler factory '{}' returned null compiler instance",
            selectedFactory->backendType
        );
        return {};
    }

    return UniquePtr<IShaderCompiler>(compiler);
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
