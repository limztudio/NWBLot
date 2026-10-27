// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "reflection_emitter.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void ReflectionEmitter::emitClassReflectionDeclarations(MString& out, const ClassDecl& decl)const{
    out.append("public:\r\n");

    if(!decl.assetTypeName.empty()){
        out.append("    inline static constexpr AStringView s_AssetTypeName = \"");
        out.append(decl.assetTypeName.data(), decl.assetTypeName.size());
        out.append("\";\r\n");
    }

    if(decl.isSerialize){
        out.append("    [[nodiscard]] usize serializeBinary(u8* outBuffer, usize bufferCapacity)const noexcept;\r\n");
        out.append("    [[nodiscard]] bool deserializeBinary(const u8* inBuffer, usize bufferSize)noexcept;\r\n");
    }

    if(decl.isReflect || !decl.assetTypeName.empty()){
        out.append("    [[nodiscard]] static constexpr usize reflectedFieldCount()noexcept;\r\n");
        out.append("    [[nodiscard]] static AStringView reflectedFieldName(usize index)noexcept;\r\n");
        out.append("    [[nodiscard]] static constexpr usize assetDependencyFieldCount()noexcept;\r\n");
        out.append("    [[nodiscard]] static AStringView assetDependencyFieldName(usize index)noexcept;\r\n");
        out.append("    template<typename NwbValue>\r\n");
        out.append("    [[nodiscard]] bool deserializeNwb(const NwbValue& inMap)noexcept{\r\n");
        out.append("        if(!inMap.isMap())\r\n");
        out.append("            return false;\r\n");
        for(const FieldDecl* field : decl.fields){
            if(!field || field->isTransient || field->access != AccessSpecifier::Public || !field->type)
                continue;
            const MStringView tname = field->type->name;
            if(tname == "bool"){
                out.append("        if(const auto* __f = inMap.findField(\"");
                out.append(field->name.data(), field->name.size());
                out.append("\"); __f && __f->isNumeric()) this->");
                out.append(field->name.data(), field->name.size());
                out.append(" = (__f->asInteger() != 0);\r\n");
            }
            else if(tname == "i8" || tname == "u8" || tname == "i16" || tname == "u16"
                || tname == "i32" || tname == "u32" || tname == "i64" || tname == "u64"
                || tname == "isize" || tname == "usize")
            {
                out.append("        if(const auto* __f = inMap.findField(\"");
                out.append(field->name.data(), field->name.size());
                out.append("\"); __f && __f->isNumeric()) this->");
                out.append(field->name.data(), field->name.size());
                out.append(" = static_cast<");
                out.append(tname.data(), tname.size());
                out.append(">(__f->asInteger());\r\n");
            }
            else if(tname == "f32" || tname == "f64"){
                out.append("        if(const auto* __f = inMap.findField(\"");
                out.append(field->name.data(), field->name.size());
                out.append("\"); __f && __f->isNumeric()) this->");
                out.append(field->name.data(), field->name.size());
                out.append(" = static_cast<");
                out.append(tname.data(), tname.size());
                out.append(">(__f->toDouble());\r\n");
            }
            else if(tname == "String" || tname == "CompactString"){
                out.append("        if(const auto* __f = inMap.findField(\"");
                out.append(field->name.data(), field->name.size());
                out.append("\"); __f && __f->isString()) this->");
                out.append(field->name.data(), field->name.size());
                out.append(".assign(__f->asString());\r\n");
            }
            else if(tname == "Name"){
                out.append("        if(const auto* __f = inMap.findField(\"");
                out.append(field->name.data(), field->name.size());
                out.append("\"); __f && __f->isString()) this->");
                out.append(field->name.data(), field->name.size());
                out.append(" = ::NWB::Core::Common::Name(__f->asString());\r\n");
            }
        }
        out.append("        return true;\r\n");
        out.append("    }\r\n");
    }
}

