// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"


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


class Value{
public:
    using StringType = MString;
    using ListType = MVector<Value>;
    using MapType = MStringMap<Value>;


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
    [[nodiscard]] bool isNull()const noexcept{ return m_type == ValueType::Null; }
    [[nodiscard]] bool isInteger()const noexcept{ return m_type == ValueType::Integer; }
    [[nodiscard]] bool isDouble()const noexcept{ return m_type == ValueType::Double; }
    [[nodiscard]] bool isString()const noexcept{ return m_type == ValueType::String; }
    [[nodiscard]] bool isReference()const noexcept{ return m_type == ValueType::Reference; }
    [[nodiscard]] bool isList()const noexcept{ return m_type == ValueType::List; }
    [[nodiscard]] bool isMap()const noexcept{ return m_type == ValueType::Map; }
    [[nodiscard]] bool isNumeric()const noexcept{ return m_type == ValueType::Integer || m_type == ValueType::Double; }

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
    void setString(MStringView val);
    void setReference(MStringView val);
    void makeList();
    void makeMap();

    Value& field(MStringView name);
    [[nodiscard]] const Value* findField(MStringView name)const noexcept;

    void append(Value&& val);

    [[nodiscard]] MString copyString()const;

    template<typename Container>
    [[nodiscard]] bool copyStringList(Container& outList)const noexcept([]()consteval noexcept{
        constexpr bool s_CanAppendString = requires(Container& c, usize n){ c.reserve(n); }
            && requires(Container& c, const MStringView text){ c.emplace_back(text); }
        ;
        constexpr bool s_CanAppendArenaString = requires(Container& c, usize n){ c.reserve(n); }
            && requires(Container& c){ c.get_allocator().arena(); }
            && requires(Container& c, const MStringView text, typename Container::value_type::allocator_type allocator){
                c.emplace_back(text, allocator);
            }
        ;
        using SourceIterator = typename ListType::const_iterator;
        constexpr bool s_ReadSourceNoThrow = noexcept(DeclVal<const Value&>().isList())
            && noexcept(DeclVal<const Value&>().asList())
            && noexcept(DeclVal<const ListType&>().begin())
            && noexcept(DeclVal<const ListType&>().end())
            && IsNothrowDestructible_V<SourceIterator>
            && noexcept(static_cast<bool>(DeclVal<SourceIterator&>() != DeclVal<SourceIterator&>()))
            && noexcept(++DeclVal<SourceIterator&>())
            && noexcept(*DeclVal<SourceIterator&>())
            && noexcept(DeclVal<const Value&>().isString())
            && noexcept(DeclVal<const Value&>().asString())
            && IsNothrowDestructible_V<MStringView>
        ;
        constexpr bool s_ReadOffsetNoThrow = noexcept(DeclVal<void(*)(usize)noexcept>()(DeclVal<Container&>().size()));
        if constexpr(s_CanAppendArenaString){
            using ArenaReference = decltype(DeclVal<Container&>().get_allocator().arena());
            return
                s_ReadSourceNoThrow && s_ReadOffsetNoThrow
                && noexcept(DeclVal<Container&>().get_allocator().arena())
                && noexcept(DeclVal<Container&>().reserve(DeclVal<const usize&>() + DeclVal<const ListType&>().size()))
                && noexcept(DeclVal<Container&>().emplace_back(DeclVal<const MStringView&>(), DeclVal<ArenaReference>()))
            ;
        }
        else if constexpr(s_CanAppendString){
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

        constexpr bool s_CanAppendString = requires(Container& c, usize n){ c.reserve(n); }
            && requires(Container& c, const MStringView text){ c.emplace_back(text); }
        ;
        constexpr bool s_CanAppendArenaString = requires(Container& c, usize n){ c.reserve(n); }
            && requires(Container& c){ c.get_allocator().arena(); }
            && requires(Container& c, const MStringView text, typename Container::value_type::allocator_type allocator){
                c.emplace_back(text, allocator);
            }
        ;
        const usize listOffset = outList.size();
        if constexpr(s_CanAppendArenaString){
            auto& arena = outList.get_allocator().arena();
            outList.reserve(listOffset + list.size());
            for(const auto& elem : list){
                const MStringView text = elem.asString();
                outList.emplace_back(text, arena);
            }
        }
        else if constexpr(s_CanAppendString){
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
    void destroy()noexcept;
    void copyFrom(const Value& other);
    void moveFrom(Value&& other)noexcept;
    void appendListCopy(const Value& val);
    void appendListCopies(const ListType& values, usize count);

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


[[nodiscard]] inline const Value* FindField(const Value& map, const AStringView fieldName)noexcept{
    return map.findField(MStringView(fieldName.data(), fieldName.size()));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

