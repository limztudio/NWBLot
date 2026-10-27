// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "symbol_table.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool SymbolTable::build(TranslationUnit& unit, MVector<ParseError>& errors){
    bool ok = true;

    for(EnumDecl* decl : unit.enums){
        if(findEnum(decl->name) || findInterface(decl->name) || findClass(decl->name) || findTypeAlias(decl->name)){
            reportError(errors, decl->span, "duplicate type symbol name");
            ok = false;
            continue;
        }
        m_enums.push_back(decl);
    }

    for(InterfaceDecl* decl : unit.interfaces){
        if(findEnum(decl->name) || findInterface(decl->name) || findClass(decl->name) || findTypeAlias(decl->name)){
            reportError(errors, decl->span, "duplicate type symbol name");
            ok = false;
            continue;
        }
        m_interfaces.push_back(decl);
    }

    for(ClassDecl* decl : unit.classes){
        if(findEnum(decl->name) || findInterface(decl->name) || findClass(decl->name) || findTypeAlias(decl->name)){
            reportError(errors, decl->span, "duplicate type symbol name");
            ok = false;
            continue;
        }
        m_classes.push_back(decl);
    }

    for(TypeAliasDecl* decl : unit.typeAliases){
        if(findEnum(decl->name) || findInterface(decl->name) || findClass(decl->name) || findTypeAlias(decl->name)){
            reportError(errors, decl->span, "duplicate type symbol name");
            ok = false;
            continue;
        }
        m_typeAliases.push_back(decl);
    }

    for(MethodDecl* fn : unit.freeFunctions)
        m_freeFunctions.push_back(fn);

    for(Stmt* c : unit.globalConstants){
        if(findGlobalConstant(c->name)){
            reportError(errors, c->span, "duplicate file-level 'const' name");
            ok = false;
            continue;
        }
        m_globalConstants.push_back(c);
    }

    return ok;
}

EnumDecl* SymbolTable::findEnum(const MStringView name)const noexcept{
    for(EnumDecl* decl : m_enums){
        if(decl->name == name)
            return decl;
    }
    return nullptr;
}

InterfaceDecl* SymbolTable::findInterface(const MStringView name)const noexcept{
    for(InterfaceDecl* decl : m_interfaces){
        if(decl->name == name)
            return decl;
    }
    return nullptr;
}

ClassDecl* SymbolTable::findClass(const MStringView name)const noexcept{
    for(ClassDecl* decl : m_classes){
        if(decl->name == name)
            return decl;
    }
    return nullptr;
}

TypeAliasDecl* SymbolTable::findTypeAlias(const MStringView name)const noexcept{
    for(TypeAliasDecl* decl : m_typeAliases){
        if(decl->name == name)
            return decl;
    }
    return nullptr;
}

MethodDecl* SymbolTable::findFreeFunction(const MStringView name)const noexcept{
    for(MethodDecl* fn : m_freeFunctions){
        if(fn->name == name)
            return fn;
    }
    return nullptr;
}

Stmt* SymbolTable::findGlobalConstant(const MStringView name)const noexcept{
    for(Stmt* c : m_globalConstants){
        if(c->name == name)
            return c;
    }
    return nullptr;
}

MethodDecl* SymbolTable::findInterfaceMethod(const MStringView interfaceName, const MStringView methodName)const noexcept{
    const InterfaceDecl* iface = findInterface(interfaceName);
    if(!iface)
        return nullptr;

    for(MethodDecl* method : iface->methods){
        if(method->name == methodName)
            return method;
    }

    for(const MStringView& baseName : iface->baseInterfaces){
        MethodDecl* inherited = findInterfaceMethod(baseName, methodName);
        if(inherited)
            return inherited;
    }

    return nullptr;
}

void SymbolTable::collectAllInterfaceMethods(const MStringView interfaceName, MVector<MethodDecl*>& outMethods)const{
    const InterfaceDecl* iface = findInterface(interfaceName);
    if(!iface)
        return;

    for(const MStringView& baseName : iface->baseInterfaces)
        collectAllInterfaceMethods(baseName, outMethods);

    for(MethodDecl* method : iface->methods)
        outMethods.push_back(method);
}

bool SymbolTable::isArenaBackedContainer(const MStringView typeName)noexcept{
    return typeName == "Vector"
        || typeName == "FixedVector"
        || typeName == "ParallelVector"
        || typeName == "ForwardList"
        || typeName == "List"
        || typeName == "Deque"
        || typeName == "Set"
        || typeName == "HashSet"
        || typeName == "ParallelHashSet"
        || typeName == "Map"
        || typeName == "HashMap"
        || typeName == "ParallelHashMap"
        || typeName == "Queue"
        || typeName == "ParallelQueue"
        || typeName == "ParallelBlockQueue";
}

bool SymbolTable::isSimdVectorType(const MStringView typeName, u32& outLanes)noexcept{
    if(typeName == "f64x2"){
        outLanes = 2u;
        return true;
    }
    if(typeName == "f64x3"){
        outLanes = 3u;
        return true;
    }
    if(typeName == "f32x4" || typeName == "i32x4" || typeName == "u32x4"
        || typeName == "f32x3_i32" || typeName == "f32x3_u32"
        || typeName == "f64x4" || typeName == "i64x4" || typeName == "u64x4"
        || typeName == "f64x3_i64" || typeName == "f64x3_u64")
    {
        outLanes = 4u;
        return true;
    }
    return false;
}

bool SymbolTable::isStringSwitchType(const MStringView typeName)noexcept{
    return typeName == "String"
        || typeName == "StringView"
        || typeName == "CompactString"
        || typeName == "Name";
}

void SymbolTable::reportError(MVector<ParseError>& errors, const SourceSpan span, const MStringView message)const{
    errors.push_back(ParseError{span.line, span.column, MString(message.data(), message.size(), m_arena)});
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

