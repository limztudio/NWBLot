// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "assert.h"
#include "basic_string.h"
#include "limit.h"
#include "text_utils.h"

#include <functional>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class CompactString{
public:
    static constexpr usize s_StorageBytes = 256;
    static constexpr usize s_LengthBytes = sizeof(u8);
    static constexpr usize s_BufferBytes = s_StorageBytes - s_LengthBytes;
    static constexpr usize s_MaxLength = s_BufferBytes - 1;
    static constexpr usize s_NPos = Limit<usize>::s_Max;


public:
    constexpr CompactString()
        : m_storage{}
        , m_size(0)
    {}

    CompactString(const char* text)
        : m_storage{}
        , m_size(0)
    {
        const bool assigned = assign(text);
        NWB_ASSERT_MSG(assigned, NWB_TEXT("CompactString initialization exceeded capacity"));
        static_cast<void>(assigned);
    }
    explicit CompactString(const AStringView text)
        : m_storage{}
        , m_size(0)
    {
        const bool assigned = assign(text);
        NWB_ASSERT_MSG(assigned, NWB_TEXT("CompactString initialization exceeded capacity"));
        static_cast<void>(assigned);
    }
    explicit CompactString(const AString& text)
        : m_storage{}
        , m_size(0)
    {
        const bool assigned = assign(text);
        NWB_ASSERT_MSG(assigned, NWB_TEXT("CompactString initialization exceeded capacity"));
        static_cast<void>(assigned);
    }


public:
    [[nodiscard]] bool assign(const char* text){
        return
            text == nullptr
                ? clearAndReturn(true)
                : assign(AStringView(text))
        ;
    }

    [[nodiscard]] bool assign(const AStringView text){
        clear();

        const usize textSize = text.size();
        if(text.empty())
            return true;
        if(textSize > s_MaxLength)
            return false;

        for(usize i = 0; i < textSize; ++i){
            if(text[i] == '\0'){
                clear();
                return false;
            }
            m_storage[i] = Canonicalize(text[i]);
        }
        m_size = static_cast<u8>(textSize);
        m_storage[m_size] = '\0';
        return true;
    }

    [[nodiscard]] bool assign(const AString& text){
        return assign(AStringView(text));
    }

    [[nodiscard]] bool append(const char* text){
        return
            text == nullptr
                ? true
                : append(AStringView(text))
        ;
    }

    [[nodiscard]] bool append(const AStringView text){
        const usize textSize = text.size();
        if(text.empty())
            return true;
        if(textSize > remainingCapacity())
            return false;

        const u8 oldSize = m_size;
        for(usize i = 0; i < textSize; ++i){
            if(text[i] == '\0'){
                m_storage[oldSize] = '\0';
                return false;
            }
            m_storage[m_size + i] = Canonicalize(text[i]);
        }
        m_size = static_cast<u8>(m_size + textSize);
        m_storage[m_size] = '\0';
        return true;
    }

    [[nodiscard]] bool append(const AString& text){
        return append(AStringView(text));
    }

    [[nodiscard]] bool append(const CompactString& text){
        return append(text.view());
    }

    [[nodiscard]] bool pushBack(const char ch){
        if(ch == '\0' || remainingCapacity() == 0)
            return false;

        m_storage[m_size] = Canonicalize(ch);
        ++m_size;
        m_storage[m_size] = '\0';
        return true;
    }

    void clear(){
        m_size = 0;
        m_storage[0] = '\0';
    }


public:
    [[nodiscard]] bool empty()const{
        return m_size == 0;
    }

    [[nodiscard]] explicit operator bool()const{
        return !empty();
    }

    [[nodiscard]] usize size()const{
        return m_size;
    }

    [[nodiscard]] constexpr usize capacity()const{
        return s_MaxLength;
    }

    [[nodiscard]] usize remainingCapacity()const{
        return s_MaxLength - m_size;
    }

    [[nodiscard]] AStringView view()const{
        return AStringView(m_storage, m_size);
    }

    [[nodiscard]] const char* c_str()const{
        return m_storage;
    }

    [[nodiscard]] const char* data()const{
        return m_storage;
    }

    [[nodiscard]] const char* cursor()const{
        return m_storage + m_size;
    }

    [[nodiscard]] CompactString substr(const usize pos, const usize count = s_NPos)const{
        CompactString result;
        if(pos >= m_size)
            return result;

        const usize remaining = m_size - pos;
        const usize copiedCount = count == s_NPos || count > remaining
            ? remaining
            : count
        ;

        NWB_MEMCPY(result.m_storage, copiedCount, m_storage + pos, copiedCount);
        result.m_size = static_cast<u8>(copiedCount);
        result.m_storage[result.m_size] = '\0';
        return result;
    }

    CompactString& operator+=(const char* text){
        const bool appended = append(text);
        NWB_ASSERT_MSG(appended, NWB_TEXT("CompactString append exceeded capacity"));
        static_cast<void>(appended);
        return *this;
    }

    CompactString& operator+=(const AStringView text){
        const bool appended = append(text);
        NWB_ASSERT_MSG(appended, NWB_TEXT("CompactString append exceeded capacity"));
        static_cast<void>(appended);
        return *this;
    }

    CompactString& operator+=(const AString& text){
        const bool appended = append(text);
        NWB_ASSERT_MSG(appended, NWB_TEXT("CompactString append exceeded capacity"));
        static_cast<void>(appended);
        return *this;
    }

    CompactString& operator+=(const CompactString& text){
        const bool appended = append(text);
        NWB_ASSERT_MSG(appended, NWB_TEXT("CompactString append exceeded capacity"));
        static_cast<void>(appended);
        return *this;
    }

    CompactString& operator+=(const char ch){
        const bool appended = pushBack(ch);
        NWB_ASSERT_MSG(appended, NWB_TEXT("CompactString append exceeded capacity"));
        static_cast<void>(appended);
        return *this;
    }


private:
    [[nodiscard]] bool clearAndReturn(const bool value){
        clear();
        return value;
    }


private:
    char m_storage[s_BufferBytes];
    u8 m_size;
};


static_assert(sizeof(CompactString) == CompactString::s_StorageBytes, "CompactString must stay 256 bytes");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool operator==(const CompactString& lhs, const CompactString& rhs){
    return lhs.view() == rhs.view();
}
[[nodiscard]] inline bool operator!=(const CompactString& lhs, const CompactString& rhs){
    return !(lhs == rhs);
}
[[nodiscard]] inline bool operator<(const CompactString& lhs, const CompactString& rhs){
    return lhs.view() < rhs.view();
}

[[nodiscard]] inline CompactString operator+(CompactString lhs, const char* rhs){
    lhs += rhs;
    return lhs;
}
[[nodiscard]] inline CompactString operator+(CompactString lhs, const AStringView rhs){
    lhs += rhs;
    return lhs;
}
[[nodiscard]] inline CompactString operator+(CompactString lhs, const AString& rhs){
    lhs += rhs;
    return lhs;
}
[[nodiscard]] inline CompactString operator+(CompactString lhs, const CompactString& rhs){
    lhs += rhs;
    return lhs;
}
[[nodiscard]] inline CompactString operator+(CompactString lhs, const char rhs){
    lhs += rhs;
    return lhs;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace std{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<>
struct hash<CompactString>{
    usize operator()(const CompactString& value)const{
        return hash<AStringView>{}(value.view());
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

