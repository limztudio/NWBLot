// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "name.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_name{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct NameHashEqual{
    bool operator()(const NameHash& a, const NameHash& b)const{
        return
            a.qwords[0] == b.qwords[0]
            && a.qwords[1] == b.qwords[1]
            && a.qwords[2] == b.qwords[2]
            && a.qwords[3] == b.qwords[3]
            && a.qwords[4] == b.qwords[4]
            && a.qwords[5] == b.qwords[5]
            && a.qwords[6] == b.qwords[6]
            && a.qwords[7] == b.qwords[7]
            ;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class NamePool{
public:
    static NamePool& instance(){
        static NamePool pool;
        return pool;
    }

private:
    NamePool() = default;
    ~NamePool() = default;

    NamePool(const NamePool&) = delete;
    NamePool& operator=(const NamePool&) = delete;

public:
    // Registers a name and stores its original string for reverse lookup.
    // Returns the Name. If a name with the same hash already exists, the original entry is kept.
    Name store(const char* str){
        Name name(str);
        m_entries.insert({ name.hash(), AString(str) });
        return name;
    }

    // Looks up the stored string for a given Name.
    // Returns the original string if found, or an empty string otherwise.
    [[nodiscard]] const char* find(const Name& name)const{
        auto it = m_entries.find(name.hash());
        if(it != m_entries.end())
            return it->second.c_str();
        return "";
    }

    // Checks if a Name is registered in the pool.
    [[nodiscard]] bool contains(const Name& name)const{
        return m_entries.find(name.hash()) != m_entries.end();
    }

    // Returns the number of registered names.
    [[nodiscard]] usize size()const{
        return m_entries.size();
    }

private:
    ParallelHashMap<NameHash, AString, Hasher<NameHash>, __hidden_name::NameHashEqual> m_entries;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

