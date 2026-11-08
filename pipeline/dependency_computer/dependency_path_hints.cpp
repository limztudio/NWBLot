// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "dependency_path_hints.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEPENDENCY_COMPUTER_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


DependencyPathHints::DependencyPathHints(Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena& scratchArena)
    : m_arena(arena)
    , m_texts(0, Hasher<Name>(), EqualTo<Name>(), scratchArena)
{}

void DependencyPathHints::reserve(const usize count){
    m_texts.reserve(count);
}

void DependencyPathHints::record(const AStringView rawText){
    const Expected<AStringView> trimmed = TryTrimVirtualPathText(rawText);
    if(!trimmed)
        return;

    const auto name = Name::FromText(*trimmed);
    if(!name || !*name)
        return;

    if(m_texts.find(*name) == m_texts.end())
        m_texts.emplace(*name, Core::Assets::AssetString(*trimmed, m_arena));
}

void DependencyPathHints::collect(const Core::Metascript::Value& value){
    if(value.isString()){
        const Core::Metascript::MStringView str = value.asString();
        record(AStringView(str.data(), str.size()));
        return;
    }
    if(value.isList()){
        for(const Core::Metascript::Value& item : value.asList())
            collect(item);
        return;
    }
    if(value.isMap()){
        for(const auto& [key, fieldValue] : value.asMap()){
            record(AStringView(key.data(), key.size()));
            collect(fieldValue);
        }
    }
}

void DependencyPathHints::collect(const Core::Metascript::Document& doc){
    for(const Core::Metascript::Document::Declaration& declaration : doc.declarations()){
        const Core::Metascript::MStringView varView(declaration.variable.data(), declaration.variable.size());
        if(const Core::Metascript::Value* value = doc.findVariable(varView))
            collect(*value);
    }
}

[[nodiscard]] AStringView DependencyPathHints::lookup(const Name& virtualPath)const noexcept{
    const auto found = m_texts.find(virtualPath);
    if(found != m_texts.end())
        return AStringView(found.value());
    return virtualPath.resolvedText();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEPENDENCY_COMPUTER_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

