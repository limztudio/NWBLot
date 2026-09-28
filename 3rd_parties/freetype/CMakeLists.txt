set(_nwb_ft_config_dir "${CMAKE_CURRENT_BINARY_DIR}/include/freetype/config")
file(MAKE_DIRECTORY "${_nwb_ft_config_dir}")
file(READ "${CMAKE_CURRENT_LIST_DIR}/include/freetype/config/ftoption.h" _nwb_ft_options)
foreach(_nwb_ft_disabled_option USE_LZW USE_ZLIB USE_BZIP2 USE_PNG USE_HARFBUZZ USE_HARFBUZZ_DYNAMIC USE_BROTLI)
    string(REGEX REPLACE "#define FT_CONFIG_OPTION_${_nwb_ft_disabled_option}([ \t]*[\r\n])" "#undef FT_CONFIG_OPTION_${_nwb_ft_disabled_option}\\1" _nwb_ft_options "${_nwb_ft_options}")
endforeach()
file(WRITE "${_nwb_ft_config_dir}/ftoption.h" "${_nwb_ft_options}")
file(WRITE "${_nwb_ft_config_dir}/ftmodule.h"
    "FT_USE_MODULE(FT_Module_Class, autofit_module_class)\n"
    "FT_USE_MODULE(FT_Driver_ClassRec, tt_driver_class)\n"
    "FT_USE_MODULE(FT_Driver_ClassRec, cff_driver_class)\n"
    "FT_USE_MODULE(FT_Module_Class, psaux_module_class)\n"
    "FT_USE_MODULE(FT_Module_Class, psnames_module_class)\n"
    "FT_USE_MODULE(FT_Module_Class, pshinter_module_class)\n"
    "FT_USE_MODULE(FT_Module_Class, sfnt_module_class)\n"
    "FT_USE_MODULE(FT_Renderer_Class, ft_smooth_renderer_class)\n"
    "FT_USE_MODULE(FT_Renderer_Class, ft_raster1_renderer_class)\n"
)

add_library(nwb_vendor_freetype STATIC
    src/autofit/autofit.c
    src/base/ftbase.c
    src/base/ftbbox.c
    src/base/ftbitmap.c
    src/base/ftdebug.c
    src/base/ftglyph.c
    src/base/ftinit.c
    src/base/ftmm.c
    src/base/ftstroke.c
    src/base/ftsynth.c
    src/base/ftsystem.c
    src/cff/cff.c
    src/psaux/psaux.c
    src/pshinter/pshinter.c
    src/psnames/psnames.c
    src/raster/raster.c
    src/sfnt/sfnt.c
    src/smooth/smooth.c
    src/truetype/truetype.c
)
add_library(nwb::freetype ALIAS nwb_vendor_freetype)
target_include_directories(nwb_vendor_freetype SYSTEM PUBLIC
    "${CMAKE_CURRENT_BINARY_DIR}/include"
    "${CMAKE_CURRENT_LIST_DIR}/include"
)
target_compile_definitions(nwb_vendor_freetype PRIVATE FT2_BUILD_LIBRARY)
nwb_disable_vendor_warnings(nwb_vendor_freetype)
nwb_apply_debug_symbols(nwb_vendor_freetype)
