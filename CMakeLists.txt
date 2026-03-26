cmake_minimum_required(VERSION 3.16)

project(DicomViewerSkeleton VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options("/source-charset:utf-8")
endif()

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Widgets Network Concurrent)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Widgets Network Concurrent)

set(MEDPRO_VCPKG_TOOLCHAIN "E:/QtProjects/vckpg/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE FILEPATH "Path to the vcpkg toolchain file")
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND EXISTS "${MEDPRO_VCPKG_TOOLCHAIN}")
    set(CMAKE_TOOLCHAIN_FILE "${MEDPRO_VCPKG_TOOLCHAIN}" CACHE FILEPATH "CMake toolchain file" FORCE)
endif()
if(DEFINED CMAKE_TOOLCHAIN_FILE AND EXISTS "${CMAKE_TOOLCHAIN_FILE}")
    include("${CMAKE_TOOLCHAIN_FILE}")
endif()

set(MEDPRO_VTK_ROOT "${PROJECT_SOURCE_DIR}/third_party/VTK9.4" CACHE PATH "Path to the bundled VTK root")
set(MEDPRO_VTK_DIR "${MEDPRO_VTK_ROOT}/lib/cmake/vtk-9.4" CACHE PATH "Path to the bundled VTK config directory")
set(VTK_DIR "${MEDPRO_VTK_DIR}")
message(STATUS "Using VTK_DIR: ${VTK_DIR}")
find_package(VTK REQUIRED)

set(MEDPRO_GDCM_VERSION "3.0.26" CACHE STRING "Bundled GDCM version")
set(MEDPRO_GDCM_BASE_DIR "${PROJECT_SOURCE_DIR}/third_party/GDCM-${MEDPRO_GDCM_VERSION}-install" CACHE PATH "Path to the bundled GDCM install root")
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(MEDPRO_GDCM_ROOT "${MEDPRO_GDCM_BASE_DIR}/Debug")
elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(MEDPRO_GDCM_ROOT "${MEDPRO_GDCM_BASE_DIR}/Release")
else()
    set(MEDPRO_GDCM_ROOT "${MEDPRO_GDCM_BASE_DIR}/${CMAKE_BUILD_TYPE}")
endif()
set(MEDPRO_GDCM_CONFIG_DIR "${MEDPRO_GDCM_ROOT}/lib/gdcm-3.0")
if(EXISTS "${MEDPRO_GDCM_CONFIG_DIR}/GDCMConfig.cmake")
    find_package(GDCM REQUIRED CONFIG PATHS "${MEDPRO_GDCM_CONFIG_DIR}" NO_DEFAULT_PATH)
    message(STATUS "Using GDCM from: ${MEDPRO_GDCM_CONFIG_DIR}")
else()
    message(FATAL_ERROR "Bundled GDCM not found at ${MEDPRO_GDCM_CONFIG_DIR}.")
endif()

set(MEDPRO_ITK_VERSION "5.4.5" CACHE STRING "Bundled ITK version")
set(MEDPRO_ITK_BASE_DIR "${PROJECT_SOURCE_DIR}/third_party/ITK545i" CACHE PATH "Path to the bundled ITK install root")
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(MEDPRO_ITK_ROOT "${MEDPRO_ITK_BASE_DIR}/Debug")
elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(MEDPRO_ITK_ROOT "${MEDPRO_ITK_BASE_DIR}/Release")
else()
    set(MEDPRO_ITK_ROOT "${MEDPRO_ITK_BASE_DIR}/${CMAKE_BUILD_TYPE}")
endif()
set(MEDPRO_ITK_CONFIG_DIR "${MEDPRO_ITK_ROOT}/lib/cmake/ITK-5.4")
set(MEDPRO_ITK_COMPONENTS
    ITKCommon
    ITKIOImageBase
    ITKIOGDCM
    ITKImageFunction
    ITKImageGrid
    ITKImageIntensity
    ITKImageStatistics
    ITKVTK
    ITKVtkGlue
)
if(EXISTS "${MEDPRO_ITK_CONFIG_DIR}/ITKConfig.cmake")
    find_package(ITK REQUIRED COMPONENTS ${MEDPRO_ITK_COMPONENTS} PATHS "${MEDPRO_ITK_CONFIG_DIR}" NO_DEFAULT_PATH)
    message(STATUS "Using ITK from: ${MEDPRO_ITK_CONFIG_DIR}")
