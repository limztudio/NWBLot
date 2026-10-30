// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

#include <global/span.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ValueType{
    static constexpr u8 s_ValueTypeNullBase = 0;
    enum Enum : u8{
        Null = s_ValueTypeNullBase,
        Integer,
        Double,
        String,
        Reference,
        List,
        Map,
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Value final{
public:
    using StringType = MString;
    using ListType = MVector<Value>;
    using MapType = MStringMap<Value>;


private:
    template<typename Container>
    static constexpr b8 s_CanAppendString = requires(Container& c, usize n){ c.reserve(n); } && requires(Container& c, const MStringView text){ c.emplace_back(text); };
    template<typename Container>
    static constexpr b8 s_CanAppendArenaString = requires(Container& c, usize n){ c.reserve(n); }
        && requires(Container& c){ c.get_allocator().arena(); }
        && requires(Container& c, const MStringView text, typename Container::value_type::allocator_type allocator){
            c.emplace_back(text, allocator);
        }
    ;


public:
    [[nodiscard]] static Value Reference(MStringView val, MetaArena& arena);


public:
    explicit Value(MetaArena& arena)noexcept;
    Value(i64 val, MetaArena& arena)noexcept;
    Value(f64 val, MetaArena& arena)noexcept;
    Value(MStringView val, MetaArena& arena);
    Value(const Value& other);
    Value(Value&& other)noexcept;
    ~Value()noexcept;


public:
    Value& operator=(const Value& other);
    Value& operator=(Value&& other);

    [[nodiscard]] Value operator+(const Value& rhs)const;
    [[nodiscard]] Value operator-(const Value& rhs)const noexcept;
    [[nodiscard]] Value operator*(const Value& rhs)const noexcept;
    [[nodiscard]] Value operator/(const Value& rhs)const noexcept;
    Value& operator+=(const Value& rhs);
    Value& operator-=(const Value& rhs)noexcept;
    Value& operator*=(const Value& rhs)noexcept;
    Value& operator/=(const Value& rhs)noexcept;


public:
    [[nodiscard]] MetaArena& arena()const noexcept{ return m_arena; }

    [[nodiscard]] ValueType::Enum type()const noexcept{ return m_type; }

    [[nodiscard]] b8 isNull()const noexcept{ return m_type == ValueType::Null; }

    [[nodiscard]] b8 isInteger()const noexcept{ return m_type == ValueType::Integer; }

    [[nodiscard]] b8 isDouble()const noexcept{ return m_type == ValueType::Double; }

    [[nodiscard]] b8 isString()const noexcept{ return m_type == ValueType::String; }

    [[nodiscard]] b8 isReference()const noexcept{ return m_type == ValueType::Reference; }

    [[nodiscard]] b8 isList()const noexcept{ return m_type == ValueType::List; }

    [[nodiscard]] b8 isMap()const noexcept{ return m_type == ValueType::Map; }

    [[nodiscard]] b8 isNumeric()const noexcept{ return m_type == ValueType::Integer || m_type == ValueType::Double; }

    [[nodiscard]] i64 asInteger()const noexcept;
    [[nodiscard]] f64 asDouble()const noexcept;
    [[nodiscard]] f64 toDouble()const noexcept;
    [[nodiscard]] MStringView asString()const noexcept;
    [[nodiscard]] MStringView asReference()const noexcept;
    [[nodiscard]] const ListType& asList()const noexcept;
    [[nodiscard]] ListType& asList()noexcept;
    [[nodiscard]] const MapType& asMap()const noexcept;
    [[nodiscard]] MapType& asMap()noexcept;

    void setInteger(i64 val)noexcept;
    void setDouble(f64 val)noexcept;
    void setString(MStringView val){ setStringOrReference(val, ValueType::String); }
    void setReference(MStringView val){ setStringOrReference(val, ValueType::Reference); }
    void makeList();
    void makeMap();

    Value& field(MStringView name);
    [[nodiscard]] const Value* findField(MStringView name)const noexcept;

    void append(Value&& val);

    template<typename Container>
    [[nodiscard]] b8 copyStringList(Container& outList)const noexcept([]()consteval noexcept{
        using SourceIterator = typename ListType::const_iterator;
        constexpr b8 s_ReadSourceNoThrow = noexcept(DeclVal<const Value&>().isList())
            && noexcept(DeclVal<const Value&>().asList())
            && noexcept(DeclVal<const ListType&>().begin())
            && noexcept(DeclVal<const ListType&>().end())
            && IsNothrowDestructible_V<SourceIterator>
            && noexcept(static_cast<b8>(DeclVal<SourceIterator&>() != DeclVal<SourceIterator&>()))
            && noexcept(++DeclVal<SourceIterator&>())
            && noexcept(*DeclVal<SourceIterator&>())
            && noexcept(DeclVal<const Value&>().isString())
            && noexcept(DeclVal<const Value&>().asString())
            && IsNothrowDestructible_V<MStringView>
        ;
        constexpr b8 s_ReadOffsetNoThrow = noexcept(DeclVal<void(*)(usize)noexcept>()(DeclVal<Container&>().size()));
        if constexpr(s_CanAppendArenaString<Container>){
            using ArenaReference = decltype(DeclVal<Container&>().get_allocator().arena());
            return
                s_ReadSourceNoThrow && s_ReadOffsetNoThrow
                && noexcept(DeclVal<Container&>().get_allocator().arena())
                && noexcept(DeclVal<Container&>().reserve(DeclVal<const usize&>() + DeclVal<const ListType&>().size()))
                && noexcept(DeclVal<Container&>().emplace_back(DeclVal<const MStringView&>(), DeclVal<ArenaReference>()))
            ;
        }
        else if constexpr(s_CanAppendString<Container>){
            return s_ReadSourceNoThrow && s_ReadOffsetNoThrow && noexcept(DeclVal<Container&>().reserve(DeclVal<const usize&>() + DeclVal<const ListType&>().size())) && noexcept(DeclVal<Container&>().emplace_back(DeclVal<const MStringView&>()));
        }
        else{
            return
                s_ReadSourceNoThrow && s_ReadOffsetNoThrow
                && noexcept(DeclVal<Container&>().resize(DeclVal<const usize&>() + DeclVal<const ListType&>().size()))
                && noexcept(DeclVal<Container&>()[DeclVal<const usize&>() + DeclVal<usize&>()].assign(DeclVal<const MStringView&>()))
            ;
        }
    }()){
        if(!isList())
            return false;
        const auto& list = asList();
        for(const auto& elem : list){
            if(!elem.isString())
                return false;
        }

        const usize listOffset = outList.size();
        if constexpr(s_CanAppendArenaString<Container>){
            auto& arena = outList.get_allocator().arena();
            outList.reserve(listOffset + list.size());
            for(const auto& elem : list){
                const MStringView text = elem.asString();
                outList.emplace_back(text, arena);
            }
        }
        else if constexpr(s_CanAppendString<Container>){
            outList.reserve(listOffset + list.size());
            for(const auto& elem : list){
                const MStringView text = elem.asString();
                outList.emplace_back(text);
            }
        }
        else{
            outList.resize(listOffset + list.size());
            for(usize i = 0u; i < list.size(); ++i){
                const auto& elem = list[i];
                const MStringView text = elem.asString();
                outList[listOffset + i].assign(text);
            }
        }
        return true;
    }


private:
    void setStringOrReference(MStringView val, ValueType::Enum type);
    void destroy()noexcept;
    void copyFrom(const Value& other);
    void moveFrom(Value&& other)noexcept;
    void appendListCopy(const Value& val);
    void appendListCopies(Span<const Value> values, usize count);

    [[nodiscard]] NotNull<ListType*> allocList()const;
    [[nodiscard]] NotNull<MapType*> allocMap()const;


private:
    MetaArena& m_arena;
    ValueType::Enum m_type = ValueType::Null;

    union{
        i64 m_integer;
        f64 m_double;
        StringType* m_string;
        ListType* m_list;
        MapType* m_map;
    } m_data{};
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

