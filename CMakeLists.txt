cmake_minimum_required(VERSION 3.16)

project(OnvifServerEmulator)

set(CMAKE_CXX_STANDARD 20)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
    message(STATUS "CMAKE_BUILD_TYPE is undefined. Set default build type ${CMAKE_BUILD_TYPE}.")
endif()

add_executable(main main.cpp)

if (MSVC)
	if ("${CMAKE_TOOLCHAIN_FILE}" STREQUAL "")
		message( FATAL_ERROR "Please install vcpkg and set the path! Ex.:  -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake" )
	endif()
endif()

find_package(Boost REQUIRED
	system
	date_time
	regex
)
include_directories(${Boost_INCLUDE_DIRS})
add_definitions(-DBOOST_ASIO_USE_TS_EXECUTOR_AS_DEFAULT) # helps to solve errors while building Simple-Web-Server with BOOST.Asio version greater or equal 1.74
if (MSVC)
	if (WIN32)
		add_definitions(-D_WIN32_WINDOWS=0x0601) # helps to supress BOOST warnings
	endif()
endif()
add_definitions(-DBOOST_ASIO_USE_TS_EXECUTOR_AS_DEFAULT)


find_package(PkgConfig REQUIRED)
pkg_check_modules(GSTREAMER REQUIRED gstreamer-rtsp-server-1.0)
message(STATUS "gstreamer lib dir: ${GSTREAMER_LIBRARY_DIRS}")
message(STATUS "gstreamer include dir ${GSTREAMER_INCLUDE_DIRS}")
include_directories(${GSTREAMER_INCLUDE_DIRS})
target_link_directories(main PRIVATE ${GSTREAMER_LIBRARY_DIRS})

set(SimpleWebServerDir "${PROJECT_SOURCE_DIR}/Simple-Web-Server")
if(NOT EXISTS "${SimpleWebServerDir}/CMakeLists.txt")
	find_package(Git QUIET)
	if(GIT_FOUND AND EXISTS "${PROJECT_SOURCE_DIR}/.git")
		option(GIT_SUBMODULE "Check submodules during build" ON)
		if(GIT_SUBMODULE)
			message(STATUS "Submodule update")
			
			execute_process(COMMAND ${GIT_EXECUTABLE} submodule update --init --recursive "${SimpleWebServerDir}"
							WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
							RESULT_VARIABLE GIT_SUBMOD_RESULT)
			
			if(NOT GIT_SUBMOD_RESULT EQUAL "0")
				message(FATAL_ERROR "git submodule update --init --recursive failed with ${GIT_SUBMOD_RESULT}, please checkout submodules")
			endif()
		endif()
	endif()
endif()

if(NOT EXISTS "${SimpleWebServerDir}/CMakeLists.txt")
    message(FATAL_ERROR "The submodules were not downloaded! GIT_SUBMODULE was turned off or failed. Please update submodules and try again.")
endif()

add_subdirectory(Simple-Web-Server)



FILE(GLOB SERVICES_SRC
	"onvif_services/IOnvifService.h"
	"onvif_services/IOnvifService.cpp"
	"onvif_services/device_service.h"
	"onvif_services/device_service.cpp"
	"onvif_services/deviceio_service.h"
	"onvif_services/deviceio_service.cpp"
	"onvif_services/media_service.h"
	"onvif_services/media_service.cpp"
	"onvif_services/media2_service.h"
	"onvif_services/media2_service.cpp"
	"onvif_services/event_service.h"
	"onvif_services/event_service.cpp"
	"onvif_services/imaging_service.h"
	"onvif_services/imaging_service.cpp"
	"onvif_services/pullpoint/pull_point.h"
	"onvif_services/pullpoint/pull_point.cpp"
	"onvif_services/discovery_service.h"
	"onvif_services/discovery_service.cpp"
	"onvif_services/pullpoint/event_generators.h"
	"onvif_services/pullpoint/event_generators.cpp"
	"onvif_services/physical_components/IPhysicalComponent.h"
  	"onvif_services/physical_components/IDigitalInput.h"
  	"onvif_services/physical_components/IDigitalInput.cpp"
	"onvif_services/ptz_service.h"
	"onvif_services/ptz_service.cpp"
	"onvif_services/ptz_service.h"
	"onvif_services/ptz_service.cpp"
	"onvif_services/recording_search_service.h"
	"onvif_services/recording_search_service.cpp"
	"onvif_services/replay_control_service.h"
	"onvif_services/replay_control_service.cpp"

	"onvif/Recording.h"
	"onvif/Recording.cpp"
	"onvif/OnvifRequest.h"	
)

FILE (GLOB UTILITY_SRC
	"utility/AudioSourceReader.h"
	"utility/AudioSourceReader.cpp"
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
	"utility/EventService.h"
	"utility/EventService.cpp"
	"utility/MediaProfilesManager.h"
	"utility/MediaProfilesManager.cpp"
)

source_group("OnvifServices" FILES ${SERVICES_SRC})
source_group("Utility" FILES ${UTILITY_SRC})

add_library(onvif_server STATIC
	Logger.h
	HttpServerFwd.h
	include/IOnvifServer.h
	src/IOnvifServer.cpp
	Server.cpp
	Server.h
	RtspServer.cpp
	RtspServer.h
	include/onvif_services/service_configs.h
    src/onvif_services/service_configs.cpp
	"${SERVICES_SRC}"
	"${UTILITY_SRC}"

	include/LoggerFactories.h
	include/ConsoleLogger.h
	include/FileLogger.h
	include/StreamLogger.h
	include/MediaFormats.h
)

#pass application CONFIGS_FILES directory as command line arguments
set_target_properties(main PROPERTIES VS_DEBUGGER_COMMAND_ARGUMENTS
	"${CMAKE_CURRENT_SOURCE_DIR}/server_configs")

include_directories(main
	"${CMAKE_CURRENT_SOURCE_DIR}/include"
	"${CMAKE_CURRENT_SOURCE_DIR}/Simple-Web-Server"
	"${GSTREAMER_INCLUDE_DIRS}"
)
	
target_compile_options(main PUBLIC "${GSTREAMER_CFLAGS}")
target_link_libraries(main PUBLIC
	onvif_server
	${GSTREAMER_LIBRARIES}
	Boost::date_time
)

if (MSVC)
	set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT main)
endif()

option(ENABLE_UNIT_TESTS OFF)

if(ENABLE_UNIT_TESTS)
	add_subdirectory(unit_tests)
endif()

#copy config files to the same folder with the execution for standalone running .exe outside IDE
execute_process(COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/server_configs"
	"${CMAKE_CURRENT_BINARY_DIR}/Debug/server_configs")