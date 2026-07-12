
#pragma once


#include "assert.h"
#include "basic_string.h"
#include "limit.h"
#include "text_utils.h"

#include <functional>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename CharT>
class BasicCompactString{
public:
    using value_type = CharT;
    using view_type = BasicStringView<value_type>;

    static constexpr usize s_StorageBytes = 256;
    static constexpr usize s_LengthBytes = sizeof(u8);
    static constexpr usize s_UsableBytes = s_StorageBytes - s_LengthBytes;
    static constexpr usize s_BufferLength = s_UsableBytes / sizeof(value_type);
    static constexpr usize s_BufferBytes = s_BufferLength * sizeof(value_type);
    static constexpr usize s_MaxLength = s_BufferLength - 1;
    static constexpr usize s_NPos = Limit<usize>::s_Max;

    static_assert(s_BufferLength > 0, "BasicCompactString requires storage for at least one null terminator");


public:
    constexpr BasicCompactString()
        : m_storage{}
        , m_size(0)
    {}

    BasicCompactString(const value_type* text)
        : m_storage{}
        , m_size(0)
    {
        if(!assign(text)){
            NWB_ASSERT_MSG(false, NWB_TEXT("BasicCompactString initialization exceeded capacity"));
            clear();
        }
    }
    explicit BasicCompactString(const view_type text)
        : m_storage{}
        , m_size(0)
    {
        if(!assign(text)){
            NWB_ASSERT_MSG(false, NWB_TEXT("BasicCompactString initialization exceeded capacity"));
            clear();
        }
    }
    template<typename ArenaT>
    explicit BasicCompactString(const BasicString<value_type, ArenaT>& text)
        : m_storage{}
        , m_size(0)
    {
        if(!assign(text)){
            NWB_ASSERT_MSG(false, NWB_TEXT("BasicCompactString initialization exceeded capacity"));
            clear();
        }
    }


public:
    [[nodiscard]] bool assign(const value_type* text){
        return text == nullptr
            ? clearAndReturn(true)
            : assign(view_type(text))
        ;
    }

    [[nodiscard]] bool assign(const view_type text){
        clear();

        const usize textSize = text.size();
        if(text.empty())
            return true;
        if(textSize > s_MaxLength)
            return false;

        for(usize i = 0; i < textSize; ++i){
            if(text[i] == value_type{}){
                clear();
                return false;
            }
            m_storage[i] = Canonicalize(text[i]);
        }
        m_size = static_cast<u8>(textSize);
        m_storage[m_size] = value_type{};
        return true;
    }

    template<typename ArenaT>
    [[nodiscard]] bool assign(const BasicString<value_type, ArenaT>& text){
        return assign(view_type(text.data(), text.size()));
    }

    [[nodiscard]] bool append(const value_type* text){
        return text == nullptr
            ? true
            : append(view_type(text))
        ;
    }

    [[nodiscard]] bool append(const view_type text){
        const usize textSize = text.size();
        if(text.empty())
            return true;
        if(textSize > remainingCapacity())
            return false;

        const u8 oldSize = m_size;
        for(usize i = 0; i < textSize; ++i){
            if(text[i] == value_type{}){
                m_storage[oldSize] = value_type{};
                return false;
            }
            m_storage[m_size + i] = Canonicalize(text[i]);
        }
        m_size = static_cast<u8>(m_size + textSize);
        m_storage[m_size] = value_type{};
        return true;
    }

    template<typename ArenaT>
    [[nodiscard]] bool append(const BasicString<value_type, ArenaT>& text){
        return append(view_type(text.data(), text.size()));
    }

    [[nodiscard]] bool append(const BasicCompactString& text){
        return append(text.view());
    }

    [[nodiscard]] bool pushBack(const value_type ch){
        if(ch == value_type{} || remainingCapacity() == 0)
            return false;

        m_storage[m_size] = Canonicalize(ch);
        ++m_size;
        m_storage[m_size] = value_type{};
        return true;
    }

