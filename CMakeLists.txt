cmake_minimum_required(VERSION 3.10...3.28)

option(BUILD_WITH_QT6 "Build with Qt6, else Qt5" OFF)

# Must be set before the first project() call to be taken into account.
if (APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
  if (BUILD_WITH_QT6)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
  else()
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
  endif()
endif()

project(gmic-qt LANGUAGES CXX)

message(STATUS "Using CMake version: ${CMAKE_VERSION}")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules")
include(FeatureSummary)
include(CheckCXXSourceCompiles)
include(CheckIPOSupported)
include(GNUInstallDirs)

# Qt6 requires C++17 (Qt5 builds stay with C++11).
if (BUILD_WITH_QT6)
  set(CMAKE_CXX_STANDARD 17)
else()
  set(CMAKE_CXX_STANDARD 11)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_definitions(-Dcimg_use_cpp11=1)

if (BUILD_WITH_QT6)
    set(MIN_QT_VERSION 6.5.0)
else()
    set(MIN_QT_VERSION 5.2.0)
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC OFF)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(EXTRA_LIBRARIES)

#
# Build type
#
get_property(GMIC_QT_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if (NOT GMIC_QT_MULTI_CONFIG AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

message(STATUS "Build type is '${CMAKE_BUILD_TYPE}'")

#
# Helpers
#

# GNU make is needed to generate some files (CImg.h, translations...).
# On BSD systems 'make' is not GNU make, hence the search for 'gmake' first.
find_program(GMIC_MAKE_PROGRAM NAMES gmake make)
mark_as_advanced(GMIC_MAKE_PROGRAM)

# gmic_run_make(<dir> <ok_var> [targets...])
# Runs make in <dir>; <ok_var> is set to TRUE on success, FALSE otherwise.
function(gmic_run_make dir ok_var)
  set(${ok_var} FALSE PARENT_SCOPE)
  if (NOT GMIC_MAKE_PROGRAM)
    return()
  endif()
  execute_process(COMMAND "${GMIC_MAKE_PROGRAM}" ${ARGN}
                  WORKING_DIRECTORY "${dir}"
                  RESULT_VARIABLE res
                  OUTPUT_QUIET)
  if (res EQUAL 0)
    set(${ok_var} TRUE PARENT_SCOPE)
  endif()
endfunction()

# gmic_read_version(<header> <macro> <out_var>)
# Reads the numeric value of '#define <macro> <number>' in <header>.
function(gmic_read_version header macro out_var)
  file(STRINGS "${header}" version_line
       REGEX "^[ \t]*#[ \t]*define[ \t]+${macro}[ \t]+[0-9]+"
       LIMIT_COUNT 1)
  if (NOT version_line)
    message(FATAL_ERROR "\nCannot find the definition of '${macro}' in ${header}")
  endif()
  string(REGEX REPLACE "^.*${macro}[ \t]+([0-9]+).*$" "\\1" version "${version_line}")
  set(${out_var} "${version}" PARENT_SCOPE)
endfunction()

# Wraps .ui files with the right Qt version.
macro(gmic_qt_wrap_ui var)
  if (BUILD_WITH_QT6)
    qt6_wrap_ui(${var} ${ARGN})
  else()
    qt5_wrap_ui(${var} ${ARGN})
  endif()
endmacro()

#
# Host application
#
set(GMIC_QT_HOST "gimp" CACHE STRING "Define for which host gmic-qt will be built: gimp, gimp3 (experimental), none, paintdotnet or 8bf.")
set_property(CACHE GMIC_QT_HOST PROPERTY STRINGS gimp gimp3 none paintdotnet 8bf)
if (NOT GMIC_QT_HOST MATCHES "^(gimp|gimp3|none|paintdotnet|8bf)$")
  message(FATAL_ERROR "GMIC_QT_HOST ('${GMIC_QT_HOST}') is not defined as gimp, gimp3, none, paintdotnet or 8bf")
endif()
if (GMIC_QT_HOST STREQUAL "none")
  message(STATUS "Building standalone version.")
else()
  message(STATUS "Building for target host application: ${GMIC_QT_HOST}")
endif()

#
# G'MIC library
#
option(ENABLE_SYSTEM_GMIC "Find GMIC shared library installed on the system" ON)

if (ENABLE_SYSTEM_GMIC)
  option(ENABLE_DYNAMIC_LINKING "Dynamically link the binaries to the GMIC shared library" ON)
  if (NOT ENABLE_DYNAMIC_LINKING)
    message(FATAL_ERROR "ENABLE_DYNAMIC_LINKING=OFF requires ENABLE_SYSTEM_GMIC=OFF")
  endif()
else()
  option(ENABLE_DYNAMIC_LINKING "Dynamically link the binaries to the GMIC shared library" OFF)

  if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../src/gmic.cpp")
    set(GMIC_PATH_DEFAULT "${CMAKE_CURRENT_SOURCE_DIR}/../src")
  else()
    set(GMIC_PATH_DEFAULT "${CMAKE_CURRENT_SOURCE_DIR}/../gmic/src")
  endif()
  set(GMIC_PATH "${GMIC_PATH_DEFAULT}" CACHE PATH "Define the path to the gmic headers")
  set(GMIC_LIB_PATH "" CACHE PATH "Define the path to the GMIC shared library")

  message(STATUS "G'MIC path: ${GMIC_PATH}")
endif()

option(ENABLE_CURL "Add support for curl" ON)

option(ENABLE_ASAN "Enable -fsanitize=address (if debug build, when supported)" ON)
option(ENABLE_FFTW3 "Enable FFTW3 library support" ON)
option(ENABLE_STRIP "Strip the executables at link time (Release build, not MSVC)" ON)

check_ipo_supported(RESULT HAVE_LTO LANGUAGES CXX)

if (HAVE_LTO)
  option(ENABLE_LTO "Enable Link Time Optimizer" OFF)
endif()

if (MSVC)
  option(ENABLE_CFG "Enable Control Flow Guard (MSVC)" ON)
  add_definitions(-D__PRETTY_FUNCTION__=__FUNCSIG__)
endif()

if (NOT ENABLE_SYSTEM_GMIC)
  #
  # Look for G'MIC repository
  #
  get_filename_component(GMIC_ABSOLUTE_PATH "${GMIC_PATH}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
  if (EXISTS "${GMIC_ABSOLUTE_PATH}/gmic.cpp")
    message(STATUS "Found G'MIC repository")
  else()
    get_filename_component(TARGET_CLONE_DIR "${GMIC_ABSOLUTE_PATH}/.." ABSOLUTE)
    message("")
    message("Cannot find G'MIC repository in ${GMIC_ABSOLUTE_PATH}")
    message("")
    message("You should try:")
    message("")
    message("   git clone https://github.com/dtschump/gmic.git ${TARGET_CLONE_DIR}/gmic")
    message("")
    message(FATAL_ERROR "\nG'MIC repository not found")
  endif()

  #
  # Look for CImg.h and gmic_stdlib_community.h
  #
  set(GMIC_FILES CImg.h gmic_stdlib_community.h)
  foreach(F ${GMIC_FILES})
    if (EXISTS "${GMIC_ABSOLUTE_PATH}/${F}")
      message(STATUS "Found ${GMIC_PATH}/${F}")
    else()
      message(STATUS "${F} not found, trying to generate it")
      gmic_run_make("${GMIC_ABSOLUTE_PATH}" GMIC_MAKE_OK ${F})
      if (EXISTS "${GMIC_ABSOLUTE_PATH}/${F}")
        message(STATUS "Found ${GMIC_PATH}/${F}")
      else()
        if (NOT GMIC_MAKE_PROGRAM)
          message(FATAL_ERROR "\nCannot obtain ${GMIC_PATH}/${F} (GNU make not found)")
        endif()
        message(FATAL_ERROR "\nCannot obtain ${GMIC_PATH}/${F}")
      endif()
    endif()
  endforeach()

  #
  # Ensure that gmic and CImg are the same version
  #
  gmic_read_version("${GMIC_ABSOLUTE_PATH}/CImg.h" cimg_version CIMG_VERSION)
  message(STATUS "CImg version is [${CIMG_VERSION}]")

  gmic_read_version("${GMIC_ABSOLUTE_PATH}/gmic.h" gmic_version GMIC_VERSION)
  message(STATUS "G'MIC version is [${GMIC_VERSION}]")

  if (NOT GMIC_VERSION STREQUAL CIMG_VERSION)
    message(FATAL_ERROR "\nVersion numbers of files 'gmic.h' (${GMIC_VERSION}) and 'CImg.h' (${CIMG_VERSION}) mismatch")
  endif()
endif()


option(PRERELEASE "Set to ON makes this a prerelease build" OFF)
if (PRERELEASE)
    string(TIMESTAMP PRERELEASE_DATE %y%m%d)
    message(STATUS "Prerelease date is ${PRERELEASE_DATE}")
    add_definitions(-Dgmic_prerelease="${PRERELEASE_DATE}")
endif()

option(DRMINGW "Set to ON enables the drmingw debugger." OFF)
if (DRMINGW)
    add_definitions(-DDRMINGW)
endif()


# Required packages

#
# Gmic
#
if (ENABLE_SYSTEM_GMIC)
  find_package(Gmic REQUIRED CONFIG)
endif (ENABLE_SYSTEM_GMIC)

#
# Threads
#
find_package(Threads REQUIRED)

#
# Qt (Qt5 or Qt6), including the translation tools
#
if (BUILD_WITH_QT6)
    set(QT_VERSION_MAJOR 6)
else()
    set(QT_VERSION_MAJOR 5)
endif()

find_package(Qt${QT_VERSION_MAJOR} ${MIN_QT_VERSION}
        REQUIRED COMPONENTS
        Core
        Gui
        Widgets
        Network
        LinguistTools
)

#
# PNG
#
find_package(PNG REQUIRED)
add_definitions(${PNG_DEFINITIONS})
add_definitions(-Dcimg_use_png)
include_directories(SYSTEM ${PNG_INCLUDE_DIR})

#
# ZLIB
#
find_package(ZLIB REQUIRED)
add_definitions(-Dcimg_use_zlib)
include_directories(SYSTEM ${ZLIB_INCLUDE_DIRS} )

#
# FFTW3
#
if (ENABLE_FFTW3)
  find_package(FFTW3 REQUIRED)
  add_definitions(-Dcimg_use_fftw3 )
  include_directories(${FFTW3_INCLUDE_DIR})

  # Detect threads support (the threads library has to be linked for the test)
  set(CMAKE_REQUIRED_INCLUDES ${FFTW3_INCLUDE_DIR})
  set(CMAKE_REQUIRED_LIBRARIES ${FFTW3_THREADS_LIBRARIES} ${FFTW3_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT})
  check_cxx_source_compiles("
      #include <fftw3.h>

      int main() {
          fftw_init_threads();
      }
  " HAVE_FFTW3_THREADS)
  unset(CMAKE_REQUIRED_INCLUDES)
  unset(CMAKE_REQUIRED_LIBRARIES)

  if(HAVE_FFTW3_THREADS)
    message(STATUS "FFTW threads Found")
    list(APPEND EXTRA_LIBRARIES ${FFTW3_THREADS_LIBRARIES})
  else()
    add_definitions(-Dcimg_use_fftw3_singlethread)
  endif()
endif()

#
# CURL
#
if(ENABLE_CURL)
    find_package(CURL)
    if (CURL_FOUND)
        add_definitions(-Dcimg_use_curl)
        include_directories(SYSTEM ${CURL_INCLUDE_DIRS} )
    else()
        message(WARNING "ENABLE_CURL is ON but curl was not found: building without curl support")
    endif()
endif()

#
# libatomic (needed on some architectures for 64-bit atomics, e.g. armel, mipsel, powerpc)
#
check_cxx_source_compiles("
    #include <atomic>
    #include <cstdint>
    int main() {
        std::atomic<std::uint64_t> a(0);
        return static_cast<int>(a.fetch_add(1));
    }
" GMIC_QT_ATOMIC64_WITHOUT_LIB)
if (NOT GMIC_QT_ATOMIC64_WITHOUT_LIB)
  set(CMAKE_REQUIRED_LIBRARIES atomic)
  check_cxx_source_compiles("
      #include <atomic>
      #include <cstdint>
      int main() {
          std::atomic<std::uint64_t> a(0);
          return static_cast<int>(a.fetch_add(1));
      }
  " GMIC_QT_ATOMIC64_WITH_LIBATOMIC)
  unset(CMAKE_REQUIRED_LIBRARIES)
  if (GMIC_QT_ATOMIC64_WITH_LIBATOMIC)
    message(STATUS "Linking with libatomic")
    list(APPEND EXTRA_LIBRARIES atomic)
  endif()
endif()

#
# Test for OpenMP
#
find_package(OpenMP 2.0)
set_package_properties(OpenMP PROPERTIES
    DESCRIPTION "A low-level parallel execution library"
    URL "http://openmp.org/wp/"
    TYPE OPTIONAL
    PURPOSE "Optionally used by gmic-qt")

if (OpenMP_FOUND)
    message(STATUS "G'Mic: using OpenMP ${OpenMP_CXX_VERSION}")
    link_libraries(OpenMP::OpenMP_CXX)
    add_definitions(-Dcimg_use_openmp)
endif()

#
# LTO option (never in Debug builds)
#
if (HAVE_LTO AND ENABLE_LTO AND NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "Link Time Optimizer enabled")
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

#
# Enable CFG
#
if (MSVC)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /STACK:16777216")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} /STACK:16777216")
    set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} /STACK:16777216")
    if (ENABLE_CFG)
        add_compile_options(/guard:CF)
        add_link_options(/GUARD:CF)
    endif()
elseif(WIN32)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--stack,16777216")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--stack,16777216")
    set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} -Wl,--stack,16777216")
endif()

#
# add all defines
#

set(gmic_qt_LIBRARIES
        Qt${QT_VERSION_MAJOR}::Core
        Qt${QT_VERSION_MAJOR}::Widgets
        Qt${QT_VERSION_MAJOR}::Gui
        Qt${QT_VERSION_MAJOR}::Network
        Threads::Threads
        ${PNG_LIBRARIES}
        ${FFTW3_LIBRARIES}
        ${ZLIB_LIBRARIES}
        ${EXTRA_LIBRARIES}
)

if(ENABLE_CURL)
    if (CURL_FOUND)
        set(gmic_qt_LIBRARIES
            ${gmic_qt_LIBRARIES}
            ${CURL_LIBRARIES}
        )
    endif()
endif()

add_definitions(-Dgmic_core)
add_definitions(-Dgmic_community)
add_definitions(-Dcimg_use_abort)
add_definitions(-Dgmic_is_parallel)
add_definitions(-Dgmic_gui)
add_definitions(-Dcimg_appname=\"gmic\")

if (UNIX)
    add_definitions(-D_IS_UNIX_)
    if(ANDROID)
        add_definitions(-Dcimg_display=0)
    elseif(NOT APPLE)
        add_definitions(-Dcimg_use_vt100)
        find_package(X11)  # XXX: Wayland is coming!
        if (X11_FOUND)
            add_definitions(-Dcimg_display=1)
            include_directories(SYSTEM ${X11_INCLUDE_DIR})
            set(gmic_qt_LIBRARIES
                ${gmic_qt_LIBRARIES}
                ${X11_LIBRARIES}
            )
        else()
            message(WARNING "X11 not found: building without X11 display support")
            add_definitions(-Dcimg_display=0)
        endif()
    endif()
endif()

if (APPLE)
    add_definitions(-Dcimg_display=0)
    add_definitions(-D_IS_MACOS_)
    set(CMAKE_MACOSX_RPATH 1)
    # The deployment target is set through CMAKE_OSX_DEPLOYMENT_TARGET (top of this file).
    if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        add_compile_options(-Wno-macro-redefined -Wno-deprecated-register)
    endif()
endif()

if (WIN32)
    add_definitions(-Dcimg_display=2)
    add_definitions(-DPSAPI_VERSION=1)
    add_definitions(-D_IS_WINDOWS_)
    if (MSVC)
      add_definitions(-D_CRT_SECURE_NO_WARNINGS)
      add_compile_options(/wd4267)
    endif()
    set(gmic_qt_LIBRARIES
        ${gmic_qt_LIBRARIES}
        psapi gdi32
    )
endif()

#
# RPATH (note: ENABLE_DYNAMIC_LINKING sets CMAKE_SKIP_RPATH further below, which disables it)
#
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
if (APPLE)
    set(CMAKE_INSTALL_RPATH "@loader_path/")
elseif (UNIX)
    set(CMAKE_INSTALL_RPATH "$ORIGIN/")
endif()

#
# Build type specific settings
#
# Definitions and flags are defined per configuration, so that they also work
# with multi-config generators (Visual Studio, Xcode). Any other build type
# (None, MinSizeRel, ...) is left untouched: flags are up to the caller/packager.
#
set_property(DIRECTORY APPEND PROPERTY COMPILE_DEFINITIONS
    "$<$<CONFIG:Debug>:_GMIC_QT_DEBUG_>"
    "$<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:QT_NO_DEBUG_OUTPUT>"
)

if (MSVC)
    string(REPLACE "Ob1" "Ob2" CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /fp:fast /Oi")
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} /fp:fast /Oi")
else()
    foreach(OPT_FLAG "-O2" "-O3")
        string(REPLACE "${OPT_FLAG}" "" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
        string(REPLACE "${OPT_FLAG}" "" CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")
    endforeach()
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -O2")
    if (CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo" AND NOT ENABLE_SYSTEM_GMIC)
        set_source_files_properties(${GMIC_PATH}/gmic.cpp PROPERTIES COMPILE_FLAGS "-O3")
    endif()

    if (ENABLE_STRIP)
        set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} -s")
    endif()
    if (WIN32)
        set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} -mwindows")
    endif()
endif()

if (CMAKE_BUILD_TYPE STREQUAL "Debug" AND ENABLE_ASAN)
    # AddressSanitizer is not available everywhere (MinGW, some architectures...)
    # and conflicts with MSVC's default runtime checks: test before use.
    if (NOT MSVC)
        set(CMAKE_REQUIRED_FLAGS "-fsanitize=address")
        set(CMAKE_REQUIRED_LINK_OPTIONS "-fsanitize=address")
        check_cxx_source_compiles("int main() { return 0; }" GMIC_QT_HAVE_ASAN)
        unset(CMAKE_REQUIRED_FLAGS)
        unset(CMAKE_REQUIRED_LINK_OPTIONS)
        if (GMIC_QT_HAVE_ASAN)
            set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -fno-omit-frame-pointer")
            set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")
        else()
            message(STATUS "AddressSanitizer is not supported by this toolchain: ENABLE_ASAN ignored")
        endif()
    endif()
endif()

if (NOT ENABLE_SYSTEM_GMIC)
    include_directories(${GMIC_PATH})
endif (NOT ENABLE_SYSTEM_GMIC)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)

set (gmic_qt_SRCS
  src/ClickableLabel.h
  src/Common.h
  src/CroppedActiveLayerProxy.h
  src/CroppedImageListProxy.h
  src/DialogSettings.h
  src/FilterParameters/AbstractParameter.h
  src/FilterParameters/BoolParameter.h
  src/FilterParameters/ButtonParameter.h
  src/FilterParameters/ChoiceParameter.h
  src/FilterParameters/ColorParameter.h
  src/FilterParameters/ConstParameter.h
  src/FilterParameters/CustomDoubleSpinBox.h
  src/FilterParameters/CustomSpinBox.h
  src/FilterParameters/FileParameter.h
  src/FilterParameters/FilterParametersWidget.h
  src/FilterParameters/FloatParameter.h
  src/FilterParameters/FolderParameter.h
  src/FilterParameters/IntParameter.h
  src/FilterParameters/LinkParameter.h
  src/FilterParameters/MultilineTextParameterWidget.h
  src/FilterParameters/NoteParameter.h
  src/FilterParameters/PointParameter.h
  src/FilterParameters/SeparatorParameter.h
  src/FilterParameters/TextParameter.h
  src/FilterSelector/FavesModel.h
  src/FilterSelector/FavesModelReader.h
  src/FilterSelector/FavesModelWriter.h
  src/FilterSelector/FiltersModelBinaryReader.h
  src/FilterSelector/FiltersModelBinaryWriter.h
  src/FilterSelector/FiltersModel.h
  src/FilterSelector/FiltersModelReader.h
  src/FilterSelector/FiltersPresenter.h
  src/FilterSelector/FiltersView/FiltersView.h
  src/FilterSelector/FiltersView/FilterTreeAbstractItem.h
  src/FilterSelector/FiltersView/FilterTreeFolder.h
  src/FilterSelector/FiltersView/FilterTreeItemDelegate.h
  src/FilterSelector/FiltersView/FilterTreeItem.h
  src/FilterSelector/FiltersView/TreeView.h
  src/FilterSelector/FiltersVisibilityMap.h
  src/FilterSelector/FilterTagMap.h
  src/FilterGuiDynamismCache.h
  src/FilterSyncRunner.h
  src/FilterTextTranslator.h
  src/FilterThread.h
  src/Globals.h
  src/GmicProcessor.h
  src/GmicQt.h
  src/GmicStdlib.h
  src/HeadlessProcessor.h
  src/Host/GmicQtHost.h
  src/HtmlTranslator.h
  src/IconLoader.h
  src/ImageTools.h
  src/InputOutputState.h
  src/KeypointList.h
  src/LanguageSettings.h
  src/LayersExtentProxy.h
  src/Logger.h
  src/MainWindow.h
  src/Misc.h
  src/OverrideCursor.h
  src/ParametersCache.h
  src/PersistentMemory.h
  src/Settings.h
  src/SourcesWidget.h
  src/Tags.h
  src/TimeLogger.h
  src/Updater.h
  src/Utils.h
  src/Widgets/InOutPanel.h
  src/Widgets/LanguageSelectionWidget.h
  src/Widgets/PreviewWidget.h
  src/Widgets/ProgressInfoWidget.h
  src/Widgets/ProgressInfoWindow.h
  src/Widgets/SearchFieldWidget.h
  src/Widgets/VisibleTagSelector.h
  src/Widgets/ZoomLevelSelector.h
  src/ZoomConstraint.h
)

if (NOT ENABLE_SYSTEM_GMIC)
    set(gmic_qt_SRCS
        ${gmic_qt_SRCS}
        ${GMIC_PATH}/gmic.h
        ${GMIC_PATH}/CImg.h
        ${GMIC_PATH}/gmic_stdlib_community.h
    )
endif()

set(gmic_qt_SRCS
  ${gmic_qt_SRCS}
  src/ClickableLabel.cpp
  src/Common.cpp
  src/CroppedActiveLayerProxy.cpp
  src/CroppedImageListProxy.cpp
  src/DialogSettings.cpp
  src/FilterParameters/AbstractParameter.cpp
  src/FilterParameters/BoolParameter.cpp
  src/FilterParameters/ButtonParameter.cpp
  src/FilterParameters/ChoiceParameter.cpp
  src/FilterParameters/ColorParameter.cpp
  src/FilterParameters/ConstParameter.cpp
  src/FilterParameters/CustomDoubleSpinBox.cpp
  src/FilterParameters/CustomSpinBox.cpp
  src/FilterParameters/FileParameter.cpp
  src/FilterParameters/FilterParametersWidget.cpp
  src/FilterParameters/FloatParameter.cpp
  src/FilterParameters/FolderParameter.cpp
  src/FilterParameters/IntParameter.cpp
  src/FilterParameters/LinkParameter.cpp
  src/FilterParameters/MultilineTextParameterWidget.cpp
  src/FilterParameters/NoteParameter.cpp
  src/FilterParameters/PointParameter.cpp
  src/FilterParameters/SeparatorParameter.cpp
  src/FilterParameters/TextParameter.cpp
  src/FilterSelector/FavesModel.cpp
  src/FilterSelector/FavesModelReader.cpp
  src/FilterSelector/FavesModelWriter.cpp
  src/FilterSelector/FiltersModelBinaryReader.cpp
  src/FilterSelector/FiltersModelBinaryWriter.cpp
  src/FilterSelector/FiltersModel.cpp
  src/FilterSelector/FiltersModelReader.cpp
  src/FilterSelector/FiltersPresenter.cpp
  src/FilterSelector/FiltersView/FiltersView.cpp
  src/FilterSelector/FiltersView/FilterTreeAbstractItem.cpp
  src/FilterSelector/FiltersView/FilterTreeFolder.cpp
  src/FilterSelector/FiltersView/FilterTreeItem.cpp
  src/FilterSelector/FiltersView/FilterTreeItemDelegate.cpp
  src/FilterSelector/FiltersView/TreeView.cpp
  src/FilterSelector/FiltersVisibilityMap.cpp
  src/FilterSelector/FilterTagMap.cpp
  src/FilterGuiDynamismCache.cpp
  src/FilterSyncRunner.cpp
  src/FilterTextTranslator.cpp
  src/FilterThread.cpp
  src/Globals.cpp
  src/GmicProcessor.cpp
  src/GmicQt.cpp
  src/GmicStdlib.cpp
  src/HeadlessProcessor.cpp
  src/HtmlTranslator.cpp
  src/IconLoader.cpp
  src/ImageTools.cpp
  src/InputOutputState.cpp
  src/KeypointList.cpp
  src/LanguageSettings.cpp
  src/LayersExtentProxy.cpp
  src/Logger.cpp
  src/MainWindow.cpp
  src/Misc.cpp
  src/OverrideCursor.cpp
  src/ParametersCache.cpp
  src/PersistentMemory.cpp
  src/Settings.cpp
  src/SourcesWidget.cpp
  src/Tags.cpp
  src/TimeLogger.cpp
  src/Updater.cpp
  src/Utils.cpp
  src/Widgets/InOutPanel.cpp
  src/Widgets/LanguageSelectionWidget.cpp
  src/Widgets/PreviewWidget.cpp
  src/Widgets/ProgressInfoWidget.cpp
  src/Widgets/ProgressInfoWindow.cpp
  src/Widgets/SearchFieldWidget.cpp
  src/Widgets/VisibleTagSelector.cpp
  src/Widgets/ZoomLevelSelector.cpp
)

set (gmic_qt_FORMS
  ui/dialogsettings.ui
  ui/filtersview.ui
  ui/headlessprogressdialog.ui
  ui/inoutpanel.ui
  ui/languageselectionwidget.ui
  ui/mainwindow.ui
  ui/multilinetextparameterwidget.ui
  ui/progressinfowidget.ui
  ui/progressinfowindow.ui
  ui/SearchFieldWidget.ui
  ui/sourceswidget.ui
  ui/zoomlevelselector.ui
)

# Directory containing the G'MIC sources when gmic-qt lives inside the gmic repository
set(GMIC_QT_PARENT_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../src")

if(ENABLE_DYNAMIC_LINKING)
  # Note: this disables any RPATH (build and install), whatever CMAKE_INSTALL_RPATH says.
  set(CMAKE_SKIP_RPATH TRUE)
  # G'MIC-Qt needs visibility into the private symbols defined
  # by the gmic.cpp plugin. However, this is only possible
  # if the library is static OR if it's dynamic and built by
  # a compiler that supports .so-style exports.
  if (TARGET libgmicstatic OR MSVC OR NOT ENABLE_SYSTEM_GMIC)
    set(gmic_qt_LIBRARIES
      ${gmic_qt_LIBRARIES}
      libgmicstatic
    )
  elseif(TARGET libgmic)
    set(gmic_qt_LIBRARIES
      ${gmic_qt_LIBRARIES}
      libgmic
    )
  elseif(GMIC_LIB_PATH)
    set(gmic_qt_LIBRARIES
      ${gmic_qt_LIBRARIES}
      "gmic"
    )
  else()
    message(FATAL_ERROR "No G'MIC library is available for linking. Please build libgmic as a static library.")
  endif()
  if (NOT ENABLE_SYSTEM_GMIC)
    if (GMIC_LIB_PATH)
      link_directories(${GMIC_LIB_PATH})
      # Inject the G'MIC CImg plugin.
      include_directories(${GMIC_QT_PARENT_SRC_DIR})
    else()
      if (NOT EXISTS "${GMIC_QT_PARENT_SRC_DIR}/gmic.cpp")
        message(FATAL_ERROR "Cannot find ${GMIC_QT_PARENT_SRC_DIR}/gmic.cpp: set GMIC_LIB_PATH or build inside the G'MIC repository")
      endif()
      # Mimic an external G'MIC library build for catching link ABI errors.
      add_library(libgmicstatic STATIC ${GMIC_QT_PARENT_SRC_DIR}/gmic.cpp)
      target_include_directories(libgmicstatic PUBLIC ${GMIC_QT_PARENT_SRC_DIR})
      # We need internal access into the gmic-core API.
      target_compile_definitions(libgmicstatic PUBLIC gmic_core)
      set_target_properties(libgmicstatic
        PROPERTIES
          AUTOMOC OFF
      )
      target_link_libraries(libgmicstatic PUBLIC
        Threads::Threads
        ${PNG_LIBRARIES}
        ${FFTW3_LIBRARIES}
        ${ZLIB_LIBRARIES}
        ${CURL_LIBRARIES}
        ${EXTRA_LIBRARIES})
    endif()
  else()
    # Inject the G'MIC CImg plugin.
    include_directories(${GMIC_QT_PARENT_SRC_DIR})
  endif()
else(ENABLE_DYNAMIC_LINKING)
  set(gmic_qt_SRCS
    ${gmic_qt_SRCS}
    ${GMIC_PATH}/gmic.cpp
    )
endif(ENABLE_DYNAMIC_LINKING)

#
# Translations (.qm files are generated at configure time by the translations Makefiles)
#
message(STATUS "Producing translation .qm files")
gmic_run_make("${CMAKE_CURRENT_SOURCE_DIR}/translations" GMIC_TRANSLATIONS_OK)
message(STATUS "Producing filter translation .qm files")
gmic_run_make("${CMAKE_CURRENT_SOURCE_DIR}/translations/filters" GMIC_FILTER_TRANSLATIONS_OK)
if (NOT GMIC_TRANSLATIONS_OK OR NOT GMIC_FILTER_TRANSLATIONS_OK)
  message(WARNING "Could not generate the translation .qm files (GNU make and Qt lrelease are required). "
                  "The build will fail if they are not already present.")
endif()

set(gmic_qt_QRC
    gmic_qt.qrc
    translations.qrc
    wip_translations.qrc
)

if (GMIC_QT_HOST STREQUAL "gimp" OR GMIC_QT_HOST STREQUAL "gimp3")
    if (GMIC_QT_HOST STREQUAL "gimp3")
        set(TARGET_GIMP_VERSION 3)
    else()
        set(TARGET_GIMP_VERSION 2)
    endif()

    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GIMP REQUIRED gimp-${TARGET_GIMP_VERSION}.0 IMPORTED_TARGET)
    # CMake does not support passing --define-variable through pkg_get_variable.
    execute_process(COMMAND ${PKG_CONFIG_EXECUTABLE} gimp-${TARGET_GIMP_VERSION}.0 --define-variable=prefix=${CMAKE_INSTALL_PREFIX} --variable gimplibdir
                    OUTPUT_VARIABLE GIMP_PKGLIBDIR
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                    RESULT_VARIABLE GIMP_PKGLIBDIR_RESULT)
    if (NOT (GIMP_PKGLIBDIR_RESULT EQUAL 0) OR NOT GIMP_PKGLIBDIR)
        message(FATAL_ERROR "Cannot determine the GIMP plug-ins directory (pkg-config gimp-${TARGET_GIMP_VERSION}.0 --variable gimplibdir)")
    endif()

    set (gmic_qt_SRCS ${gmic_qt_SRCS} src/Host/Gimp/host_gimp.cpp)

    gmic_qt_wrap_ui(gmic_qt_SRCS ${gmic_qt_FORMS})

    add_definitions(-DGMIC_HOST=gimp -DGIMP_DISABLE_DEPRECATED)
    add_executable(gmic_gimp_qt ${gmic_qt_SRCS} ${gmic_qt_QRC})
    target_link_libraries(
      gmic_gimp_qt
      PRIVATE
      PkgConfig::GIMP
      ${gmic_qt_LIBRARIES}
      )
    install(TARGETS gmic_gimp_qt RUNTIME DESTINATION "${GIMP_PKGLIBDIR}/plug-ins/gmic_gimp_qt")

elseif (GMIC_QT_HOST STREQUAL "none")

  set (gmic_qt_SRCS ${gmic_qt_SRCS}
    src/Host/None/host_none.cpp
    src/Host/None/ImageDialog.h
    src/Host/None/ImageDialog.cpp
    src/Host/None/JpegQualityDialog.h
    src/Host/None/JpegQualityDialog.cpp
    )
  set(gmic_qt_FORMS ${gmic_qt_FORMS}
    src/Host/None/jpegqualitydialog.ui
    )

  gmic_qt_wrap_ui(gmic_qt_SRCS ${gmic_qt_FORMS})

  add_definitions(-DGMIC_HOST=standalone)
  add_executable(gmic_qt ${gmic_qt_SRCS} ${gmic_qt_QRC})
  target_link_libraries(gmic_qt PRIVATE ${gmic_qt_LIBRARIES})
  install(TARGETS gmic_qt RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

elseif (GMIC_QT_HOST STREQUAL "paintdotnet")

  set (gmic_qt_SRCS ${gmic_qt_SRCS} src/Host/PaintDotNet/host_paintdotnet.cpp)

  gmic_qt_wrap_ui(gmic_qt_SRCS ${gmic_qt_FORMS})

  add_definitions(-DGMIC_HOST=paintdotnet)
  add_executable(gmic_paintdotnet_qt ${gmic_qt_SRCS} ${gmic_qt_QRC})
  target_link_libraries(
    gmic_paintdotnet_qt
    PRIVATE ${gmic_qt_LIBRARIES}
    )

elseif (GMIC_QT_HOST STREQUAL "8bf")

  # Look for a CMake package on MSVC or a PkgConfig file on MinGW etc.
  if (MSVC)
    find_package(lcms2 CONFIG REQUIRED)
    include_directories(${LCMS2_INCLUDE_DIR})
    set(GMIC_QT_LCMS2_LIBRARIES ${LCMS2_LIBRARIES})
  else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LCMS2 REQUIRED IMPORTED_TARGET lcms2)
    set(GMIC_QT_LCMS2_LIBRARIES PkgConfig::LCMS2)
  endif()

  set (gmic_qt_SRCS ${gmic_qt_SRCS} src/Host/8bf/host_8bf.cpp)

  gmic_qt_wrap_ui(gmic_qt_SRCS ${gmic_qt_FORMS})

  add_definitions(-DGMIC_HOST=plugin8bf)
  add_executable(gmic_8bf_qt ${gmic_qt_SRCS} ${gmic_qt_QRC})
  target_link_libraries(
    gmic_8bf_qt
    PRIVATE
    ${gmic_qt_LIBRARIES}
    ${GMIC_QT_LCMS2_LIBRARIES}
    )

endif()

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
