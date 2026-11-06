cmake_minimum_required(VERSION 3.16)
project(kencoder-notify VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.4 REQUIRED COMPONENTS Core DBus)
add_executable(kencoder-notify main.cpp config.cpp session_policy.cpp)
target_link_libraries(kencoder-notify PRIVATE Qt6::Core Qt6::DBus)
include(GNUInstallDirs)
install(TARGETS kencoder-notify RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
# Start once per Plasma session, even when notification listening itself is off.
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../packaging/kencoder-notify-autostart.desktop"
        DESTINATION "/etc/xdg/autostart")