else()
    message(FATAL_ERROR "Bundled ITK not found at ${MEDPRO_ITK_CONFIG_DIR}.")
endif()

find_package(ZLIB REQUIRED)
if(ZLIB_FOUND)
    message(STATUS "Using ZLIB includes: ${ZLIB_INCLUDE_DIR}")
else()
    message(FATAL_ERROR "ZLIB not found!")
endif()

set(MEDPRO_QUAZIP_ROOT "${CMAKE_SOURCE_DIR}/QuaZip" CACHE PATH "Path to the bundled QuaZip root")
set(MEDPRO_QUAZIP_CONFIG_DIR "${MEDPRO_QUAZIP_ROOT}/lib/cmake/QuaZip-Qt5-1.4" CACHE PATH "Path to the bundled QuaZip CMake config directory")
find_package(QuaZip-Qt5 QUIET PATHS ${MEDPRO_QUAZIP_CONFIG_DIR})
if(QuaZip-Qt5_FOUND)
    get_target_property(QUAZIP_INCLUDE_DIRS QuaZip::QuaZip INTERFACE_INCLUDE_DIRECTORIES)
    message(STATUS "Using QuaZip includes: ${QUAZIP_INCLUDE_DIRS}")
else()
    message(STATUS "QuaZip-Qt5 not found, continuing without archive package support.")
endif()

if(EXISTS "${MEDPRO_QUAZIP_ROOT}/include/QuaZip-Qt5-1.4/quazip")
    include_directories(${MEDPRO_QUAZIP_ROOT}/include/QuaZip-Qt5-1.4/quazip)
endif()
include(${ITK_USE_FILE})

