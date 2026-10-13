// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "value.h"
#include "arena_names.h"

#include <global/allocation_size.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Value Value::Reference(MStringView val, MetaArena& arena){
    Value out(arena);
    out.setReference(val);
    return out;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Value::Value(MetaArena& arena)noexcept
    : m_arena(arena)
{}

Value::Value(i64 val, MetaArena& arena)noexcept
    : m_arena(arena)
    , m_type(ValueType::Integer)
{
    m_data.m_integer = val;
}

Value::Value(f64 val, MetaArena& arena)noexcept
    : m_arena(arena)
    , m_type(ValueType::Double)
{
    m_data.m_double = val;
}

Value::Value(MStringView val, MetaArena& arena)
    : m_arena(arena)
{
    m_data.m_string = NewArenaObject<StringType>(m_arena, val.data(), val.size(), m_arena);
    if(!m_data.m_string)
        throw AllocationException{};
    m_type = ValueType::String;
}

Value::~Value()noexcept{
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
    if(this != &other)
        copyFrom(other);
    return *this;
}

Value& Value::operator=(Value&& other){
    if(this != &other){
        Value candidate(m_arena);
        if(&m_arena != &other.m_arena){
            candidate.copyFrom(other);
            other.destroy();
        }
        else
            candidate.moveFrom(Move(other));
        destroy();
        moveFrom(Move(candidate));
    }
    return *this;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Value Value::operator+(const Value& rhs)const{
    if(m_type == ValueType::Integer && rhs.m_type == ValueType::Integer){
        if(AddOverflows<i64>(m_data.m_integer, rhs.m_data.m_integer)){
            NWB_ASSERT_MSG(false, MetascriptArenaScope::s_IntegerOverflowMessage);
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
            NWB_ASSERT_MSG(false, MetascriptArenaScope::s_StringConcatenationSizeOverflowMessage);
            return Value(m_arena);
        }

        StringType result{m_arena};
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
            NWB_ASSERT_MSG(false, MetascriptArenaScope::s_ListConcatenationSizeOverflowMessage);
            return Value(m_arena);
        }
        dst.reserve(m_data.m_list->size() + rhs.m_data.m_list->size());
        v.appendListCopies(*m_data.m_list, m_data.m_list->size());
        v.appendListCopies(*rhs.m_data.m_list, rhs.m_data.m_list->size());
        return v;
    }

    NWB_ASSERT_MSG(false, NWB_TEXT("invalid operand types for operator+"));
    return Value(m_arena);
}

Value Value::operator-(const Value& rhs)const noexcept{
    if(m_type == ValueType::Integer && rhs.m_type == ValueType::Integer){
        if(SubtractOverflows<i64>(m_data.m_integer, rhs.m_data.m_integer)){
            NWB_ASSERT_MSG(false, MetascriptArenaScope::s_IntegerOverflowMessage);
            return Value(m_arena);
        }
        return Value(m_data.m_integer - rhs.m_data.m_integer, m_arena);
    }

    if(isNumeric() && rhs.isNumeric())
        return Value(toDouble() - rhs.toDouble(), m_arena);

    NWB_ASSERT_MSG(false, NWB_TEXT("invalid operand types for operator-"));
    return Value(m_arena);
}

Value Value::operator*(const Value& rhs)const noexcept{
    if(m_type == ValueType::Integer && rhs.m_type == ValueType::Integer){
        if(MultiplyOverflows<i64>(m_data.m_integer, rhs.m_data.m_integer)){
            NWB_ASSERT_MSG(false, MetascriptArenaScope::s_IntegerOverflowMessage);
            return Value(m_arena);
        }
        return Value(m_data.m_integer * rhs.m_data.m_integer, m_arena);
    }

    if(isNumeric() && rhs.isNumeric())
        return Value(toDouble() * rhs.toDouble(), m_arena);

    NWB_ASSERT_MSG(false, NWB_TEXT("invalid operand types for operator*"));
    return Value(m_arena);
}

