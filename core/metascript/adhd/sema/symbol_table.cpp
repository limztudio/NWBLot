// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "symbol_table.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool SymbolTable::build(TranslationUnit& unit, MVector<ParseError>& errors){
    bool ok = true;

    for(EnumDecl* decl : unit.enums){
        if(hasTypeSymbol(decl->name)){
            ReportAdhdDiagnostic(m_arena, errors, decl->span, "duplicate type symbol name");
            ok = false;
            continue;
        }
        m_enums.push_back(decl);
    }

    for(InterfaceDecl* decl : unit.interfaces){
        if(hasTypeSymbol(decl->name)){
            ReportAdhdDiagnostic(m_arena, errors, decl->span, "duplicate type symbol name");
            ok = false;
            continue;
        }
        m_interfaces.push_back(decl);
    }

    for(ClassDecl* decl : unit.classes){
        if(hasTypeSymbol(decl->name)){
            ReportAdhdDiagnostic(m_arena, errors, decl->span, "duplicate type symbol name");
            ok = false;
            continue;
        }
        m_classes.push_back(decl);
    }

    for(TypeAliasDecl* decl : unit.typeAliases){
        if(hasTypeSymbol(decl->name)){
            ReportAdhdDiagnostic(m_arena, errors, decl->span, "duplicate type symbol name");
            ok = false;
            continue;
        }
        m_typeAliases.push_back(decl);
    }

    for(MethodDecl* fn : unit.freeFunctions)
        m_freeFunctions.push_back(fn);

    for(Stmt* c : unit.globalConstants){
        if(findGlobalConstant(c->name)){
            ReportAdhdDiagnostic(m_arena, errors, c->span, "duplicate file-level 'const' name");
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

MStringView SymbolTable::mapBuiltinCppType(const MStringView typeName)noexcept{
    if(typeName == "String") return "AString";
    if(typeName == "StringView") return "AStringView";
    if(typeName == "CompactString") return "ACompactString";
    if(typeName == "f32x4") return "Float4";
    if(typeName == "i32x4") return "Int4";
    if(typeName == "u32x4") return "UInt4";
    if(typeName == "f32x3_i32") return "Float3Int";
    if(typeName == "f32x3_u32") return "Float3UInt";
    if(typeName == "f32x3x4") return "Float34";
    if(typeName == "f32x4x4") return "Float44";
    if(typeName == "f64x2") return "Double2";
    if(typeName == "f64x3") return "Double3";
    if(typeName == "f64x4") return "Double4";
    if(typeName == "i64x4") return "Int64x4";
    if(typeName == "u64x4") return "UInt64x4";
    if(typeName == "f64x3_i64") return "Double3Int";
    if(typeName == "f64x3_u64") return "Double3UInt";
    if(typeName == "f64x3x3") return "Double33";
    if(typeName == "f64x3x4") return "Double34";
    if(typeName == "f64x4x4") return "Double44";
    return {};
}

bool SymbolTable::hasTypeSymbol(const MStringView name)const noexcept{
    return findEnum(name) != nullptr
        || findInterface(name) != nullptr
        || findClass(name) != nullptr
        || findTypeAlias(name) != nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

