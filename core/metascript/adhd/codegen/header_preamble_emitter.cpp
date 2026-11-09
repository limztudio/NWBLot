// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "header_preamble_emitter.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void HeaderPreambleEmitter::Emit(MString& out, const TranslationUnit& unit, const MStringView namespacePath){
    out.append(s_AdhdGeneratedFileBanner.data(), s_AdhdGeneratedFileBanner.size());
    out.append("////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////\r\n\r\n\r\n");
    out.append("#pragma once\r\n\r\n\r\n");
    if(IsNameLiteralOnlyUnit(unit)){
        out.append("#include <global/name.h>\r\n");
    }
    else if(IsScalarLiteralOnlyUnit(unit)){
        out.append("#include <global/type.h>\r\n");
    }
    else{
        out.append("#include <global/global.h>\r\n");
        out.append("#include <global/atomic_identity.h>\r\n");
        out.append("#include <global/binary_big_endian.h>\r\n");
        out.append("#include <global/binary_little_endian.h>\r\n");
        out.append("#include <global/numeric_range.h>\r\n");
        out.append("#include <global/simdmath.h>\r\n");
        out.append("#include <global/inplace_function.h>\r\n");
        out.append("#include <global/not_null.h>\r\n");
        out.append("#include <global/termination.h>\r\n");
        out.append("#include <core/alloc/global.h>\r\n");
        out.append("#include <core/alloc/general.h>\r\n");
        out.append("#include <core/alloc/scratch.h>\r\n");
        out.append("#include <core/assets/ref.h>\r\n");
        out.append("#include <core/ecs/entity_id.h>\r\n");
        if(namespacePath == "NWB::Impl::Scene")
            out.append("#include <impl/ecs_scene/global.adhd.h>\r\n");
        else if(namespacePath.size() >= 9u && namespacePath.substr(0u, 9u) == "NWB::Impl")
            out.append("#include <impl/global.adhd.h>\r\n");
    }

    b8 hasSubsystemDecl = false;
    b8 hasComponentAssetDecl = false;
    b8 hasHeaderTaskScope = false;
    for(const ClassDecl* classDecl : unit.classes){
        if(!classDecl)
            continue;
        for(const MethodDecl* method : classDecl->methods){
            if(!method || method->isImported || !method->hasBody || !method->usesTaskScope)
                continue;
            if(!classDecl->templateParams.empty() || !method->templateParams.empty() || method->isInline){
                hasHeaderTaskScope = true;
                break;
            }
        }
        if(classDecl->isImported)
            continue;
        if(!classDecl->subsystemTypeName.empty())
            hasSubsystemDecl = true;
        if(!classDecl->componentAssetTypeName.empty())
            hasComponentAssetDecl = true;
    }
    if(hasSubsystemDecl)
        out.append("#include <impl/assets_subsystem/asset.adhd.h>\r\n");
    if(hasComponentAssetDecl)
        out.append("#include <impl/assets_scene/components.adhd.h>\r\n");
    if(!hasHeaderTaskScope){
        for(const MethodDecl* fn : unit.freeFunctions){
            if(fn && !fn->isImported && fn->hasBody && fn->usesTaskScope && (fn->isInline || !fn->templateParams.empty())){
                hasHeaderTaskScope = true;
                break;
            }
        }
    }
    if(hasHeaderTaskScope)
        out.append("#include <core/task/cpu/scheduler.h>\r\n");

    for(const MString& inc : unit.includedAdhdFiles){
        const MStringView incView = StripAdhdExtension(MStringView(inc.data(), inc.size()));
        out.append("#include \"");
        out.append(incView.data(), incView.size());
        out.append(".adhd.h\"\r\n");
    }

    if(!unit.topLevelNativeBlocks.empty()){
        out.append("\r\n");
        for(const MStringView& rawBlock : unit.topLevelNativeBlocks){
            AppendTextDiscardCrAndExpandLf(out, rawBlock);
            out.append("\r\n");
        }
    }

    out.append("\r\n\r\n////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////\r\n\r\n\r\n");

    if(!namespacePath.empty()){
        out.append("namespace ");
        out.append(namespacePath.data(), namespacePath.size());
        out.append("{\r\n\r\n\r\n");
    }

}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

