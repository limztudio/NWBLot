// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "bind_declaration_builder.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_metascript_bind_declaration_builder{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool ContainsInstanceName(const Value& instances, MStringView instanceName)noexcept{
    NWB_ASSERT(instances.isList());

    for(const Value& instance : instances.asList()){
        if(!instance.isMap())
            continue;

        const Value* name = instance.findField(LiteralView("name"));
        if(name && name->isString() && name->asString() == instanceName)
            return true;
    }

    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool BindDeclarationBuilder::addStruct(MStringView assetVariable, MStringView structName, Value&& attributes, Value&& fields, const u32 line, const u32 column){
    const auto root = assetRootMap(assetVariable, line, column);
    if(!root)
        return false;
    Value& assetRoot = **root;

    Value& structs = assetRoot.field(LiteralView("structs"));
    if(!ensureMapValue(structs, line, column, "asset.structs must be a map"))
        return false;
    if(structs.findField(structName)){
        m_cursor.error(line, column, "duplicate struct declaration");
        return false;
    }

    Value& outStruct = structs.field(structName);
    outStruct.makeMap();
    outStruct.field(LiteralView("attributes")) = Move(attributes);
    outStruct.field(LiteralView("fields")) = Move(fields);
    return true;
}

bool BindDeclarationBuilder::addInstance(MStringView assetVariable, MStringView typeName, MStringView instanceName, const u32 line, const u32 column){
    const auto root = assetRootMap(assetVariable, line, column);
    if(!root)
        return false;
    Value& assetRoot = **root;

    Value& instances = assetRoot.field(LiteralView("instances"));
    if(!ensureListValue(instances, line, column, "asset.instances must be a list"))
        return false;
    if(__hidden_metascript_bind_declaration_builder::ContainsInstanceName(instances, instanceName)){
        m_cursor.error(line, column, "duplicate struct instance declaration");
        return false;
    }

    Value instance(m_arena);
    instance.makeMap();
    instance.field(LiteralView("type")).setString(typeName);
    instance.field(LiteralView("name")).setString(instanceName);
    instances.append(Move(instance));
    return true;
}


Expected<NotNull<Value*>> BindDeclarationBuilder::assetRootMap(MStringView assetVariable, const u32 line, const u32 column){
    auto rootIt = m_variables.find(assetVariable);
    if(rootIt == m_variables.end()){
        m_cursor.error(line, column, "missing declared asset variable");
        return MakeUnexpected(Failure{});
    }

    Value& assetRoot = rootIt.value();
    if(!ensureMapValue(assetRoot, line, column, "bind declarations require asset root to be a map"))
        return MakeUnexpected(Failure{});
    return MakeNotNull(&assetRoot);
}

bool BindDeclarationBuilder::ensureMapValue(Value& value, const u32 line, const u32 column, MStringView message){
    if(value.isNull())
        value.makeMap();
    else if(!value.isMap()){
        m_cursor.error(line, column, message);
        return false;
    }
    return true;
}

bool BindDeclarationBuilder::ensureListValue(Value& value, const u32 line, const u32 column, MStringView message){
    if(value.isNull())
        value.makeList();
    else if(!value.isList()){
        m_cursor.error(line, column, message);
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

