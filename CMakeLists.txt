cmake_minimum_required(VERSION 3.21)
project(Folix VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent Network Multimedia MultimediaWidgets)
find_package(CURL REQUIRED)

# Icon: logo.png -> Folix.icns (transparent corners, all sizes)
add_executable(makeicon tools/makeicon.cpp)
target_link_libraries(makeicon PRIVATE Qt6::Gui)

set(FOLIX_ICNS ${CMAKE_CURRENT_BINARY_DIR}/Folix.icns)
add_custom_command(
    OUTPUT ${FOLIX_ICNS}
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/tools/make_icns.sh $<TARGET_FILE:makeicon>
            ${CMAKE_CURRENT_SOURCE_DIR}/resources/logo.png ${FOLIX_ICNS}
    DEPENDS makeicon resources/logo.png tools/make_icns.sh
    COMMENT "Building Folix.icns")
set_source_files_properties(${FOLIX_ICNS} PROPERTIES MACOSX_PACKAGE_LOCATION Resources GENERATED TRUE)

add_executable(Folix MACOSX_BUNDLE
    src/main.cpp
    src/Backend.cpp src/Backend.h
    src/Tools.cpp src/Tools.h
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
    src/SecureStore.cpp src/SecureStore.h
    src/SizeIndex.cpp src/SizeIndex.h
    src/InfoDialog.cpp src/InfoDialog.h
    src/AutoOpen.cpp src/AutoOpen.h
    ${FOLIX_ICNS}
)

target_link_libraries(Folix PRIVATE
    Qt6::Widgets Qt6::Concurrent Qt6::Network Qt6::Multimedia Qt6::MultimediaWidgets CURL::libcurl
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