Value Value::operator/(const Value& rhs)const noexcept{
    if(m_type == ValueType::Integer && rhs.m_type == ValueType::Integer){
        if(rhs.m_data.m_integer == 0){
            NWB_ASSERT_MSG(false, MetascriptArenaScope::s_DivisionByZeroMessage);
            return Value(m_arena);
        }
        if(DivideOverflows<i64>(m_data.m_integer, rhs.m_data.m_integer)){
            NWB_ASSERT_MSG(false, MetascriptArenaScope::s_IntegerOverflowMessage);
            return Value(m_arena);
        }
        return Value(m_data.m_integer / rhs.m_data.m_integer, m_arena);
    }

    if(isNumeric() && rhs.isNumeric()){
        if(rhs.toDouble() == 0.0){
            NWB_ASSERT_MSG(false, MetascriptArenaScope::s_DivisionByZeroMessage);
            return Value(m_arena);
        }
        return Value(toDouble() / rhs.toDouble(), m_arena);
    }

    NWB_ASSERT_MSG(false, NWB_TEXT("invalid operand types for operator/"));
    return Value(m_arena);
}

Value& Value::operator+=(const Value& rhs){
    if(m_type == ValueType::Integer && rhs.m_type == ValueType::Integer){
        if(AddOverflows<i64>(m_data.m_integer, rhs.m_data.m_integer)){
            NWB_ASSERT_MSG(false, MetascriptArenaScope::s_IntegerOverflowMessage);
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
            NWB_ASSERT_MSG(false, MetascriptArenaScope::s_StringAppendSizeOverflowMessage);
            return *this;
        }
        const usize requiredStringCapacity = m_data.m_string->size() + rhs.m_data.m_string->size();
        ::ContainerDetail::ReserveGrowingCapacity(*m_data.m_string, requiredStringCapacity);
        m_data.m_string->append(rhs.m_data.m_string->data(), rhs.m_data.m_string->size());
        return *this;
    }

    if(m_type == ValueType::List){
        if(rhs.m_type == ValueType::List){
            // The separately owned list survives relocation when rhs is an element of this list.
            const ListType& sourceList = *rhs.m_data.m_list;
            if(m_data.m_list->size() > Limit<usize>::s_Max - sourceList.size()){
                NWB_ASSERT_MSG(false, MetascriptArenaScope::s_ListAppendSizeOverflowMessage);
                return *this;
            }
            const usize appendCount = sourceList.size();
            const usize requiredListCapacity = m_data.m_list->size() + appendCount;
            ::ContainerDetail::ReserveGrowingCapacity(*m_data.m_list, requiredListCapacity);
            appendListCopies(sourceList, appendCount);
        }
        else{
            if(m_data.m_list->size() == Limit<usize>::s_Max){
                NWB_ASSERT_MSG(false, MetascriptArenaScope::s_ListAppendSizeOverflowMessage);
                return *this;
            }
            const usize listSize = m_data.m_list->size();
            const auto rhsIndex = ::ContainerDetail::SourceAliasesDestination(*m_data.m_list, &rhs, 1u);
            const usize requiredListCapacity = listSize + 1u;
            ::ContainerDetail::ReserveGrowingCapacity(*m_data.m_list, requiredListCapacity);
            appendListCopy(rhsIndex ? (*m_data.m_list)[*rhsIndex] : rhs);
        }
        return *this;
    }

    NWB_ASSERT_MSG(false, NWB_TEXT("invalid operand types for operator+="));
    return *this;
}

