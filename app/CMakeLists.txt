find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Widgets Test)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Widgets Network Test)
if(QT_VERSION_MAJOR EQUAL 5)
    message(STATUS "Using existing Qt 5 development environment; Qt 6 is preferred for distribution")
endif()
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
get_target_property(CDNG_QMAKE Qt${QT_VERSION_MAJOR}::qmake IMPORTED_LOCATION)
execute_process(COMMAND "${CDNG_QMAKE}" -query QT_INSTALL_TRANSLATIONS OUTPUT_VARIABLE CDNG_QT_TRANSLATIONS OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT EXISTS "${CDNG_QT_TRANSLATIONS}/qtbase_es.qm")
    message(FATAL_ERROR "Qt Spanish base translations are required for bilingual dialogs")
endif()
configure_file(i18n.qrc.in "${CMAKE_CURRENT_BINARY_DIR}/i18n.qrc" @ONLY)
add_library(cdng_i18n STATIC i18n.cpp "${CMAKE_CURRENT_BINARY_DIR}/i18n.qrc")
target_link_libraries(cdng_i18n PUBLIC Qt${QT_VERSION_MAJOR}::Core)
target_include_directories(cdng_i18n PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

add_library(cdng_profiles STATIC profile_store.cpp profiles.qrc)
target_compile_features(cdng_profiles PUBLIC cxx_std_17)
target_include_directories(cdng_profiles PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(cdng_profiles PUBLIC cdng_i18n Qt${QT_VERSION_MAJOR}::Core)

add_executable(cdng_profile_helper profile_helper.cpp)
target_link_libraries(cdng_profile_helper PRIVATE cdng_profiles)
set_target_properties(cdng_profile_helper PROPERTIES OUTPUT_NAME cdng-profile-helper)

add_library(cdng_conversion STATIC conversion_job.cpp)
target_link_libraries(cdng_conversion PUBLIC cdng_profiles)
add_library(cdng_preview STATIC preview_service.cpp)
target_link_libraries(cdng_preview PUBLIC cdng_profiles Qt${QT_VERSION_MAJOR}::Widgets)
add_library(cdng_noise_dialog STATIC noise_dialog.cpp)
target_link_libraries(cdng_noise_dialog PUBLIC cdng_preview Qt${QT_VERSION_MAJOR}::Widgets)
set(CDNG_APP_ICON ${CMAKE_CURRENT_SOURCE_DIR}/icon/AppIcon.icns)
set_source_files_properties(${CDNG_APP_ICON} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
# En Windows el icono y los datos de versión del ejecutable van en un .rc (equivalente del Info.plist de macOS).
set(CDNG_APP_RESOURCES "")
if(WIN32)
    # sdk_probe_win.cpp: comprobación de que el decodificador BRAW de Resolve lee el perfil (--probe-camera-support).
    set(CDNG_APP_RESOURCES ${CMAKE_CURRENT_SOURCE_DIR}/cdng2braw.rc ${CMAKE_CURRENT_SOURCE_DIR}/sdk_probe_win.cpp)
endif()
add_executable(cdng_app WIN32 MACOSX_BUNDLE main.cpp profile_window.cpp converter_window.cpp automation_server.cpp ${CDNG_APP_ICON} ${CDNG_APP_RESOURCES})
target_link_libraries(cdng_app PRIVATE cdng_profiles cdng_conversion cdng_preview cdng_noise_dialog Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Network)
set_target_properties(cdng_app PROPERTIES
    OUTPUT_NAME "cdng2braw"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.elcacharrista.cdng2braw"
    MACOSX_BUNDLE_ICON_FILE "AppIcon.icns"
    MACOSX_BUNDLE_BUNDLE_NAME "CDNG to BRAW Converter"
    MACOSX_BUNDLE_BUNDLE_VERSION "1.0.1"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0.1")
add_dependencies(cdng_app cdng_profile_helper)
if(APPLE)
    add_custom_command(TARGET cdng_app POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:cdng_profile_helper>"
                "$<TARGET_FILE_DIR:cdng_app>/$<TARGET_FILE_NAME:cdng_profile_helper>")
endif()
if(MSVC)
    # /utf-8 se aplica ya a todos los objetivos desde el CMakeLists raíz (las claves de traducción lo necesitan).
    # Aquí solo el manifiesto de la página de códigos, que debe ir en cada ejecutable. Ver packaging/utf8.manifest.
    foreach(executable cdng_app cdng_profile_helper)
        target_link_options(${executable} PRIVATE "/MANIFEST:EMBED"
            "/MANIFESTINPUT:${CMAKE_SOURCE_DIR}/packaging/utf8.manifest")
    endforeach()
endif()

if(BUILD_TESTING)
    # En Windows las pruebas se enlazan con Qt dinámicamente y no pasan por windeployqt: necesitan la carpeta
    # bin de Qt en el PATH o arrancan con 0xc0000135 (DLL no encontrada). ENVIRONMENT_MODIFICATION existe
    # desde CMake 3.22; con una anterior hace falta el PATH ya puesto (build.ps1 lo hace).
    function(cdng_test_needs_qt name)
        if(WIN32 AND NOT CMAKE_VERSION VERSION_LESS 3.22)
            execute_process(COMMAND "${CDNG_QMAKE}" -query QT_INSTALL_BINS
                OUTPUT_VARIABLE qt_bins OUTPUT_STRIP_TRAILING_WHITESPACE)
            set_tests_properties(${name} PROPERTIES
                ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${qt_bins}")
        endif()
    endfunction()
    add_executable(cdng_i18n_tests tests/i18n_test.cpp)
    target_link_libraries(cdng_i18n_tests PRIVATE cdng_i18n Qt${QT_VERSION_MAJOR}::Test)
    add_test(NAME i18n COMMAND cdng_i18n_tests)
    add_executable(cdng_profile_tests tests/profile_store_test.cpp)
    target_link_libraries(cdng_profile_tests PRIVATE cdng_profiles Qt${QT_VERSION_MAJOR}::Test)
    add_test(NAME profile_store COMMAND cdng_profile_tests)
    add_test(NAME profile_app_status COMMAND cdng_app --profile-status)
    add_executable(cdng_profile_window_tests tests/profile_window_test.cpp profile_window.cpp)
    target_link_libraries(cdng_profile_window_tests PRIVATE cdng_profiles Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Test)
    add_test(NAME profile_window COMMAND cdng_profile_window_tests)
    set_tests_properties(profile_window PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_executable(cdng_converter_window_tests tests/converter_window_test.cpp converter_window.cpp profile_window.cpp)
    target_link_libraries(cdng_converter_window_tests PRIVATE cdng_conversion cdng_preview cdng_noise_dialog Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Test)
    add_test(NAME converter_window COMMAND cdng_converter_window_tests)
    set_tests_properties(converter_window PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    foreach(qt_test i18n profile_store profile_app_status profile_window converter_window)
        cdng_test_needs_qt(${qt_test})
    endforeach()
endif()
