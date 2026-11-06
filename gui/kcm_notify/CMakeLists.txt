# Optional Plasma 6 KCM for notification rules and preset editor.
# Keep KF6 dependencies inside this plugin, not the Qt Widgets executable.
set(BUILD_SHARED_LIBS ON)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/lib")
kcmutils_add_qml_kcm(kcm_kencoder_notify)
set_target_properties(kcm_kencoder_notify PROPERTIES PREFIX "" AUTOMOC ON)
target_sources(kcm_kencoder_notify PRIVATE
    kcm_kencoder_notify.cpp
    "${CMAKE_CURRENT_SOURCE_DIR}/../../notify/config.cpp"
)
target_link_libraries(kcm_kencoder_notify PRIVATE
    Qt6::Quick
    KF6::CoreAddons
    KF6::KCMUtils
    KF6::KCMUtilsQuick
)
