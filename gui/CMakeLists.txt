cmake_minimum_required(VERSION 3.16)
project(kencoder VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets)
add_executable(kencoder main.cpp resources.qrc)
target_link_libraries(kencoder PRIVATE Qt6::Widgets)

include(GNUInstallDirs)
install(TARGETS kencoder RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../packaging/kencoder.desktop"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")

install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../packaging/kencoder.png"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps")
