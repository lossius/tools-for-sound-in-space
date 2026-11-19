# SPDX-FileCopyrightText: 2026 Trond Lossius
# SPDX-License-Identifier: AGPL-3.0-or-later

# What every plugin of this repository needs besides its juce_add_plugin() call: the shared libraries,
# the JUCE definitions, the IDE grouping and the copy of the built plug-in into the host's folder.
#
# Usage, in the plugin's CMakeLists.txt:
#     juce_add_plugin(MyPlugin ... FORMATS ${TLOS_PLUGIN_FORMATS} ...)
#     target_sources(MyPlugin PRIVATE ...)
#     tlos_finish_plugin(MyPlugin)

# The formats to build: VST3 and Standalone everywhere, AudioUnit on macOS, AAX if enabled.
set(TLOS_PLUGIN_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND TLOS_PLUGIN_FORMATS AU)
endif()

if(TLOS_ENABLE_AAX)
    # JUCE (since 9.0.6) bundles the AAX SDK (offered under Avid's licence or GPLv3); it is used automatically, no path is needed.
    list(APPEND TLOS_PLUGIN_FORMATS AAX)
endif()

# JUCE's module sources are compiled into our targets (they are not separate Xcode targets). Put
# them all in a "JUCE" group, with one subgroup per module and "src" (the compiled .mm/.cpp/.c units)
# and "include" (the headers, only listed so that the IDE shows them; they are not compiled).
set(TLOS_JUCE_MODULES
    juce_audio_basics juce_audio_devices juce_audio_formats juce_audio_plugin_client
    juce_audio_processors juce_audio_processors_headless juce_audio_utils juce_core
    juce_data_structures juce_events juce_graphics juce_gui_basics juce_gui_extra juce_osc)

# Adds a post-build step to the format target <target>_<format> that replaces <product><extension> in
# <directory> with the built bundle.
#
# On macOS the copy is signed ad hoc: this step can run before Xcode has signed the bundle, and an unsigned
# bundle cannot be loaded on Apple silicon (auval and hosts then fail to open an AudioUnit). Release builds are
# signed with a Developer ID by scripts/sign-and-notarize.sh, which does not use the copy.
function(tlos_install_after_build target format extension directory)
    set(bundle "$<GENEX_EVAL:$<TARGET_PROPERTY:${target}_${format},JUCE_PLUGIN_ARTEFACT_FILE>>")
    set(destination "${directory}/$<TARGET_PROPERTY:${target},JUCE_PRODUCT_NAME>${extension}")
    set(sign_command ${CMAKE_COMMAND} -E true)
    if(APPLE)
        set(sign_command codesign --force --sign - "${destination}")
    endif()
    add_custom_command(TARGET ${target}_${format} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${directory}"
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${destination}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${bundle}" "${destination}"
        COMMAND ${sign_command}
        COMMENT "Copying ${target}${extension} to ${directory}"
        VERBATIM)
endfunction()

# Call after juce_add_plugin() and target_sources().
function(tlos_finish_plugin target)
    target_compile_definitions(${target} PUBLIC JUCE_VST3_CAN_REPLACE_VST2=0)

    target_link_libraries(${target}
        PRIVATE
            juce::juce_audio_utils
        PUBLIC
            TlosUI
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)

    # IDE: group the JUCE module files. Later groups win over earlier ones; groups with FILES win over regexes.
    source_group("JUCE" REGULAR_EXPRESSION "/JUCE/")
    foreach(module_name IN LISTS TLOS_JUCE_MODULES)
        set(module_dir "${CMAKE_SOURCE_DIR}/JUCE/modules/${module_name}")
        source_group("JUCE/${module_name}/src" REGULAR_EXPRESSION "/JUCE/modules/${module_name}/")
        file(GLOB_RECURSE module_headers CONFIGURE_DEPENDS "${module_dir}/*.h" "${module_dir}/*.hpp")
        target_sources(${target} PRIVATE ${module_headers})
        source_group(TREE "${module_dir}" PREFIX "JUCE/${module_name}/include" FILES ${module_headers})
    endforeach()
    source_group("Tests" REGULAR_EXPRESSION "/tests/")

    if(TLOS_INSTALL_VST3)
        tlos_install_after_build(${target} VST3 ".vst3" "${TLOS_VST3_INSTALL_DIR}")
    endif()
    if(APPLE AND TLOS_INSTALL_AU)
        tlos_install_after_build(${target} AU ".component" "${TLOS_AU_INSTALL_DIR}")
    endif()
    if(TLOS_ENABLE_AAX AND TLOS_INSTALL_AAX)
        tlos_install_after_build(${target} AAX ".aaxplugin" "${TLOS_AAX_INSTALL_DIR}")
    endif()
endfunction()
