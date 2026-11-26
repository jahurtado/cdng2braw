add_library(cdng_native SHARED src/entropy.cpp)
target_compile_features(cdng_native PUBLIC cxx_std_17)
target_include_directories(cdng_native PUBLIC include)
target_compile_definitions(cdng_native PRIVATE CDNG_NATIVE_BUILD)
set_target_properties(cdng_native PROPERTIES
    CXX_EXTENSIONS OFF
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set_target_properties(cdng_native PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY_${config} "${CMAKE_BINARY_DIR}/lib"
        RUNTIME_OUTPUT_DIRECTORY_${config} "${CMAKE_BINARY_DIR}/lib")
endforeach()
if(MSVC)
    target_compile_options(cdng_native PRIVATE /W4 /permissive-)
else()
    target_compile_options(cdng_native PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(CDNG_SANITIZERS AND NOT MSVC)
    target_compile_options(cdng_native PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(cdng_native PRIVATE -fsanitize=address,undefined)
endif()

if(BUILD_TESTING)
    add_executable(cdng_native_tests tests/entropy_test.cpp)
    target_link_libraries(cdng_native_tests PRIVATE cdng_native)
    target_compile_features(cdng_native_tests PRIVATE cxx_std_17)
    set_target_properties(cdng_native_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
    foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
        set_target_properties(cdng_native_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${config} "${CMAKE_BINARY_DIR}/lib")
    endforeach()
    if(CDNG_SANITIZERS AND NOT MSVC)
        target_compile_options(cdng_native_tests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(cdng_native_tests PRIVATE -fsanitize=address,undefined)
    endif()
    add_test(NAME native_entropy COMMAND cdng_native_tests)
endif()

# Motor de conversión completo en C++ (sustituye al proceso Python de la app). Ver native/engine/README.md.
# LibRaw: en Windows por el paquete de CMake de vcpkg (no hay pkg-config con MSVC); en macOS y Linux por
# pkg-config, como hasta ahora, para no cambiar la construcción ya validada. En los dos casos se usa la
# variante reentrante (libraw_r), que es la que usa rawpy.
if(WIN32)
    find_package(libraw CONFIG REQUIRED)
    set(CDNG_LIBRAW_TARGET libraw::raw_r)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBRAW REQUIRED IMPORTED_TARGET libraw_r)
    set(CDNG_LIBRAW_TARGET PkgConfig::LIBRAW)
endif()
find_package(simdjson REQUIRED)
if(NOT APPLE AND NOT WIN32)
    find_package(Threads REQUIRED)
endif()
add_library(cdng_engine STATIC
    engine/common.cpp engine/geometry.cpp
    engine/dng.cpp engine/resize.cpp engine/wav.cpp
    engine/signal.cpp engine/nr.cpp
    engine/signal_asshot.cpp engine/signal_scatter.cpp engine/signal_source.cpp engine/signal_util.cpp
    engine/codec.cpp engine/codec_tables.cpp src/entropy.cpp
    engine/container.cpp)
target_compile_features(cdng_engine PUBLIC cxx_std_17)
target_include_directories(cdng_engine PUBLIC engine include)
target_compile_definitions(cdng_engine PRIVATE CDNG_NATIVE_STATIC)
# CDNG_STATIC_DEPS: el ejecutable del paquete enlaza LibRaw (CDDL), lcms2, libjpeg, OpenMP y simdjson de forma
# estática, así cdng-convert no arrastra bibliotecas de Homebrew. En desarrollo se enlazan dinámicamente.
# En Windows no se usa: el paquete copia los DLL de vcpkg junto al ejecutable, que además es el enlace
# dinámico que pide la LGPL de LibRaw.
option(CDNG_STATIC_DEPS "Link LibRaw and its dependencies statically (macOS y Linux)" OFF)
if(CDNG_STATIC_DEPS AND WIN32)
    message(FATAL_ERROR "CDNG_STATIC_DEPS no está soportado en Windows: el paquete lleva los DLL de vcpkg")
endif()
if(CDNG_STATIC_DEPS)
    # En Linux, LibRaw, lcms2 y libjpeg salen de vcpkg (triplete x64-linux, estático) y OpenMP es la libgomp de GCC,
    # que se toma de la carpeta del propio compilador.
    if(APPLE)
        set(CDNG_STATIC_OMP omp)
        set(CDNG_STATIC_HINTS /opt/homebrew/opt/little-cms2/lib /opt/homebrew/opt/jpeg-turbo/lib /opt/homebrew/opt/libomp/lib)
    else()
        set(CDNG_STATIC_OMP gomp)
        execute_process(COMMAND ${CMAKE_CXX_COMPILER} -print-file-name=libgomp.a OUTPUT_VARIABLE CDNG_GOMP
            OUTPUT_STRIP_TRAILING_WHITESPACE)
        get_filename_component(CDNG_STATIC_HINTS "${CDNG_GOMP}" DIRECTORY)
    endif()
    foreach(lib raw_r lcms2 jpeg ${CDNG_STATIC_OMP})
        find_library(CDNG_STATIC_${lib} NAMES lib${lib}.a PATHS ${LIBRAW_LIBRARY_DIRS} ${CDNG_STATIC_HINTS} NO_DEFAULT_PATH)
        if(NOT CDNG_STATIC_${lib})
            message(FATAL_ERROR "Static lib${lib}.a not found")
        endif()
        target_link_libraries(cdng_engine PUBLIC ${CDNG_STATIC_${lib}})
    endforeach()
    target_include_directories(cdng_engine PUBLIC ${LIBRAW_INCLUDE_DIRS})
    # Homebrew exporta simdjson::simdjson_static; vcpkg, solo simdjson::simdjson (ya estático en x64-linux).
    # zlib: la del sistema en macOS; en Linux, la de vcpkg (estática).
    if(APPLE)
        set(CDNG_ZLIB z)
    else()
        find_package(ZLIB REQUIRED)
        set(CDNG_ZLIB ZLIB::ZLIB)
    endif()
    if(TARGET simdjson::simdjson_static)
        target_link_libraries(cdng_engine PUBLIC ${CDNG_ZLIB} simdjson::simdjson_static)
    else()
        target_link_libraries(cdng_engine PUBLIC ${CDNG_ZLIB} simdjson::simdjson)
    endif()
    if(NOT APPLE)
        # Sin libstdc++ ni libgcc_s del sistema: el ejecutable solo pide glibc.
        target_link_libraries(cdng_engine PUBLIC Threads::Threads ${CMAKE_DL_LIBS} -static-libstdc++ -static-libgcc)
    endif()
else()
    target_link_libraries(cdng_engine PUBLIC ${CDNG_LIBRAW_TARGET} simdjson::simdjson)
endif()
set_target_properties(cdng_engine PROPERTIES CXX_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)
if(WIN32)
    # engine/dng.cpp incluye <libraw.h> a secas, que es como lo deja pkg-config en macOS (-I.../include/libraw).
    # vcpkg instala las cabeceras en include/libraw, así que se añade esa carpeta en lugar de cambiar el #include
    # (cambiarlo rompería la construcción de macOS, donde el padre no está en la ruta de búsqueda).
    find_path(CDNG_LIBRAW_INCLUDE NAMES libraw.h PATH_SUFFIXES libraw)
    if(NOT CDNG_LIBRAW_INCLUDE)
        message(FATAL_ERROR "No se encontró libraw.h; instala las dependencias con el manifiesto vcpkg.json")
    endif()
    target_include_directories(cdng_engine PUBLIC ${CDNG_LIBRAW_INCLUDE})
endif()
if(APPLE)
    # Productos de matrices de la reducción de ruido: la misma rutina que usa NumPy (resultado idéntico).
    target_link_libraries(cdng_engine PUBLIC "-framework Accelerate")
    target_compile_definitions(cdng_engine PRIVATE ACCELERATE_NEW_LAPACK)
endif()
# Bloque B: la paridad con NumPy necesita la aritmética tal cual (las FMA de los productos de matrices se escriben
# explícitamente con std::fma, como las hace Accelerate).
if(NOT MSVC)
    set_source_files_properties(engine/signal.cpp engine/nr.cpp engine/signal_asshot.cpp engine/signal_scatter.cpp
        PROPERTIES COMPILE_OPTIONS "-ffp-contract=off")
else()
    # MSVC no tiene -ffp-contract: lo que evita la contracción es /fp:precise, que es lo que se pide aquí.
    #
    # /arch:AVX2 no es una optimización opcional, es lo que decide si std::fma existe como instrucción. Con el
    # /arch por omisión (SSE2) el x64 no tiene FMA, así que cada std::fma/std::fmaf se compila como **llamada a
    # la CRT** (medido en nr.obj: 0 instrucciones vfmadd y 59 call fma/fmaf). La reducción de ruido hace unos
    # 11.600 millones de FMA por fotograma 4K, de modo que esas llamadas costaban 9,8 s por fotograma.
    #
    # No cambia el resultado: std::fma es un FMA explícito y correctamente redondeado, y vfmadd calcula
    # exactamente eso mismo. Lo que cambiaría los números sería la contracción automática de a*b+c, y esa la
    # sigue bloqueando /fp:precise. Comprobado: el BRAW de 12 fotogramas de la Sigma fp con NR 35/60 sale
    # idéntico byte a byte con y sin este flag.
    #
    # Coste: exige un procesador con AVX2 (Intel 2013 o posterior, AMD 2015 o posterior). Con -DCDNG_AVX2=OFF
    # se vuelve a SSE2 y a las llamadas a la CRT, que es correcto pero mucho más lento.
    option(CDNG_AVX2 "Compilar con AVX2 y FMA en hardware (Windows)" ON)
    target_compile_options(cdng_engine PRIVATE /fp:precise)
    if(CDNG_AVX2)
        target_compile_options(cdng_engine PRIVATE /arch:AVX2)
    endif()
endif()
if(MSVC)
    target_compile_options(cdng_engine PRIVATE /W4 /permissive-)
else()
    target_compile_options(cdng_engine PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(CDNG_SANITIZERS AND NOT MSVC)
    target_compile_options(cdng_engine PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(cdng_engine PUBLIC -fsanitize=address,undefined)
endif()

# Proceso de trabajo de la app (sustituye al worker Python congelado).
add_executable(cdng-convert engine/worker/main.cpp engine/worker/common.cpp engine/worker/convert.cpp engine/worker/gyro.cpp engine/worker/preview.cpp)
target_link_libraries(cdng-convert PRIVATE cdng_engine)
target_compile_features(cdng-convert PRIVATE cxx_std_17)
set_target_properties(cdng-convert PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(NOT MSVC)
    target_compile_options(cdng-convert PRIVATE -Wall -Wextra -Wpedantic)
else()
    # Rutas con acentos: el manifiesto pone la página de códigos del proceso en UTF-8. Ver packaging/utf8.manifest.
    target_link_options(cdng-convert PRIVATE "/MANIFEST:EMBED"
        "/MANIFESTINPUT:${CMAKE_SOURCE_DIR}/packaging/utf8.manifest")
endif()

if(BUILD_TESTING)
    foreach(module geometry dng signal codec container)
        add_executable(engine_${module}_test engine/tests/${module}_test.cpp)
        target_link_libraries(engine_${module}_test PRIVATE cdng_engine)
        set_target_properties(engine_${module}_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
        add_test(NAME engine_${module} COMMAND engine_${module}_test)
        set_tests_properties(engine_${module} PROPERTIES ENVIRONMENT "CDNG_RESOURCES=${CMAKE_SOURCE_DIR}")
    endforeach()
endif()
