// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/assets_font_atlas/asset.adhd.h>
#include <impl/assets_font_atlas/cook.adhd.h>
#include <tests/common/gtest_sdk.h>
#include <tests/common/memory_sdk.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN
namespace Tests::FontAtlasCases{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////




[[nodiscard]] inline b8 VerifyFontAtlasAllocationUnwindContracts(){
    static_assert(!noexcept(Impl::ValidateFontAtlasGroupGuardedOverlap(
        DeclVal<const Span<const Impl::FontAtlasGlyph>&>(),
        0u,
        0u,
        0u,
        0u,
        0u
    )));
    static_assert(!noexcept(Impl::DeserializeFontAtlasBinaryInto(
        DeclVal<const Span<const u8>&>(),
        DeclVal<Impl::FontAtlasPayload&>()
    )));
    static_assert(!noexcept(Impl::SerializeFontAtlasPayloadValidatedInto(
        DeclVal<const Impl::FontAtlasPayload&>(),
        DeclVal<Vector<u8, ArenaBase>&>()
    )));
    static_assert(!noexcept(Impl::CopyFontAtlasPositioningTables(
        DeclVal<ArenaBase&>(),
        DeclVal<const Span<const u8>&>(),
        0u,
        0u
    )));
    static_assert(!noexcept(DeclVal<Impl::FontAtlas&>().loadBinary(DeclVal<const Span<const u8>&>())));
    static_assert(!noexcept(DeclVal<const Impl::FontAtlas&>().validatePayload()));
    static_assert(!noexcept(DeclVal<Impl::FontAtlas&>().setPayload(DeclVal<const Impl::FontAtlasPayload&>())));
    static_assert(!noexcept(Impl::SerializeFontAtlasAsset(
        DeclVal<const Impl::FontAtlas&>(),
        DeclVal<Vector<u8, ArenaBase>&>()
    )));
    static_assert(!noexcept(Impl::BuildFontAtlasAsset(
        DeclVal<ArenaBase&>(),
        DeclVal<const Impl::FontAtlasCookEntry&>()
    )));
    static_assert(!noexcept(DeclVal<const Impl::FontAtlasAssetCodec&>().serialize(
        DeclVal<const Core::Assets::IAsset&>(),
        DeclVal<Core::Assets::AssetBytes&>()
    )));
    return true;
}

[[nodiscard]] inline b8 VerifyFontAtlasOomUnwindPreservesExistingAtlas(
    const Impl::FontAtlasPayload& baselinePayload,
    const Span<const u8> replacementBinary,
    const Span<const u8> fontBytes,
    const u32 sourceGlyphCount
){
    if(!VerifyFontAtlasAllocationUnwindContracts())
        return false;

    const Name baselinePath("project/fonts/existing_atlas");
    const usize expectedGlyphCount = baselinePayload.glyphs.size();
    const usize expectedGroupCount = baselinePayload.groups.size();
    const usize expectedTableCount = baselinePayload.positioningTables.size();
    const f32 expectedAscenderUnits = baselinePayload.ascenderUnits;

    b8 completedLoad = false;
    for(usize failAt = 1u; failAt <= 256u; ++failAt){
        Tests::NthFailArena arena(Name("tests/assets_font_atlas/nth_fail"), Name("tests/assets_font_atlas/nth_fail_backing"), true);
        Impl::FontAtlas atlas(arena, baselinePath);
        atlas.setPayload(baselinePayload);
        atlas.markPayloadValidated();

        arena.arm(failAt);
        b8 threw = false;
        b8 loaded = false;
        try{
            loaded = atlas.loadBinary(replacementBinary);
        }
        catch(const AllocationException&){
            threw = true;
        }
        const b8 hit = arena.hit();
        arena.disarm();

        if(threw){
            if(!hit || loaded || !atlas.isPayloadValidated()
                || atlas.payload().ascenderUnits != expectedAscenderUnits
                || atlas.payload().glyphs.size() != expectedGlyphCount
                || atlas.payload().groups.size() != expectedGroupCount
                || atlas.payload().positioningTables.size() != expectedTableCount)
            {
                ADD_FAILURE() << "loadBinary OOM clobbered state at failAt=" << failAt
                    << " hit=" << hit << " loaded=" << loaded
                    << " validated=" << atlas.isPayloadValidated()
                    << " ascender=" << atlas.payload().ascenderUnits
                    << " glyphs=" << atlas.payload().glyphs.size();
                return false;
            }
            continue;
        }
        if(!loaded || !atlas.isPayloadValidated()){
            ADD_FAILURE() << "loadBinary completion failed at failAt=" << failAt
                << " loaded=" << loaded << " validated=" << atlas.isPayloadValidated();
            return false;
        }
        completedLoad = true;
        break;
    }
    if(!completedLoad){
        ADD_FAILURE() << "loadBinary OOM sweep exhausted limit";
        return false;
    }

    b8 completedSerialize = false;
    Core::Alloc::GlobalArena sourceArena(Name("tests/assets_font_atlas/unwind_source"));
    Impl::FontAtlas sourceAtlas(sourceArena, baselinePath);
    sourceAtlas.setPayload(baselinePayload);
    sourceAtlas.markPayloadValidated();

    for(usize failAt = 1u; failAt <= 256u; ++failAt){
        Tests::NthFailArena arena(Name("tests/assets_font_atlas/nth_fail"), Name("tests/assets_font_atlas/nth_fail_backing"), true);
        Vector<u8, Tests::NthFailArena> serialized(arena);
        arena.arm(failAt);
        b8 threw = false;
        b8 ok = false;
        try{
            ok = Impl::SerializeFontAtlasAsset(sourceAtlas, serialized);
        }
        catch(const AllocationException&){
            threw = true;
        }
        const b8 hit = arena.hit();
        arena.disarm();

        if(threw){
            if(!hit || ok){
                ADD_FAILURE() << "SerializeFontAtlasAsset OOM invalid state at failAt=" << failAt;
                return false;
            }
            continue;
        }
        if(!ok || serialized.empty()){
            ADD_FAILURE() << "SerializeFontAtlasAsset failed at failAt=" << failAt;
            return false;
        }
        completedSerialize = true;
        break;
    }
    if(!completedSerialize){
        ADD_FAILURE() << "SerializeFontAtlasAsset OOM sweep exhausted limit";
        return false;
    }

    b8 completedCopyTables = false;
    for(usize failAt = 1u; failAt <= 128u; ++failAt){
        Tests::NthFailArena arena(Name("tests/assets_font_atlas/nth_fail"), Name("tests/assets_font_atlas/nth_fail_backing"), true);
        arena.arm(failAt);
        b8 threw = false;
        Expected<Vector<Impl::FontAtlasPositioningTable, ArenaBase>> copied = MakeUnexpected(Failure{});
        try{
            copied = Impl::CopyFontAtlasPositioningTables(arena, fontBytes, 0u, sourceGlyphCount);
        }
        catch(const AllocationException&){
            threw = true;
        }
        const b8 hit = arena.hit();
        arena.disarm();

        if(threw){
            if(!hit){
                ADD_FAILURE() << "CopyFontAtlasPositioningTables threw without arena hit at failAt=" << failAt;
                return false;
            }
            continue;
        }
        if(!copied.has_value()){
            ADD_FAILURE() << "CopyFontAtlasPositioningTables failed without throw at failAt=" << failAt;
            return false;
        }
        completedCopyTables = true;
        break;
    }
    if(!completedCopyTables){
        ADD_FAILURE() << "CopyFontAtlasPositioningTables OOM sweep exhausted limit";
        return false;
    }

    b8 completedBuildAsset = false;
    for(usize failAt = 1u; failAt <= 256u; ++failAt){
        Tests::NthFailArena arena(Name("tests/assets_font_atlas/nth_fail"), Name("tests/assets_font_atlas/nth_fail_backing"), true);
        Impl::FontAtlasCookEntry entry(sourceArena);
        entry.virtualPath = baselinePath;
        entry.payload = baselinePayload;
        entry.payloadValidated = false;

        arena.arm(failAt);
        b8 threw = false;
        Expected<Impl::FontAtlas> built = MakeUnexpected(Failure{});
        try{
            built = Impl::BuildFontAtlasAsset(arena, entry);
        }
        catch(const AllocationException&){
            threw = true;
        }
        const b8 hit = arena.hit();
        arena.disarm();

        if(threw){
            if(!hit){
                ADD_FAILURE() << "BuildFontAtlasAsset threw without arena hit at failAt=" << failAt;
                return false;
            }
            continue;
        }
        if(!built.has_value() || !built.value().isPayloadValidated()){
            ADD_FAILURE() << "BuildFontAtlasAsset failed at failAt=" << failAt;
            return false;
        }
        completedBuildAsset = true;
        break;
    }
    if(!completedBuildAsset)
        ADD_FAILURE() << "BuildFontAtlasAsset OOM sweep exhausted limit";
    return completedBuildAsset;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};
NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

