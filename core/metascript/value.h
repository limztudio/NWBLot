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
    explicit Value(Alloc::CustomArena& arena);
    Value(i64 val, Alloc::CustomArena& arena);
    Value(f64 val, Alloc::CustomArena& arena);
    Value(MStringView val, Alloc::CustomArena& arena);
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
    [[nodiscard]] Alloc::CustomArena& arena()const{ return m_arena; }
    [[nodiscard]] ValueType::Enum type()const{ return m_type; }
    [[nodiscard]] bool isNull()const{ return m_type == ValueType::Null; }
    [[nodiscard]] bool isInteger()const{ return m_type == ValueType::Integer; }
    [[nodiscard]] bool isDouble()const{ return m_type == ValueType::Double; }
    [[nodiscard]] bool isString()const{ return m_type == ValueType::String; }
    [[nodiscard]] bool isList()const{ return m_type == ValueType::List; }
    [[nodiscard]] bool isMap()const{ return m_type == ValueType::Map; }
    [[nodiscard]] bool isNumeric()const{ return m_type == ValueType::Integer || m_type == ValueType::Double; }

    [[nodiscard]] i64 asInteger()const;
    [[nodiscard]] f64 asDouble()const;
    [[nodiscard]] f64 toDouble()const;
    [[nodiscard]] MStringView asString()const;
    [[nodiscard]] const ListType& asList()const;
    [[nodiscard]] ListType& asList();
    [[nodiscard]] const MapType& asMap()const;
    [[nodiscard]] MapType& asMap();

    void setInteger(i64 val);
    void setDouble(f64 val);
    void setString(MStringView val);
    void makeList();
    void makeMap();

    Value& field(MStringView name);
    [[nodiscard]] const Value* findField(MStringView name)const;

    void append(Value&& val);

    [[nodiscard]] AString copyString()const;

    template<typename Container>
    [[nodiscard]] bool copyStringList(Container& outList)const{
        if(!isList())
            return false;
        const auto& list = asList();
        outList.reserve(list.size());
        for(const auto& elem : list){
            if(!elem.isString())
                return false;
            outList.push_back(elem.copyString());
        }
        return true;
    }


private:
    void destroy();
    void copyFrom(const Value& other);
    void moveFrom(Value&& other)noexcept;

    [[nodiscard]] StringType makeArenaString(MStringView sv)const;
    [[nodiscard]] ListType* allocList()const;
    [[nodiscard]] MapType* allocMap()const;
    void freeList(ListType* p);
    void freeMap(MapType* p);


private:
    Alloc::CustomArena& m_arena;
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

