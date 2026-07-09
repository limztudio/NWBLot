// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ValueType{
    enum Enum : u8{
        Null = 0,
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
    explicit Value(MetaArena& arena);
    Value(i64 val, MetaArena& arena);
    Value(f64 val, MetaArena& arena);
    Value(MStringView val, MetaArena& arena);
    [[nodiscard]] static Value Reference(MStringView val, MetaArena& arena);
    ~Value();
    Value(const Value& other);
    Value(Value&& other)noexcept;
    Value& operator=(const Value& other);
    Value& operator=(Value&& other)noexcept;

    [[nodiscard]] Value operator+(const Value& rhs)const;
    [[nodiscard]] Value operator-(const Value& rhs)const;
    [[nodiscard]] Value operator*(const Value& rhs)const;
    [[nodiscard]] Value operator/(const Value& rhs)const;
    Value& operator+=(const Value& rhs);
    Value& operator-=(const Value& rhs);
    Value& operator*=(const Value& rhs);
    Value& operator/=(const Value& rhs);


public:
    [[nodiscard]] MetaArena& arena()const{ return m_arena; }
    [[nodiscard]] ValueType::Enum type()const{ return m_type; }
    [[nodiscard]] bool isNull()const{ return m_type == ValueType::Null; }
    [[nodiscard]] bool isInteger()const{ return m_type == ValueType::Integer; }
    [[nodiscard]] bool isDouble()const{ return m_type == ValueType::Double; }
    [[nodiscard]] bool isString()const{ return m_type == ValueType::String; }
    [[nodiscard]] bool isReference()const{ return m_type == ValueType::Reference; }
    [[nodiscard]] bool isList()const{ return m_type == ValueType::List; }
    [[nodiscard]] bool isMap()const{ return m_type == ValueType::Map; }
    [[nodiscard]] bool isNumeric()const{ return m_type == ValueType::Integer || m_type == ValueType::Double; }

    [[nodiscard]] i64 asInteger()const;
    [[nodiscard]] f64 asDouble()const;
    [[nodiscard]] f64 toDouble()const;
    [[nodiscard]] MStringView asString()const;
    [[nodiscard]] MStringView asReference()const;
    [[nodiscard]] const ListType& asList()const;
    [[nodiscard]] ListType& asList();
    [[nodiscard]] const MapType& asMap()const;
    [[nodiscard]] MapType& asMap();

    void setInteger(i64 val);
    void setDouble(f64 val);
    void setString(MStringView val);
    void setReference(MStringView val);
    void makeList();
    void makeMap();

    Value& field(MStringView name);
    [[nodiscard]] const Value* findField(MStringView name)const;

    void append(Value&& val);

    [[nodiscard]] MString copyString()const;

    template<typename Container>
    [[nodiscard]] bool copyStringList(Container& outList)const{
        if(!isList())
            return false;
        const auto& list = asList();
        for(const auto& elem : list){
            if(!elem.isString())
                return false;
        }

        constexpr bool canAppendString =
            requires(Container& c, usize n){ c.reserve(n); }
            && requires(Container& c, const MChar* data, usize size){ c.emplace_back(data, size); }
        ;
        constexpr bool canAppendArenaString =
            requires(Container& c, usize n){ c.reserve(n); }
            && requires(Container& c){ c.get_allocator().arena(); }
            && requires(
                Container& c,
                const MChar* data,
                usize size,
                typename Container::value_type::allocator_type allocator
            ){
                c.emplace_back(data, size, allocator);
            }
        ;
        const usize listOffset = outList.size();
        if constexpr(canAppendArenaString){
            auto& arena = outList.get_allocator().arena();
            outList.reserve(listOffset + list.size());
            for(const auto& elem : list){
                const MStringView text = elem.asString();
                outList.emplace_back(text.data(), text.size(), arena);
            }
        }
        else if constexpr(canAppendString){
            outList.reserve(listOffset + list.size());
            for(const auto& elem : list){
                const MStringView text = elem.asString();
                outList.emplace_back(text.data(), text.size());
            }
        }
        else{
            outList.resize(listOffset + list.size());
            for(usize i = 0u; i < list.size(); ++i){
                const auto& elem = list[i];
                const MStringView text = elem.asString();
                outList[listOffset + i].assign(text.data(), text.size());
            }
        }
        return true;
    }


private:
    void destroy();
    void copyFrom(const Value& other);
    void moveFrom(Value&& other)noexcept;
    Value& appendListCopy(const Value& val);
    void appendListCopies(const ListType& values, usize count);
    [[nodiscard]] usize valueIndexInList(const Value& val)const;

    [[nodiscard]] StringType makeArenaString(MStringView sv)const;
    [[nodiscard]] ListType* allocList()const;
    [[nodiscard]] MapType* allocMap()const;
    void freeList(ListType* p);
    void freeMap(MapType* p);


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


[[nodiscard]] inline const Value* FindField(const Value& map, const AStringView fieldName){
    return map.findField(MStringView(fieldName.data(), fieldName.size()));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

