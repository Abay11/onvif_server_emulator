cmake_minimum_required(VERSION 3.16)

project(OnvifServer)

set(CMAKE_CXX_STANDARD 11)

find_package(Boost REQUIRED
	system
	date_time
	regex
)

IF(NOT Boost_FOUND)
	message(FATAL_ERROR "Not found Boost")
ELSE()
	message("+++ Boost INCLUDE_DIR is ${Boost_INCLUDE_DIRS}")
	include_directories(${Boost_INCLUDE_DIRS})
ENDIF()	

#TODO: getting values from a user
set(PKG_CONFIG_INSTALLATION_PATH "C:/dev/pkg-config-lite-0.28-1_bin-win32/pkg-config-lite-0.28-1/bin")
set(GST_INSTALLATION_PATH "C:/dev")
set(PKG_CONFIG_EXECUTABLE "${PKG_CONFIG_INSTALLATION_PATH}/pkg-config.exe" CACHE FILEPATH "")
set(ENV{PKG_CONFIG_PATH} "${GST_INSTALLATION_PATH}/gstreamer/1.0/x86_64/lib/pkgconfig")

find_package(PkgConfig REQUIRED)

pkg_check_modules(GST REQUIRED gstreamer-1.0 gstreamer-rtsp-server-1.0)

add_subdirectory(Simple-Web-Server)

FILE(GLOB SERVICES_SRC
	"onvif_services/device_service.h"
	"onvif_services/device_service.cpp"
	"onvif_services/media_service.h"
	"onvif_services/media_service.cpp"
	"onvif_services/event_service.h"
	"onvif_services/event_service.cpp"
	"onvif_services/pull_point.h"
	"onvif_services/pull_point.cpp"
)

FILE (GLOB UTILITY_SRC
	"utility/XmlParser.h"
	"utility/XmlParser.cpp"
	"utility/HttpHelper.h"
	"utility/HttpHelper.cpp"
	"utility/SoapHelper.h"
	"utility/SoapHelper.cpp"
	"utility/AuthHelper.h"
	"utility/AuthHelper.cpp"
	"utility/HttpDigestHelper.h"
	"utility/HttpDigestHelper.cpp"
	"utility/DateTime.hpp"
)

source_group("OnvifServices" FILES ${SERVICES_SRC})
source_group("Utility" FILES ${UTILITY_SRC})

add_library(onvif_server STATIC
	Logger.hpp
	Types.inl
	Server.cpp
	Server.h
	RtspServer.cpp
	RtspServer.h
	"${SERVICES_SRC}"
	"${UTILITY_SRC}"
)

add_executable(main main.cpp)

#pass application CONFIGS_FILES directory as command line arguments
set_target_properties(main PROPERTIES VS_DEBUGGER_COMMAND_ARGUMENTS
	"${CMAKE_CURRENT_SOURCE_DIR}/server_configs")

IF(WIN32)
	### JUST HELPS TO AVOID WARNINGS WITH Boost
	add_definitions(-D_WIN32_WINDOWS)
ENDIF()

set(THIRD_LIBS_DIR
	"${CMAKE_CURRENT_SOURCE_DIR}/third"
)

include_directories(
	"${THIRD_LIBS_DIR}"
)

include_directories(main "${GST_INCLUDE_DIRS}")
target_compile_options(main PUBLIC "${GST_CFLAGS}")
target_link_directories(main PUBLIC "${GST_INSTALLATION_PATH}/gstreamer/1.0/x86_64/lib")
target_link_libraries(main
	onvif_server
	"${GST_LIBRARIES}"
	Boost::date_time
	)

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT main)

add_subdirectory(unit_tests)

#copy config files to the same folder with the execution for standalone running .exe outside IDE
execute_process(COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/server_configs"
	"${CMAKE_CURRENT_BINARY_DIR}/Debug/server_configs")
