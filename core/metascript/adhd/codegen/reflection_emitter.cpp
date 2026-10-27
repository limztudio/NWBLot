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

    if(decl.isReflect){
        out.append("    [[nodiscard]] static constexpr usize reflectedFieldCount()noexcept;\r\n");
        out.append("    [[nodiscard]] static AStringView reflectedFieldName(usize index)noexcept;\r\n");
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

    if(decl.isReflect){
        usize nonTransientCount = 0u;
        for(const FieldDecl* field : decl.fields){
            if(field && !field->isTransient)
                ++nonTransientCount;
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
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