Value& Value::operator-=(const Value& rhs)noexcept{
    if(m_type == ValueType::Integer && rhs.m_type == ValueType::Integer){
        if(SubtractOverflows<i64>(m_data.m_integer, rhs.m_data.m_integer)){
            NWB_ASSERT_MSG(false, MetascriptArenaScope::s_IntegerOverflowMessage);
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

Value& Value::operator*=(const Value& rhs)noexcept{
    if(m_type == ValueType::Integer && rhs.m_type == ValueType::Integer){
        if(MultiplyOverflows<i64>(m_data.m_integer, rhs.m_data.m_integer)){
            NWB_ASSERT_MSG(false, MetascriptArenaScope::s_IntegerOverflowMessage);
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

Value& Value::operator/=(const Value& rhs)noexcept{
    if(m_type == ValueType::Integer && rhs.m_type == ValueType::Integer){
        if(rhs.m_data.m_integer == 0){
            NWB_ASSERT_MSG(false, MetascriptArenaScope::s_DivisionByZeroMessage);
            return *this;
        }
        if(DivideOverflows<i64>(m_data.m_integer, rhs.m_data.m_integer)){
            NWB_ASSERT_MSG(false, MetascriptArenaScope::s_IntegerOverflowMessage);
            return *this;
        }
        m_data.m_integer /= rhs.m_data.m_integer;
        return *this;
    }

    if(isNumeric() && rhs.isNumeric()){
        if(rhs.toDouble() == 0.0){
            NWB_ASSERT_MSG(false, MetascriptArenaScope::s_DivisionByZeroMessage);
            return *this;
        }
        setDouble(toDouble() / rhs.toDouble());
        return *this;
    }

    NWB_ASSERT_MSG(false, NWB_TEXT("invalid operand types for operator/="));
    return *this;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


i64 Value::asInteger()const noexcept{
    NWB_ASSERT(m_type == ValueType::Integer);
    return m_data.m_integer;
}

f64 Value::asDouble()const noexcept{
    NWB_ASSERT(m_type == ValueType::Double);
    return m_data.m_double;
}

f64 Value::toDouble()const noexcept{
    if(m_type == ValueType::Integer)
        return static_cast<f64>(m_data.m_integer);
    NWB_ASSERT(m_type == ValueType::Double);
    return m_data.m_double;
}

MStringView Value::asString()const noexcept{
    NWB_ASSERT(m_type == ValueType::String);
    return MStringView(m_data.m_string->data(), m_data.m_string->size());
}

MStringView Value::asReference()const noexcept{
    NWB_ASSERT(m_type == ValueType::Reference);
    return MStringView(m_data.m_string->data(), m_data.m_string->size());
}

MString Value::copyString()const{
    if(!isString())
        return MString(m_arena);
    const MStringView sv = asString();
    return MString(sv.data(), sv.size(), m_arena);
}

const Value::ListType& Value::asList()const noexcept{
    NWB_ASSERT(m_type == ValueType::List);
    return *m_data.m_list;
}

Value::ListType& Value::asList()noexcept{
    NWB_ASSERT(m_type == ValueType::List);
    return *m_data.m_list;
}

const Value::MapType& Value::asMap()const noexcept{
    NWB_ASSERT(m_type == ValueType::Map);
    return *m_data.m_map;
}

Value::MapType& Value::asMap()noexcept{
    NWB_ASSERT(m_type == ValueType::Map);
    return *m_data.m_map;
}


void Value::setInteger(i64 val)noexcept{
    destroy();
    m_type = ValueType::Integer;
    m_data.m_integer = val;
}

void Value::setDouble(f64 val)noexcept{
    destroy();
    m_type = ValueType::Double;
    m_data.m_double = val;
}

void Value::setString(MStringView val){
    StringType* const replacement = NewArenaObject<StringType>(m_arena, val.data(), val.size(), m_arena);
    if(!replacement)
        throw AllocationException{};
    destroy();
    m_data.m_string = replacement;
    m_type = ValueType::String;
}

void Value::setReference(MStringView val){
    StringType* const replacement = NewArenaObject<StringType>(m_arena, val.data(), val.size(), m_arena);
    if(!replacement)
        throw AllocationException{};
    destroy();
    m_data.m_string = replacement;
    m_type = ValueType::Reference;
}

void Value::makeList(){
    ListType* const replacement = allocList();
    destroy();
    m_data.m_list = replacement;
    m_type = ValueType::List;
}

void Value::makeMap(){
    MapType* const replacement = allocMap();
    destroy();
    m_data.m_map = replacement;
    m_type = ValueType::Map;
}

Value& Value::field(MStringView name){
    if(m_type == ValueType::Null)
        makeMap();
    NWB_ASSERT(m_type == ValueType::Map);

    auto it = m_data.m_map->find(name);
    if(it != m_data.m_map->end())
        return it.value();

    StringType key(name.data(), name.size(), m_arena);
    auto result = m_data.m_map->emplace(Move(key), Value(m_arena));
    return result.first.value();
}

const Value* Value::findField(MStringView name)const noexcept{
    NWB_ASSERT(m_type == ValueType::Map);

    auto it = m_data.m_map->find(name);
    if(it == m_data.m_map->end())
        return nullptr;
    return &it.value();
}

void Value::append(Value&& val){
    Value selfCopy(m_arena);
    const bool valIsSelf = &val == this;
    if(valIsSelf)
        selfCopy.copyFrom(val);

    if(m_type == ValueType::Null)
        makeList();
    NWB_ASSERT(m_type == ValueType::List);

    const usize listSize = m_data.m_list->size();
    if(listSize == Limit<usize>::s_Max){
        NWB_ASSERT_MSG(false, MetascriptArenaScope::s_ListAppendSizeOverflowMessage);
        return;
    }

    if(valIsSelf){
        appendListCopy(selfCopy);
        return;
    }

    const auto valIndex = ::ContainerDetail::SourceAliasesDestination(*m_data.m_list, &val, 1u);
    if(valIndex){
        const usize requiredListCapacity = listSize + 1u;
        ::ContainerDetail::ReserveGrowingCapacity(*m_data.m_list, requiredListCapacity);
        appendListCopy((*m_data.m_list)[*valIndex]);
        (*m_data.m_list)[*valIndex].destroy();
        return;
    }

    if(&m_arena != &val.m_arena){
        appendListCopy(val);
        val.destroy();
        return;
    }

    m_data.m_list->push_back(Move(val));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void Value::destroy()noexcept{
    if(m_type == ValueType::Null)
        return;

    switch(m_type){
    case ValueType::String:
    case ValueType::Reference:
        DestroyArenaObjectNoexcept(m_arena, m_data.m_string);
        break;
    case ValueType::List:
        DestroyArenaObjectNoexcept(m_arena, m_data.m_list);
        break;
    case ValueType::Map:
        DestroyArenaObjectNoexcept(m_arena, m_data.m_map);
        break;
    default:
        break;
    }

    m_type = ValueType::Null;
    m_data = {};
}

void Value::copyFrom(const Value& other){
    Value candidate(m_arena);
    switch(other.m_type){
    case ValueType::Integer:
        candidate.setInteger(other.m_data.m_integer);
        break;
    case ValueType::Double:
        candidate.setDouble(other.m_data.m_double);
        break;
    case ValueType::String:
        candidate.setString(other.asString());
        break;
    case ValueType::Reference:
        candidate.setReference(other.asReference());
        break;
    case ValueType::List:
        candidate.makeList();
        candidate.m_data.m_list->reserve(other.m_data.m_list->size());
        candidate.appendListCopies(*other.m_data.m_list, other.m_data.m_list->size());
        break;
    case ValueType::Map:
        candidate.makeMap();
        candidate.m_data.m_map->reserve(other.m_data.m_map->size());
        for(const auto& [k, v] : *other.m_data.m_map){
            auto result = candidate.m_data.m_map->emplace(StringType(k, m_arena), Value(m_arena));
            if(!result.second){
                NWB_ASSERT(false);
                continue;
            }
            result.first.value().copyFrom(v);
        }
        break;
    default:
        break;
    }

    destroy();
    moveFrom(Move(candidate));
}

void Value::moveFrom(Value&& other)noexcept{
    NWB_ASSERT(&m_arena == &other.m_arena);

    m_type = other.m_type;
    m_data = other.m_data;

    other.m_type = ValueType::Null;
    other.m_data = {};
}

void Value::appendListCopy(const Value& val){
    NWB_ASSERT(m_type == ValueType::List);
    Value candidate(m_arena);
    candidate.copyFrom(val);
    m_data.m_list->push_back(Move(candidate));
}

void Value::appendListCopies(const ListType& values, const usize count){
    NWB_ASSERT(count <= values.size());
    for(usize i = 0u; i < count; ++i)
        appendListCopy(values[i]);
}

Value::ListType* Value::allocList()const{
    ListType* const created = NewArenaObject<ListType>(m_arena, m_arena);
    if(!created)
        throw AllocationException{};
    return created;
}

Value::MapType* Value::allocMap()const{
    MapType* const created = NewArenaObject<MapType>(m_arena, 0, MStringHash(), MStringEqual(), m_arena);
    if(!created)
        throw AllocationException{};
    return created;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

