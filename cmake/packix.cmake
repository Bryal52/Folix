# Packix (third_party/Packix, git submodule): the archive engine and its windows, built as two
# static libraries. Only the library parts are used; Packix's own app, tests and tools are not built.
# Packix's log is replaced by src/PackixLogBridge.cpp so its errors land in Folix's log.

set(PACKIX_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/Packix)
if(NOT EXISTS ${PACKIX_DIR}/core/Archiver.h)
    message(FATAL_ERROR "Packix is missing: run `git submodule update --init`")
endif()

add_library(packix_core STATIC
    ${PACKIX_DIR}/core/Types.cpp ${PACKIX_DIR}/core/Types.h
    ${PACKIX_DIR}/core/Shared.cpp ${PACKIX_DIR}/core/Shared.h
    ${PACKIX_DIR}/core/Formats.cpp ${PACKIX_DIR}/core/Formats.h
    ${PACKIX_DIR}/core/Volumes.cpp ${PACKIX_DIR}/core/Volumes.h
    ${PACKIX_DIR}/core/Names.cpp ${PACKIX_DIR}/core/Names.h
    ${PACKIX_DIR}/core/Engine.h
    ${PACKIX_DIR}/core/SevenZipEngine.cpp ${PACKIX_DIR}/core/SevenZipEngine.h
    ${PACKIX_DIR}/core/Archiver.cpp ${PACKIX_DIR}/core/Archiver.h
    ${PACKIX_DIR}/core/Temp.cpp ${PACKIX_DIR}/core/Temp.h
)
target_include_directories(packix_core PUBLIC ${PACKIX_DIR}/core)
target_link_libraries(packix_core PUBLIC Qt6::Core)
# Builds run from the build folder find shared/ in the submodule.
target_compile_definitions(packix_core PRIVATE PACKIX_SOURCE_SHARED="${PACKIX_DIR}/shared")

set(PACKIX_UI_SOURCES
    ${PACKIX_DIR}/dialogs/Lang.cpp ${PACKIX_DIR}/dialogs/Lang.h
    ${PACKIX_DIR}/dialogs/Theme.cpp ${PACKIX_DIR}/dialogs/Theme.h
    ${PACKIX_DIR}/dialogs/Icons.cpp ${PACKIX_DIR}/dialogs/Icons.h
    ${PACKIX_DIR}/dialogs/Log.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/PackixLogBridge.cpp
    ${PACKIX_DIR}/dialogs/LogViewer.cpp ${PACKIX_DIR}/dialogs/LogViewer.h
    ${PACKIX_DIR}/dialogs/Effects.cpp ${PACKIX_DIR}/dialogs/Effects.h
    ${PACKIX_DIR}/dialogs/Thermometer.cpp ${PACKIX_DIR}/dialogs/Thermometer.h
    ${PACKIX_DIR}/dialogs/PathField.cpp ${PACKIX_DIR}/dialogs/PathField.h
    ${PACKIX_DIR}/dialogs/Dialogs.cpp ${PACKIX_DIR}/dialogs/Dialogs.h
    ${PACKIX_DIR}/dialogs/Jobs.cpp ${PACKIX_DIR}/dialogs/Jobs.h
    ${PACKIX_DIR}/dialogs/CreateDialog.cpp ${PACKIX_DIR}/dialogs/CreateDialog.h
    ${PACKIX_DIR}/dialogs/ExtractDialog.cpp ${PACKIX_DIR}/dialogs/ExtractDialog.h
    ${PACKIX_DIR}/dialogs/Actions.cpp ${PACKIX_DIR}/dialogs/Actions.h
    ${PACKIX_DIR}/platform/Platform.h
)
if(APPLE)
    list(APPEND PACKIX_UI_SOURCES ${PACKIX_DIR}/platform/macos/Platform_mac.mm)
    set_source_files_properties(${PACKIX_DIR}/platform/macos/Platform_mac.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
elseif(WIN32)
    list(APPEND PACKIX_UI_SOURCES ${PACKIX_DIR}/platform/windows/Platform_win.cpp)
endif()
add_library(packix_ui STATIC ${PACKIX_UI_SOURCES})
target_include_directories(packix_ui PUBLIC ${PACKIX_DIR}/dialogs ${PACKIX_DIR}/platform)
target_link_libraries(packix_ui PUBLIC packix_core Qt6::Widgets Qt6::Concurrent)
if(APPLE)
    target_link_libraries(packix_ui PUBLIC
        "-framework AppKit" "-framework Security" "-framework UniformTypeIdentifiers" "-framework UserNotifications")
elseif(WIN32)
    target_link_libraries(packix_ui PUBLIC shell32 ole32 advapi32 crypt32)
endif()

# Runtime files for Folix: Packix's data (formats, theme, texts) and the 7-Zip engine with its license.
function(folix_bundle_packix target)
    if(APPLE)
        find_program(SEVENZIP 7zz HINTS /opt/homebrew/bin /usr/local/bin REQUIRED)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory ${PACKIX_DIR}/shared
                    $<TARGET_BUNDLE_CONTENT_DIR:${target}>/Resources/shared
            COMMAND ${CMAKE_COMMAND} -E copy ${PACKIX_DIR}/resources/7zip-License.txt
                    $<TARGET_BUNDLE_CONTENT_DIR:${target}>/Resources/7zip-License.txt
            COMMAND ${CMAKE_COMMAND} -E copy ${SEVENZIP} $<TARGET_BUNDLE_CONTENT_DIR:${target}>/MacOS/7zz)
    else()
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory ${PACKIX_DIR}/shared $<TARGET_FILE_DIR:${target}>/shared
            COMMAND ${CMAKE_COMMAND} -E copy ${PACKIX_DIR}/resources/7zip-License.txt
                    $<TARGET_FILE_DIR:${target}>/7zip-License.txt)
    endif()
endfunction()
