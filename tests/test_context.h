// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/alloc/custom.h>
#include <core/alloc/memory.h>
#include <core/alloc/thread.h>
#include <core/ecs/world.h>

#include <global/global.h>
#include <logger/client/client.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct TestContext{
    u32 passed = 0;
    u32 failed = 0;

    void checkTrue(const bool condition, const AStringView expression, const AStringView file, const i32 line){
        if(condition){
            ++passed;
            return;
        }

        ++failed;
        NWB_CERR << file << '(' << line << "): check failed: " << expression << '\n';
    }
};

class CapturingLogger final : public Log::IClient{
public:
    virtual void enqueue(TString&& str, Log::Type::Enum type = Log::Type::Info)override{
        record(str, type);
    }
    virtual void enqueue(const TString& str, Log::Type::Enum type = Log::Type::Info)override{
        record(str, type);
    }

    [[nodiscard]] u32 errorCount()const{ return m_errorCount; }
    [[nodiscard]] bool sawErrorContaining(const TStringView text)const{
        for(const TString& error : m_errors){
            if(error.find(text) != TString::npos)
                return true;
        }
        return false;
    }

private:
    void record(const TString& str, const Log::Type::Enum type){
        if(type == Log::Type::Error){
            ++m_errorCount;
            m_errors.push_back(str);
        }
    }

private:
    u32 m_errorCount = 0;
    Vector<TString> m_errors;
};

template<
    Core::Alloc::CustomArena::AllocFunc Alloc,
    Core::Alloc::CustomArena::FreeFunc Free,
    Core::Alloc::CustomArena::AllocAlignedFunc AllocAligned,
    Core::Alloc::CustomArena::FreeAlignedFunc FreeAligned>
struct EcsTestWorld{
    Core::Alloc::CustomArena arena;
    Core::Alloc::ThreadPool threadPool;
    Core::ECS::World world;

    EcsTestWorld()
        : arena(Alloc, Free, AllocAligned, FreeAligned)
        , threadPool(0)
        , world(arena, threadPool)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

