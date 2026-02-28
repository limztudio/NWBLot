// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "name.h"
#include "containers.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_name_pool{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline AString CanonicalString(AStringView str){
    AString output;
    output.reserve(str.size());
    for(const char ch : str)
        output.push_back(__hidden_name::Canonicalize(ch));
    return output;
}

inline bool HasEmbeddedNull(AStringView str){
    for(const char ch : str){
        if(ch == '\0')
            return true;
    }
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class NamePool : NoCopy{
private:
    using NameIndexMap = HashMap<NameHash, usize>;


public:
    static NamePool& instance(){
        static NamePool pool;
        return pool;
    }


private:
    NamePool() = default;
    ~NamePool() = default;


public:
    [[nodiscard]] Name store(AStringView str){
        if(str.empty())
            return NAME_NONE;
        if(__hidden_name_pool::HasEmbeddedNull(str))
            return NAME_NONE;

        const AString canonical = __hidden_name_pool::CanonicalString(str);
        const Name name(canonical.c_str());

        ScopedLock lock(m_mutex);

        const auto itr = m_nameIndex.find(name.hash());
        if(itr != m_nameIndex.end()){
            const usize index = itr->second;
            if(index >= m_strings.size())
                return NAME_NONE;
            if(m_strings[index] == canonical)
                return name;

            NWB_ASSERT_MSG(false, "Name hash collision detected in NamePool::store");
            return NAME_NONE;
        }

        const usize newIndex = m_strings.size();
        m_strings.push_back(canonical);
        m_nameIndex.insert({ name.hash(), newIndex });
        return name;
    }
    [[nodiscard]] Name store(const char* str){
        if(str == nullptr)
            return NAME_NONE;
        return store(AStringView(str));
    }

    [[nodiscard]] const char* find(const Name& name)const{
        ScopedLock lock(m_mutex);

        const auto itr = m_nameIndex.find(name.hash());
        if(itr == m_nameIndex.end())
            return "";

        const usize index = itr->second;
        if(index >= m_strings.size())
            return "";
        return m_strings[index].c_str();
    }
    [[nodiscard]] bool find(const Name& name, AString& outCanonical)const{
        ScopedLock lock(m_mutex);

        const auto itr = m_nameIndex.find(name.hash());
        if(itr == m_nameIndex.end())
            return false;

        const usize index = itr->second;
        if(index >= m_strings.size())
            return false;

        outCanonical = m_strings[index];
        return true;
    }

    [[nodiscard]] bool contains(const Name& name)const{
        ScopedLock lock(m_mutex);
        return m_nameIndex.find(name.hash()) != m_nameIndex.end();
    }

    [[nodiscard]] usize size()const{
        ScopedLock lock(m_mutex);
        return m_nameIndex.size();
    }


private:
    mutable Futex m_mutex;

    NameIndexMap m_nameIndex;
    Deque<AString> m_strings;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

