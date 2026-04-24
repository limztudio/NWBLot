// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "name.h"
#include "containers.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class NamePool : NoCopy{
private:
    using NameIndexMap = HashMap<NameHash, usize>;

    [[nodiscard]] bool resolveExistingNameLocked(const Name& name, const AStringView text, Name& outName)const{
        outName = NAME_NONE;

        const auto itr = m_nameIndex.find(name.hash());
        if(itr == m_nameIndex.end())
            return false;

        const usize index = itr.value();
        if(index >= m_strings.size())
            return true;

        const AString& canonical = m_strings[index];
        if(canonical.size() == text.size()){
            bool matches = true;
            for(usize i = 0; i < text.size(); ++i){
                if(canonical[i] == Canonicalize(text[i]))
                    continue;

                matches = false;
                break;
            }

            if(matches){
                outName = name;
                return true;
            }
        }

        NWB_ASSERT_MSG(false, "Name hash collision detected in NamePool::store");
        return true;
    }


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
        if(HasEmbeddedNull(str))
            return NAME_NONE;

        const Name name(str);
        Name existingName = NAME_NONE;
        {
            ScopedLock lock(m_mutex);
            if(resolveExistingNameLocked(name, str, existingName))
                return existingName;
        }

        AString canonical = CanonicalizeText(str);
        ScopedLock lock(m_mutex);

        if(resolveExistingNameLocked(name, str, existingName))
            return existingName;

        const usize newIndex = m_strings.size();
        m_strings.push_back(Move(canonical));
        m_nameIndex.emplace(name.hash(), newIndex);
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

        const usize index = itr.value();
        if(index >= m_strings.size())
            return "";
        return m_strings[index].c_str();
    }
    [[nodiscard]] bool find(const Name& name, AString& outCanonical)const{
        ScopedLock lock(m_mutex);

        const auto itr = m_nameIndex.find(name.hash());
        if(itr == m_nameIndex.end())
            return false;

        const usize index = itr.value();
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