void ReflectionEmitter::emitClassReflectionDefinitions(MString& out, const ClassDecl& decl)const{
    if(decl.isSerialize){
        out.append("usize ");
        out.append(decl.name.data(), decl.name.size());
        out.append("::serializeBinary(u8* outBuffer, const usize bufferCapacity)const noexcept{\r\n");
        out.append("    usize offset = 0u;\r\n");
        for(const FieldDecl* field : decl.fields){
            if(!field || field->isTransient)
                continue;
            out.append("    if(offset + sizeof(this->");
            out.append(field->name.data(), field->name.size());
            out.append(") > bufferCapacity)\r\n        return 0u;\r\n");
            out.append("    NWB_MEMCPY(outBuffer + offset, bufferCapacity - offset, &this->");
            out.append(field->name.data(), field->name.size());
            out.append(", sizeof(this->");
            out.append(field->name.data(), field->name.size());
            out.append("));\r\n");
            out.append("    offset += sizeof(this->");
            out.append(field->name.data(), field->name.size());
            out.append(");\r\n");
        }
        out.append("    return offset;\r\n}\r\n\r\n");

        out.append("bool ");
        out.append(decl.name.data(), decl.name.size());
        out.append("::deserializeBinary(const u8* inBuffer, const usize bufferSize)noexcept{\r\n");
        out.append("    usize offset = 0u;\r\n");
        for(const FieldDecl* field : decl.fields){
            if(!field || field->isTransient)
                continue;
            out.append("    if(offset + sizeof(this->");
            out.append(field->name.data(), field->name.size());
            out.append(") > bufferSize)\r\n        return false;\r\n");
            out.append("    NWB_MEMCPY(&this->");
            out.append(field->name.data(), field->name.size());
            out.append(", sizeof(this->");
            out.append(field->name.data(), field->name.size());
            out.append("), inBuffer + offset, sizeof(this->");
            out.append(field->name.data(), field->name.size());
            out.append("));\r\n");
            out.append("    offset += sizeof(this->");
            out.append(field->name.data(), field->name.size());
            out.append(");\r\n");
        }
        out.append("    return true;\r\n}\r\n\r\n");
    }

    if(decl.isReflect || !decl.assetTypeName.empty()){
        usize nonTransientCount = 0u;
        usize assetRefCount = 0u;
        for(const FieldDecl* field : decl.fields){
            if(field && !field->isTransient){
                ++nonTransientCount;
                if(field->type && field->type->name == "AssetRef")
                    ++assetRefCount;
            }
        }

        out.append("constexpr usize ");
        out.append(decl.name.data(), decl.name.size());
        out.append("::reflectedFieldCount()noexcept{\r\n    return ");
        char buf[32];
        usize idx = 0u;
        usize val = nonTransientCount;
        if(val == 0u){
            out.push_back('0');
        }
        else{
            while(val > 0u && idx < sizeof(buf)){
                buf[idx++] = static_cast<char>('0' + (val % 10u));
                val /= 10u;
            }
            while(idx > 0u)
                out.push_back(buf[--idx]);
        }
        out.append("u;\r\n}\r\n\r\n");

        out.append("AStringView ");
        out.append(decl.name.data(), decl.name.size());
        out.append("::reflectedFieldName(const usize index)noexcept{\r\n");
        usize currentIdx = 0u;
        for(const FieldDecl* field : decl.fields){
            if(!field || field->isTransient)
                continue;
            out.append("    if(index == ");
            usize v = currentIdx++;
            if(v == 0u){
                out.push_back('0');
            }
            else{
                usize bIdx = 0u;
                while(v > 0u && bIdx < sizeof(buf)){
                    buf[bIdx++] = static_cast<char>('0' + (v % 10u));
                    v /= 10u;
                }
                while(bIdx > 0u)
                    out.push_back(buf[--bIdx]);
            }
            out.append("u)\r\n        return \"");
            out.append(field->name.data(), field->name.size());
            out.append("\";\r\n");
        }
        out.append("    return {};\r\n}\r\n\r\n");

        out.append("constexpr usize ");
        out.append(decl.name.data(), decl.name.size());
        out.append("::assetDependencyFieldCount()noexcept{\r\n    return ");
        usize depIdx = 0u;
        usize depVal = assetRefCount;
        if(depVal == 0u){
            out.push_back('0');
        }
        else{
            while(depVal > 0u && depIdx < sizeof(buf)){
                buf[depIdx++] = static_cast<char>('0' + (depVal % 10u));
                depVal /= 10u;
            }
            while(depIdx > 0u)
                out.push_back(buf[--depIdx]);
        }
        out.append("u;\r\n}\r\n\r\n");

        out.append("AStringView ");
        out.append(decl.name.data(), decl.name.size());
        out.append("::assetDependencyFieldName(const usize index)noexcept{\r\n");
        usize currentDepIdx = 0u;
        for(const FieldDecl* field : decl.fields){
            if(!field || field->isTransient || !field->type || field->type->name != "AssetRef")
                continue;
            out.append("    if(index == ");
            usize v = currentDepIdx++;
            if(v == 0u){
                out.push_back('0');
            }
            else{
                usize bIdx = 0u;
                while(v > 0u && bIdx < sizeof(buf)){
                    buf[bIdx++] = static_cast<char>('0' + (v % 10u));
                    v /= 10u;
                }
                while(bIdx > 0u)
                    out.push_back(buf[--bIdx]);
            }
            out.append("u)\r\n        return \"");
            out.append(field->name.data(), field->name.size());
            out.append("\";\r\n");
        }
        out.append("    return {};\r\n}\r\n\r\n");
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

