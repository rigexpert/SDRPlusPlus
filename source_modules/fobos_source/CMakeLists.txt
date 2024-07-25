#==============================================================================
#  SDR++ source module wrapper for Fobos SDR API
#  V.T.
#  LGPL-2.1 or above LICENSE
#  23.07.2024
#==============================================================================

cmake_minimum_required(VERSION 3.13)
project(fobos_sourse)

file(GLOB SRC "src/*.cpp")

include(${SDRPP_MODULE_CMAKE})

if (MSVC)
    # Lib path
    target_link_directories(fobos_sourse PRIVATE "C:/Program Files/PothosSDR/bin/")

    target_include_directories(fobos_sourse PUBLIC "C:/Program Files/PothosSDR/include/libfobos/")

    target_link_libraries(fobos_sourse PRIVATE fobos)
elseif (ANDROID)
    target_include_directories(fobos_sourse PUBLIC
        /sdr-kit/${ANDROID_ABI}/include/libfobos
    )

    target_link_libraries(fobos_sourse PUBLIC
        /sdr-kit/${ANDROID_ABI}/lib/libusb1.0.so
        /sdr-kit/${ANDROID_ABI}/lib/libfobos.so
    )
else (MSVC)
    find_package(PkgConfig)

    pkg_check_modules(LIBFOBOS REQUIRED libfobos)

    target_include_directories(fobos_sourse PRIVATE ${LIBFOBOS_INCLUDE_DIRS})
    target_link_directories(fobos_sourse PRIVATE ${LIBFOBOS_LIBRARY_DIRS})
    target_link_libraries(fobos_sourse PRIVATE ${LIBFOBOS_LIBRARIES})

    # Include it because for some reason pkgconfig doesn't look here?
    if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
        target_include_directories(fobos_sourse PRIVATE "/usr/local/include")
    endif()
    
endif ()