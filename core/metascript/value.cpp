// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "value.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool AddI64Overflows(const i64 lhs, const i64 rhs){
    if(rhs > 0)
        return lhs > Limit<i64>::s_Max - rhs;
    if(rhs < 0)
        return lhs < Limit<i64>::s_Min - rhs;
    return false;
}

[[nodiscard]] bool SubtractI64Overflows(const i64 lhs, const i64 rhs){
    if(rhs > 0)
        return lhs < Limit<i64>::s_Min + rhs;
    if(rhs < 0)
        return lhs > Limit<i64>::s_Max + rhs;
    return false;
}

[[nodiscard]] bool MultiplyI64Overflows(const i64 lhs, const i64 rhs){
    if(lhs == 0 || rhs == 0)
        return false;

    if(lhs > 0){
        if(rhs > 0)
            return lhs > Limit<i64>::s_Max / rhs;
        return rhs < Limit<i64>::s_Min / lhs;
    }

    if(rhs > 0)
        return lhs < Limit<i64>::s_Min / rhs;

    return rhs < Limit<i64>::s_Max / lhs;
}

[[nodiscard]] bool DivideI64Overflows(const i64 lhs, const i64 rhs){
    return lhs == Limit<i64>::s_Min && rhs == -1;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Value::Value(Alloc::CustomArena& arena)
    : m_arena(arena)
{}

Value::Value(i64 val, Alloc::CustomArena& arena)
    : m_arena(arena)
    , m_type(ValueType::Integer)
{
    m_data.m_integer = val;
}

Value::Value(f64 val, Alloc::CustomArena& arena)
    : m_arena(arena)
    , m_type(ValueType::Double)
{
    m_data.m_double = val;
}

Value::Value(MStringView val, Alloc::CustomArena& arena)
    : m_arena(arena)
    , m_type(ValueType::String)
{
    m_data.m_string = NewArenaObject<StringType>(m_arena, val.data(), val.size(), MAllocator<MChar>(m_arena));
}

Value::~Value(){
    destroy();
}

Value::Value(const Value& other)
    : m_arena(other.m_arena)
{
    copyFrom(other);
}

Value::Value(Value&& other)noexcept
    : m_arena(other.m_arena)
{
    moveFrom(Move(other));
}

Value& Value::operator=(const Value& other){
    if(this != &other){
        destroy();
        copyFrom(other);
    }
    return *this;
}

Value& Value::operator=(Value&& other)noexcept{
    if(this != &other){
        destroy();
        moveFrom(Move(other));
    }
    return *this;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Value Value::operator+(const Value& rhs)const{
    if(m_type == ValueType::Integer && rhs.m_type == ValueType::Integer){
        if(AddI64Overflows(m_data.m_integer, rhs.m_data.m_integer)){
            NWB_ASSERT_MSG(false, NWB_TEXT("integer overflow"));
            return Value(m_arena);
        }
        return Value(m_data.m_integer + rhs.m_data.m_integer, m_arena);
    }

    if(isNumeric() && rhs.isNumeric())
        return Value(toDouble() + rhs.toDouble(), m_arena);

    if(m_type == ValueType::String && rhs.m_type == ValueType::String){
        const auto lsv = asString();
        const auto rsv = rhs.asString();
        if(lsv.size() > Limit<usize>::s_Max - rsv.size()){
            NWB_ASSERT_MSG(false, NWB_TEXT("string concatenation size overflow"));
            return Value(m_arena);
        }

        StringType result{MAllocator<MChar>(m_arena)};
        result.reserve(lsv.size() + rsv.size());
        result.append(lsv.data(), lsv.size());
        result.append(rsv.data(), rsv.size());

        Value v(m_arena);
        v.m_type = ValueType::String;
        v.m_data.m_string = NewArenaObject<StringType>(m_arena, Move(result));
        return v;
    }

    if(m_type == ValueType::List && rhs.m_type == ValueType::List){
        Value v(m_arena);
        v.makeList();
        auto& dst = *v.m_data.m_list;
        if(m_data.m_list->size() > Limit<usize>::s_Max - rhs.m_data.m_list->size()){
            NWB_ASSERT_MSG(false, NWB_TEXT("list concatenation size overflow"));
            return Value(m_arena);
        }
        dst.reserve(m_data.m_list->size() + rhs.m_data.m_list->size());
        for(const auto& elem : *m_data.m_list)
            dst.push_back(elem);
        for(const auto& elem : *rhs.m_data.m_list)
            dst.push_back(elem);
        return v;
    }

    NWB_ASSERT_MSG(false, NWB_TEXT("invalid operand types for operator+"));
    return Value(m_arena);
}

Value Value::operator-(const Value& rhs)const{
    if(m_type == ValueType::Integer && rhs.m_type == ValueType::Integer){
        if(SubtractI64Overflows(m_data.m_integer, rhs.m_data.m_integer)){
            NWB_ASSERT_MSG(false, NWB_TEXT("integer overflow"));
            return Value(m_arena);
        }
        return Value(m_data.m_integer - rhs.m_data.m_integer, m_arena);
    }

    if(isNumeric() && rhs.isNumeric())
        return Value(toDouble() - rhs.toDouble(), m_arena);

    NWB_ASSERT_MSG(false, NWB_TEXT("invalid operand types for operator-"));
    return Value(m_arena);
}

Value Value::operator*(const Value& rhs)const{
    if(m_type == ValueType::Integer && rhs.m_type == ValueType::Integer){
        if(MultiplyI64Overflows(m_data.m_integer, rhs.m_data.m_integer)){
            NWB_ASSERT_MSG(false, NWB_TEXT("integer overflow"));
            return Value(m_arena);
        }
        return Value(m_data.m_integer * rhs.m_data.m_integer, m_arena);
    }

    if(isNumeric() && rhs.isNumeric())
        return Value(toDouble() * rhs.toDouble(), m_arena);

    NWB_ASSERT_MSG(false, NWB_TEXT("invalid operand types for operator*"));
    return Value(m_arena);
}

Value Value::operator/(const Value& rhs)const{
    if(m_type == ValueType::Integer && rhs.m_type == ValueType::Integer){
        if(rhs.m_data.m_integer == 0){
            NWB_ASSERT_MSG(false, NWB_TEXT("division by zero"));
            return Value(m_arena);
        }
        if(DivideI64Overflows(m_data.m_integer, rhs.m_data.m_integer)){
            NWB_ASSERT_MSG(false, NWB_TEXT("integer overflow"));
            return Value(m_arena);
        }
        return Value(m_data.m_integer / rhs.m_data.m_integer, m_arena);
    }

    if(isNumeric() && rhs.isNumeric()){
        if(rhs.toDouble() == 0.0){
            NWB_ASSERT_MSG(false, NWB_TEXT("division by zero"));
            return Value(m_arena);
        }
        return Value(toDouble() / rhs.toDouble(), m_arena);
    }

    NWB_ASSERT_MSG(false, NWB_TEXT("invalid operand types for operator/"));
    return Value(m_arena);
}

Value& Value::operator+=(const Value& rhs){
    if(m_type == ValueType::Integer && rhs.m_type == ValueType::Integer){
        if(AddI64Overflows(m_data.m_integer, rhs.m_data.m_integer)){
            NWB_ASSERT_MSG(false, NWB_TEXT("integer overflow"));
            return *this;
        }
        m_data.m_integer += rhs.m_data.m_integer;
        return *this;
    }

    if(isNumeric() && rhs.isNumeric()){
        setDouble(toDouble() + rhs.toDouble());
        return *this;
    }

    if(m_type == ValueType::String && rhs.m_type == ValueType::String){
        if(m_data.m_string->size() > Limit<usize>::s_Max - rhs.m_data.m_string->size()){
            NWB_ASSERT_MSG(false, NWB_TEXT("string append size overflow"));
            return *this;
        }
        m_data.m_string->reserve(m_data.m_string->size() + rhs.m_data.m_string->size());
        m_data.m_string->append(rhs.m_data.m_string->data(), rhs.m_data.m_string->size());
        return *this;
    }

    if(m_type == ValueType::List){
        if(rhs.m_type == ValueType::List){
            if(m_data.m_list->size() > Limit<usize>::s_Max - rhs.m_data.m_list->size()){
                NWB_ASSERT_MSG(false, NWB_TEXT("list append size overflow"));
                return *this;
            }
            m_data.m_list->reserve(m_data.m_list->size() + rhs.m_data.m_list->size());
            for(const auto& elem : *rhs.m_data.m_list)
                m_data.m_list->push_back(elem);
        }
        else{
            if(m_data.m_list->size() == Limit<usize>::s_Max){
                NWB_ASSERT_MSG(false, NWB_TEXT("list append size overflow"));
                return *this;
            }
            m_data.m_list->push_back(rhs);
        }
        return *this;
    }

    NWB_ASSERT_MSG(false, NWB_TEXT("invalid operand types for operator+="));
    return *this;
}

Value& Value::operator-=(const Value& rhs){
    if(m_type == ValueType::Integer && rhs.m_type == ValueType::Integer){
        if(SubtractI64Overflows(m_data.m_integer, rhs.m_data.m_integer)){
            NWB_ASSERT_MSG(false, NWB_TEXT("integer overflow"));
            return *this;
        }
        m_data.m_integer -= rhs.m_data.m_integer;
        return *this;
    }

    if(isNumeric() && rhs.isNumeric()){
        setDouble(toDouble() - rhs.toDouble());
        return *this;
    }

    NWB_ASSERT_MSG(false, NWB_TEXT("invalid operand types for operator-="));
    return *this;
}

Value& Value::operator*=(const Value& rhs){
    if(m_type == ValueType::Integer && rhs.m_type == ValueType::Integer){
        if(MultiplyI64Overflows(m_data.m_integer, rhs.m_data.m_integer)){
            NWB_ASSERT_MSG(false, NWB_TEXT("integer overflow"));
            return *this;
        }
        m_data.m_integer *= rhs.m_data.m_integer;
        return *this;
    }

    if(isNumeric() && rhs.isNumeric()){
        setDouble(toDouble() * rhs.toDouble());
        return *this;
    }

    NWB_ASSERT_MSG(false, NWB_TEXT("invalid operand types for operator*="));
    return *this;
}

Value& Value::operator/=(const Value& rhs){
    if(m_type == ValueType::Integer && rhs.m_type == ValueType::Integer){
        if(rhs.m_data.m_integer == 0){
            NWB_ASSERT_MSG(false, NWB_TEXT("division by zero"));
            return *this;
        }
        if(DivideI64Overflows(m_data.m_integer, rhs.m_data.m_integer)){
            NWB_ASSERT_MSG(false, NWB_TEXT("integer overflow"));
            return *this;
        }
        m_data.m_integer /= rhs.m_data.m_integer;
        return *this;
    }

    if(isNumeric() && rhs.isNumeric()){
        if(rhs.toDouble() == 0.0){
            NWB_ASSERT_MSG(false, NWB_TEXT("division by zero"));
            return *this;
        }
        setDouble(toDouble() / rhs.toDouble());
        return *this;
    }

    NWB_ASSERT_MSG(false, NWB_TEXT("invalid operand types for operator/="));
    return *this;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


i64 Value::asInteger()const{
    NWB_ASSERT(m_type == ValueType::Integer);
    return m_data.m_integer;
}

f64 Value::asDouble()const{
    NWB_ASSERT(m_type == ValueType::Double);
    return m_data.m_double;
}

f64 Value::toDouble()const{
    if(m_type == ValueType::Integer)
        return static_cast<f64>(m_data.m_integer);
    NWB_ASSERT(m_type == ValueType::Double);
    return m_data.m_double;
}

MStringView Value::asString()const{
    NWB_ASSERT(m_type == ValueType::String);
    return MStringView(m_data.m_string->data(), m_data.m_string->size());
}

AString Value::copyString()const{
    if(!isString())
        return {};
    const MStringView sv = asString();
    return AString(sv.data(), sv.size());
}

const Value::ListType& Value::asList()const{
    NWB_ASSERT(m_type == ValueType::List);
    return *m_data.m_list;
}

Value::ListType& Value::asList(){
    NWB_ASSERT(m_type == ValueType::List);
    return *m_data.m_list;
}

const Value::MapType& Value::asMap()const{
    NWB_ASSERT(m_type == ValueType::Map);
    return *m_data.m_map;
}

Value::MapType& Value::asMap(){
    NWB_ASSERT(m_type == ValueType::Map);
    return *m_data.m_map;
}


void Value::setInteger(i64 val){
    destroy();
    m_type = ValueType::Integer;
    m_data.m_integer = val;
}

void Value::setDouble(f64 val){
    destroy();
    m_type = ValueType::Double;
    m_data.m_double = val;
}

void Value::setString(MStringView val){
    destroy();
    m_type = ValueType::String;
    m_data.m_string = NewArenaObject<StringType>(m_arena, val.data(), val.size(), MAllocator<MChar>(m_arena));
}

void Value::makeList(){
    destroy();
    m_type = ValueType::List;
    m_data.m_list = allocList();
}

void Value::makeMap(){
    destroy();
    m_type = ValueType::Map;
    m_data.m_map = allocMap();
}


Value& Value::field(MStringView name){
    if(m_type == ValueType::Null)
        makeMap();
    NWB_ASSERT(m_type == ValueType::Map);

    auto it = m_data.m_map->find(name);
    if(it != m_data.m_map->end())
        return it.value();

    StringType key(name.data(), name.size(), MAllocator<MChar>(m_arena));
    auto result = m_data.m_map->emplace(Move(key), Value(m_arena));
    return result.first.value();
}

const Value* Value::findField(MStringView name)const{
    NWB_ASSERT(m_type == ValueType::Map);

    auto it = m_data.m_map->find(name);
    if(it == m_data.m_map->end())
        return nullptr;
    return &it.value();
}

void Value::append(Value&& val){
    if(m_type == ValueType::Null)
        makeList();
    NWB_ASSERT(m_type == ValueType::List);
    m_data.m_list->push_back(Move(val));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void Value::destroy(){
    if(m_type == ValueType::Null)
        return;

    switch(m_type){
    case ValueType::String:
        DestroyArenaObject(m_arena, m_data.m_string);
        break;
    case ValueType::List:
        DestroyArenaObject(m_arena, m_data.m_list);
        break;
    case ValueType::Map:
        DestroyArenaObject(m_arena, m_data.m_map);
        break;
    default:
        break;
    }

    m_type = ValueType::Null;
    m_data = {};
}

void Value::copyFrom(const Value& other){
    m_type = other.m_type;
    m_data = {};

    switch(m_type){
    case ValueType::Integer:
        m_data.m_integer = other.m_data.m_integer;
        break;
    case ValueType::Double:
        m_data.m_double = other.m_data.m_double;
        break;
    case ValueType::String:
        m_data.m_string = NewArenaObject<StringType>(m_arena, other.m_data.m_string->data(), other.m_data.m_string->size(), MAllocator<MChar>(m_arena));
        break;
    case ValueType::List:{
        m_data.m_list = allocList();
        m_data.m_list->reserve(other.m_data.m_list->size());
        for(const auto& elem : *other.m_data.m_list)
            m_data.m_list->push_back(elem);
        break;
    }
    case ValueType::Map:{
        m_data.m_map = allocMap();
        for(const auto& [k, v] : *other.m_data.m_map)
            m_data.m_map->emplace(StringType(k, MAllocator<MChar>(m_arena)), v);
        break;
    }
    default:
        break;
    }
}

void Value::moveFrom(Value&& other)noexcept{
    m_type = other.m_type;
    m_data = other.m_data;

    other.m_type = ValueType::Null;
    other.m_data = {};
}

Value::StringType Value::makeArenaString(MStringView sv)const{
    return StringType(sv.data(), sv.size(), MAllocator<MChar>(m_arena));
}

Value::ListType* Value::allocList()const{
    return NewArenaObject<ListType>(m_arena, MAllocator<Value>(m_arena));
}

Value::MapType* Value::allocMap()const{
    return NewArenaObject<MapType>(m_arena, 0, MStringHash(), MStringEqual(), MAllocator<Pair<StringType, Value>>(m_arena));
}

void Value::freeList(ListType* p){
    DestroyArenaObject(m_arena, p);
}

void Value::freeMap(MapType* p){
    DestroyArenaObject(m_arena, p);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