    void clear(){
        m_size = 0;
        m_storage[0] = value_type{};
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

    [[nodiscard]] constexpr usize max_size()const{
        return capacity();
    }

    [[nodiscard]] usize remainingCapacity()const{
        return s_MaxLength - m_size;
    }

    [[nodiscard]] view_type view()const{
        return view_type(m_storage, m_size);
    }

    [[nodiscard]] operator view_type()const{
        return view();
    }

    [[nodiscard]] const value_type* c_str()const{
        return m_storage;
    }

    [[nodiscard]] const value_type* data()const{
        return m_storage;
    }

    [[nodiscard]] const value_type* cursor()const{
        return m_storage + m_size;
    }

    [[nodiscard]] BasicCompactString substr(const usize pos, const usize count = s_NPos)const{
        BasicCompactString result;
        if(pos >= m_size)
            return result;

        const usize remaining = m_size - pos;
        const usize copiedCount = count == s_NPos || count > remaining
            ? remaining
            : count
        ;

        NWB_MEMCPY(result.m_storage, copiedCount * sizeof(value_type), m_storage + pos, copiedCount * sizeof(value_type));
        result.m_size = static_cast<u8>(copiedCount);
        result.m_storage[result.m_size] = value_type{};
        return result;
    }

    BasicCompactString& operator+=(const value_type* text){
        if(!append(text)){
            NWB_ASSERT_MSG(false, NWB_TEXT("BasicCompactString append exceeded capacity"));
        }
        return *this;
    }

    BasicCompactString& operator+=(const view_type text){
        if(!append(text)){
            NWB_ASSERT_MSG(false, NWB_TEXT("BasicCompactString append exceeded capacity"));
        }
        return *this;
    }

    template<typename ArenaT>
    BasicCompactString& operator+=(const BasicString<value_type, ArenaT>& text){
        if(!append(text)){
            NWB_ASSERT_MSG(false, NWB_TEXT("BasicCompactString append exceeded capacity"));
        }
        return *this;
    }

    BasicCompactString& operator+=(const BasicCompactString& text){
        if(!append(text)){
            NWB_ASSERT_MSG(false, NWB_TEXT("BasicCompactString append exceeded capacity"));
        }
        return *this;
    }

    BasicCompactString& operator+=(const value_type ch){
        if(!pushBack(ch)){
            NWB_ASSERT_MSG(false, NWB_TEXT("BasicCompactString append exceeded capacity"));
        }
        return *this;
    }

    void push_back(const value_type ch){
        *this += ch;
    }


private:
    [[nodiscard]] bool clearAndReturn(const bool value){
        clear();
        return value;
    }


private:
    value_type m_storage[s_BufferLength];
    u8 m_size;
};

using ACompactString = BasicCompactString<char>;
using WCompactString = BasicCompactString<wchar>;

static_assert(sizeof(ACompactString) == ACompactString::s_StorageBytes, "ACompactString must stay 256 bytes");
static_assert(sizeof(WCompactString) == WCompactString::s_StorageBytes, "WCompactString must stay 256 bytes");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename CharT>
[[nodiscard]] inline bool operator==(const BasicCompactString<CharT>& lhs, const BasicCompactString<CharT>& rhs){
    return lhs.view() == rhs.view();
}
template<typename CharT>
[[nodiscard]] inline bool operator!=(const BasicCompactString<CharT>& lhs, const BasicCompactString<CharT>& rhs){
    return !(lhs == rhs);
}
template<typename CharT>
[[nodiscard]] inline bool operator<(const BasicCompactString<CharT>& lhs, const BasicCompactString<CharT>& rhs){
    return lhs.view() < rhs.view();
}

template<typename CharT>
[[nodiscard]] inline BasicCompactString<CharT> operator+(BasicCompactString<CharT> lhs, const CharT* rhs){
    lhs += rhs;
    return lhs;
}
template<typename CharT>
[[nodiscard]] inline BasicCompactString<CharT> operator+(BasicCompactString<CharT> lhs, const BasicStringView<CharT> rhs){
    lhs += rhs;
    return lhs;
}
template<typename CharT, typename ArenaT>
[[nodiscard]] inline BasicCompactString<CharT> operator+(BasicCompactString<CharT> lhs, const BasicString<CharT, ArenaT>& rhs){
    lhs += rhs;
    return lhs;
}
template<typename CharT>
[[nodiscard]] inline BasicCompactString<CharT> operator+(BasicCompactString<CharT> lhs, const BasicCompactString<CharT>& rhs){
    lhs += rhs;
    return lhs;
}
template<typename CharT>
[[nodiscard]] inline BasicCompactString<CharT> operator+(BasicCompactString<CharT> lhs, const CharT rhs){
    lhs += rhs;
    return lhs;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace std{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename CharT>
struct hash<BasicCompactString<CharT>>{
    usize operator()(const BasicCompactString<CharT>& value)const{
        return hash<BasicStringView<CharT>>{}(value.view());
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

