cmake_minimum_required (VERSION 4.2.0 FATAL_ERROR)

set (CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
set (CMAKE_CONFIGURATION_TYPES Debug Release)

set (CMAKE_CXX_FLAGS_RELEASE "/O1 /Ob1 /clang:-Oz /GF /GS- /Gy /Gw /DNDEBUG")
set (CMAKE_EXE_LINKER_FLAGS_RELEASE "/INCREMENTAL:NO /OPT:REF /OPT:ICF /DEBUG:NONE")

file (READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" VERSION)
string (STRIP "${VERSION}" VERSION)

project (
	ultraBattery
	VERSION ${VERSION}
	LANGUAGES CXX)

set_property (DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")

set (CMAKE_CXX_STANDARD 23)
set (CMAKE_CXX_STANDARD_REQUIRED ON)
set (CMAKE_CXX_EXTENSIONS OFF)

add_executable (ultraBattery WIN32
	Source/Autostart.cpp
	Source/Autostart.h
	Source/Battery.cpp
	Source/Battery.h
	Source/GUI_BatteryIcon.cpp
	Source/GUI_BatteryIcon.h
	Source/Main.cpp)

target_compile_definitions (ultraBattery PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _HAS_EXCEPTIONS=0 APP_VERSION="${VERSION}")
target_compile_options (ultraBattery PRIVATE /EHs-c-)
target_link_libraries (ultraBattery PRIVATE advapi32 bthprops cfgmgr32 shell32 gdi32 user32)
set_property (DIRECTORY PROPERTY VS_STARTUP_PROJECT ultraBattery)