set(PROJECT_SOURCES
    main.cpp
    mainwindow.cpp
    mainwindow.h
    src/model/studypackage.h
    src/model/studyloadresult.h
    src/core/casepackagereader.h
    src/core/casepackagereader.cpp
    src/core/studyloader.h
    src/core/studyloader.cpp
    src/view/viewportpanel.h
    src/view/viewportpanel.cpp
    src/view/mprviewwidget.h
    src/view/mprviewwidget.cpp
    src/view/modelviewwidget.h
    src/view/modelviewwidget.cpp
    src/view/fourpaneviewer.h
    src/view/fourpaneviewer.cpp
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(DicomViewerSkeleton
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
    )
else()
    if(ANDROID)
        add_library(DicomViewerSkeleton SHARED ${PROJECT_SOURCES})
    else()
        add_executable(DicomViewerSkeleton ${PROJECT_SOURCES})
        target_compile_definitions(DicomViewerSkeleton PRIVATE _UNICODE UNICODE)
    endif()
endif()

target_link_libraries(DicomViewerSkeleton PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Network
    Qt${QT_VERSION_MAJOR}::Concurrent
    VTK::GUISupportQt
    ${VTK_LIBRARIES}
    ${ITK_LIBRARIES}
    ZLIB::ZLIB
    gdcmCommon
    gdcmDSED
    gdcmIOD
    gdcmDICT
    gdcmMSFF
)

if(QuaZip-Qt5_FOUND)
    target_link_libraries(DicomViewerSkeleton PRIVATE QuaZip::QuaZip)
    target_compile_definitions(DicomViewerSkeleton PRIVATE MEDPRO_HAS_QUAZIP=1)
endif()

target_include_directories(DicomViewerSkeleton PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

if(MSVC)
    target_compile_options(DicomViewerSkeleton PRIVATE
        $<$<CONFIG:Release>:/Zi /Od>
    )
    target_link_options(DicomViewerSkeleton PRIVATE
        $<$<CONFIG:Release>:/DEBUG>
    )
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /DEBUG")
endif()

set(MEDPRO_OPENSSL_ROOT "${CMAKE_SOURCE_DIR}/OpenSSL_libs" CACHE PATH "Path to the bundled OpenSSL DLLs")
set(OPENSSL_DLLS
    ${MEDPRO_OPENSSL_ROOT}/libcrypto-1_1-x64.dll
    ${MEDPRO_OPENSSL_ROOT}/libssl-1_1-x64.dll
)
foreach(OPENSSL_DLL IN LISTS OPENSSL_DLLS)
    if(EXISTS "${OPENSSL_DLL}")
        file(COPY "${OPENSSL_DLL}" DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
    endif()
endforeach()

if(QuaZip-Qt5_FOUND)
    if(CMAKE_BUILD_TYPE MATCHES Release)
        set(ZLIB_DLLS ${MEDPRO_QUAZIP_ROOT}/bin/zlib.dll)
        set(QUAZIP_DLLS ${MEDPRO_QUAZIP_ROOT}/bin/quazip1-qt5.dll)
    elseif(CMAKE_BUILD_TYPE MATCHES Debug)
        set(ZLIB_DLLS ${MEDPRO_QUAZIP_ROOT}/bin/zlibd.dll)
        set(QUAZIP_DLLS ${MEDPRO_QUAZIP_ROOT}/bin/quazip1-qt5d.dll)
    endif()

    if(EXISTS "${ZLIB_DLLS}")
        file(COPY "${ZLIB_DLLS}" DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
    endif()
    if(EXISTS "${QUAZIP_DLLS}")
        file(COPY "${QUAZIP_DLLS}" DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
    endif()
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    file(GLOB VTK_DLLS "${MEDPRO_VTK_ROOT}/bin/vtk*9.4d.dll")
elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
    file(GLOB VTK_DLLS "${MEDPRO_VTK_ROOT}/bin/vtk*9.4.dll")
endif()

file(GLOB GDCM_DLLS
    "${MEDPRO_GDCM_ROOT}/bin/gdcm*.dll"
    "${MEDPRO_GDCM_ROOT}/bin/socketxx.dll"
)
file(GLOB ITK_DLLS
    "${MEDPRO_ITK_ROOT}/bin/*.dll"
)

add_custom_command(TARGET DicomViewerSkeleton POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${VTK_DLLS}
    $<TARGET_FILE_DIR:DicomViewerSkeleton>
)
add_custom_command(TARGET DicomViewerSkeleton POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${GDCM_DLLS}
    $<TARGET_FILE_DIR:DicomViewerSkeleton>
)
add_custom_command(TARGET DicomViewerSkeleton POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${ITK_DLLS}
    $<TARGET_FILE_DIR:DicomViewerSkeleton>
)

if(${QT_VERSION} VERSION_LESS 6.1.0)
    set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.DicomViewerSkeleton)
endif()
set_target_properties(DicomViewerSkeleton PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)
install(TARGETS DicomViewerSkeleton
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(DicomViewerSkeleton)
endif()

add_executable(dicom_to_stl
    tools/dicom_to_stl.cpp
)
target_link_libraries(dicom_to_stl PRIVATE
    ${VTK_LIBRARIES}
    ${ITK_LIBRARIES}
    gdcmCommon
    gdcmDSED
    gdcmIOD
    gdcmDICT
    gdcmMSFF
)
target_include_directories(dicom_to_stl PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_compile_definitions(dicom_to_stl PRIVATE _UNICODE UNICODE)

add_custom_command(TARGET dicom_to_stl POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${VTK_DLLS}
    $<TARGET_FILE_DIR:dicom_to_stl>
)
add_custom_command(TARGET dicom_to_stl POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${GDCM_DLLS}
    $<TARGET_FILE_DIR:dicom_to_stl>
)
add_custom_command(TARGET dicom_to_stl POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${ITK_DLLS}
    $<TARGET_FILE_DIR:dicom_to_stl>
)


