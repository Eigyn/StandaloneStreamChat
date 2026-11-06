# Compiles the PNGs in `asset_dir` into `target`: each becomes
# <build>/embedded/<file name>.inc, its bytes as a comma-separated list to
# #include inside an array initializer. This stands in for C23 #embed, which
# the NDK 27 compiler used by the 1.40.8 build doesn't have.
function(embed_assets target asset_dir)
    set(out_dir ${CMAKE_CURRENT_BINARY_DIR}/embedded)
    file(GLOB assets ${asset_dir}/*.png)
    foreach(asset ${assets})
        get_filename_component(name ${asset} NAME)
        file(READ ${asset} hex HEX)
        string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
        # Only touches the .inc when the bytes changed, so a re-configure
        # doesn't rebuild everything that includes it.
        file(WRITE ${out_dir}/${name}.inc.tmp "${bytes}\n")
        configure_file(${out_dir}/${name}.inc.tmp ${out_dir}/${name}.inc COPYONLY)
        # Re-run CMake when an asset changes.
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${asset})
    endforeach()
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
endfunction()
