cmake_minimum_required(VERSION 3.21)
project(Folix VERSION 1.1.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

if(APPLE)
    enable_language(OBJCXX)   # Packix's macOS layer is Objective-C++
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent Network Multimedia MultimediaWidgets)
find_package(CURL REQUIRED)

include(cmake/packix.cmake)

# App icon from logo.png (transparent corners, every size)
add_executable(makeicon tools/makeicon.cpp)
target_link_libraries(makeicon PRIVATE Qt6::Gui)

set(FOLIX_SOURCES
    src/main.cpp
    src/Backend.cpp src/Backend.h
    src/Tools.cpp src/Tools.h
    src/Platform.cpp src/Platform.h
    src/LocalBackend.cpp src/LocalBackend.h
    src/AdbBackend.cpp src/AdbBackend.h
    src/FtpBackend.cpp src/FtpBackend.h
    src/Devices.cpp src/Devices.h
    src/FileTypes.cpp src/FileTypes.h
    src/FileModel.cpp src/FileModel.h
    src/Protection.cpp src/Protection.h
    src/Dialogs.cpp src/Dialogs.h
    src/JobDialog.cpp src/JobDialog.h
    src/Operations.cpp src/Operations.h
    src/Preview.cpp src/Preview.h
    src/Viewer.cpp src/Viewer.h
    src/ConnectionDialog.cpp src/ConnectionDialog.h
    src/FilePanel.cpp src/FilePanel.h
    src/MainWindow.cpp src/MainWindow.h
    src/SecureStore.h
    src/SizeIndex.cpp src/SizeIndex.h
    src/InfoDialog.cpp src/InfoDialog.h
    src/AutoOpen.cpp src/AutoOpen.h
    src/FullDiskAccess.cpp src/FullDiskAccess.h
    src/Log.cpp src/Log.h
    src/LogViewer.cpp src/LogViewer.h
    src/Archives.cpp src/Archives.h
    src/Donate.cpp src/Donate.h
    src/ArchiveBackend.cpp src/ArchiveBackend.h
    resources/folix.qrc
)

if(APPLE)
    set(FOLIX_ICNS ${CMAKE_CURRENT_BINARY_DIR}/Folix.icns)
    add_custom_command(
        OUTPUT ${FOLIX_ICNS}
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/tools/make_icns.sh $<TARGET_FILE:makeicon>
                ${CMAKE_CURRENT_SOURCE_DIR}/resources/logo.png ${FOLIX_ICNS}
        DEPENDS makeicon resources/logo.png tools/make_icns.sh
        COMMENT "Building Folix.icns")
    set_source_files_properties(${FOLIX_ICNS} PROPERTIES MACOSX_PACKAGE_LOCATION Resources GENERATED TRUE)

    add_executable(Folix MACOSX_BUNDLE ${FOLIX_SOURCES} src/SecureStore.cpp ${FOLIX_ICNS})
    target_link_libraries(Folix PRIVATE
        "-framework Security" "-framework CoreFoundation" "-framework CoreServices")
    set_target_properties(Folix PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/resources/Info.plist.in
        MACOSX_BUNDLE_BUNDLE_NAME Folix
        MACOSX_BUNDLE_GUI_IDENTIFIER com.umid.folix
        MACOSX_BUNDLE_ICON_FILE Folix.icns
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION})

    # Background helper that opens newly connected drives in Folix (no Qt, stays tiny)
    add_executable(FolixAgent agent/FolixAgent.cpp)
    target_link_libraries(FolixAgent PRIVATE "-framework CoreFoundation" "-framework DiskArbitration")
    add_dependencies(Folix FolixAgent)
    add_custom_command(TARGET Folix POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:FolixAgent> $<TARGET_BUNDLE_CONTENT_DIR:Folix>/MacOS/FolixAgent)
elseif(WIN32)
    set(FOLIX_ICO ${CMAKE_CURRENT_BINARY_DIR}/folix.ico)
    add_custom_command(
        OUTPUT ${FOLIX_ICO}
        COMMAND $<TARGET_FILE:makeicon> ${CMAKE_CURRENT_SOURCE_DIR}/resources/logo.png
                ${CMAKE_CURRENT_BINARY_DIR}/folix-1024.png ${FOLIX_ICO}
        DEPENDS makeicon resources/logo.png
        COMMENT "Building folix.ico")
    configure_file(resources/folix.rc.in ${CMAKE_CURRENT_BINARY_DIR}/folix.rc @ONLY)
    set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/folix.rc PROPERTIES OBJECT_DEPENDS ${FOLIX_ICO})

    add_executable(Folix WIN32 ${FOLIX_SOURCES} src/SecureStore_win.cpp ${CMAKE_CURRENT_BINARY_DIR}/folix.rc)
    target_link_libraries(Folix PRIVATE crypt32)
else()
    message(FATAL_ERROR "Folix is built for macOS and Windows")
endif()

target_compile_definitions(Folix PRIVATE FOLIX_VERSION="${PROJECT_VERSION}")
target_link_libraries(Folix PRIVATE
    Qt6::Widgets Qt6::Concurrent Qt6::Network Qt6::Multimedia Qt6::MultimediaWidgets CURL::libcurl packix_ui)
folix_bundle_packix(Folix)

# Archive tests (not part of the app): cmake --build build --target tst_archive && build/tst_archive
add_executable(tst_archive EXCLUDE_FROM_ALL tests/tst_archive.cpp
    src/ArchiveBackend.cpp src/Archives.cpp src/Backend.cpp src/LocalBackend.cpp src/Platform.cpp
    src/Operations.cpp src/JobDialog.cpp src/Tools.cpp src/Log.cpp)
target_include_directories(tst_archive PRIVATE src)
target_compile_definitions(tst_archive PRIVATE FOLIX_VERSION="${PROJECT_VERSION}")
target_link_libraries(tst_archive PRIVATE packix_ui Qt6::Network)
