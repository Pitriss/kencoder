# All GUI components use ONE gettext source catalog and the same locale QM files.
# .po files are the only manually translated sources. .ts is generated at build.
option(KENCODER_REQUIRE_TRANSLATIONS "Require Qt Linguist tools for the GUI translations" OFF)
find_program(KENCODER_LCONVERT NAMES lconvert HINTS /usr/lib/qt6/bin)
find_program(KENCODER_LRELEASE NAMES lrelease HINTS /usr/lib/qt6/bin)
if(NOT KENCODER_LCONVERT OR NOT KENCODER_LRELEASE)
    if(KENCODER_REQUIRE_TRANSLATIONS)
        message(FATAL_ERROR "KEncoder translations require qt6-l10n-tools (lconvert and lrelease)")
    endif()
    message(WARNING "Qt Linguist tools missing: GUI translations will not be installed")
    return()
endif()
set(_kencoder_languages en sk uk de ru pl)
set(_kencoder_qms)
foreach(lang IN LISTS _kencoder_languages)
    set(po "${CMAKE_CURRENT_SOURCE_DIR}/${lang}.po")
    set(ts "${CMAKE_CURRENT_BINARY_DIR}/kencoder_${lang}.ts")
    set(qtpo "${CMAKE_CURRENT_BINARY_DIR}/kencoder_${lang}.qt.po")
    set(qm "${CMAKE_CURRENT_BINARY_DIR}/kencoder_${lang}.qm")
    add_custom_command(
        OUTPUT "${qm}"
        # Qt lconvert requires X-Qt-Contexts: true and Context| in msgctxt.
        # Convert a temporary copy, never the translator-edited source PO.
        COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/po-contexts.sh" to-qt "${po}" "${qtpo}"
        COMMAND "${KENCODER_LCONVERT}" -i "${qtpo}" -if po -of ts
                -source-language cs -target-language "${lang}" -o "${ts}"
        COMMAND "${KENCODER_LRELEASE}" "${ts}" -qm "${qm}"
        DEPENDS "${po}" "${CMAKE_CURRENT_SOURCE_DIR}/po-contexts.sh"
        COMMENT "Compiling KEncoder ${lang} translation from the shared POT catalog"
        VERBATIM)
    list(APPEND _kencoder_qms "${qm}")
endforeach()
add_custom_target(kencoder_translations ALL DEPENDS ${_kencoder_qms})
# The no-HID regression suite builds specific targets, not the default ALL
# target. Those builds must also compile every QM file before cmake --install.
# Otherwise staging fails on a missing kencoder_<lang>.qm.
foreach(_gui_target IN ITEMS kencoder kcm_kencoder kcm_kencoder_notify)
    if(TARGET ${_gui_target})
        add_dependencies(${_gui_target} kencoder_translations)
    endif()
endforeach()
install(FILES ${_kencoder_qms} DESTINATION "${CMAKE_INSTALL_DATADIR}/kencoder/translations")

# Runtime smoke test for the actual QTranslator loader and a QML qsTr() context.
# The Debian package builder copies translations/ but intentionally omits tests/.
# Keep the test target in complete checkouts, without making package builds
# depend on a test-only source file. The target remains EXCLUDE_FROM_ALL.
find_package(Qt6 6.4 QUIET COMPONENTS Qml)
if(TARGET Qt6::Qml AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../tests/test-translations-runtime.cpp")
    add_executable(kencoder_translation_loader_test EXCLUDE_FROM_ALL
        "${CMAKE_CURRENT_SOURCE_DIR}/../gui/translation_loader.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../tests/test-translations-runtime.cpp"
    )
    target_link_libraries(kencoder_translation_loader_test PRIVATE Qt6::Core Qt6::Qml)
endif()
