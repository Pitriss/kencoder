cmake_minimum_required(VERSION 3.16)
project(kencoder VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets DBus)
add_executable(kencoder main.cpp resources.qrc translation_loader.cpp "${CMAKE_CURRENT_SOURCE_DIR}/../notify/config.cpp")
target_link_libraries(kencoder PRIVATE Qt6::Widgets Qt6::DBus)

include(GNUInstallDirs)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../translations" "${CMAKE_CURRENT_BINARY_DIR}/translations")
install(TARGETS kencoder RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../packaging/kencoder.desktop"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")

install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../packaging/kencoder.png"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps")

# Build KCM only when the KDE development stack is available. A standalone
# non-Plasma build still produces the unmodified Qt Widgets application.
option(KENCODER_BUILD_KCM "Build optional Plasma 6 Settings module" ON)
option(KENCODER_REQUIRE_KCM "Fail if the optional KCM cannot be built" OFF)
if(KENCODER_BUILD_KCM)
    find_package(ECM 6.0 QUIET NO_MODULE)
    if(ECM_FOUND)
        list(PREPEND CMAKE_MODULE_PATH ${ECM_MODULE_PATH})
        find_package(Qt6 6.4 QUIET COMPONENTS Quick Qml DBus)
        find_package(KF6 6.0 QUIET COMPONENTS CoreAddons KCMUtils)
    endif()
    if(TARGET Qt6::Quick AND TARGET Qt6::DBus AND TARGET KF6::CoreAddons AND
       TARGET KF6::KCMUtils AND TARGET KF6::KCMUtilsQuick)
        include(KDEInstallDirs)
        add_subdirectory(kcm)
        add_subdirectory(kcm_notify)
        message(STATUS "KEncoder: optional Plasma KCM enabled")
    elseif(KENCODER_REQUIRE_KCM)
        message(FATAL_ERROR "KEncoder: KCM requested but ECM, Qt Quick or KF6 KCMUtils are missing")
    else()
        message(STATUS "KEncoder: Plasma KCM skipped (optional build dependencies missing)")
    endif()
elseif(KENCODER_REQUIRE_KCM)
    message(FATAL_ERROR "KEncoder: KENCODER_BUILD_KCM=OFF conflicts with KENCODER_REQUIRE_KCM=ON")
endif()
