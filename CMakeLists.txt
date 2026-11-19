cmake_minimum_required(VERSION 3.16)

project(OnvifServerEmulator)

set(CMAKE_CXX_STANDARD 20)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
    message(STATUS "CMAKE_BUILD_TYPE is undefined. Set default build type ${CMAKE_BUILD_TYPE}.")
endif()

add_executable(main main.cpp)

if (NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "Only Linux builds are supported. On Windows use WSL2 and the Linux container; see docs/WSL2.md.")
endif()

find_package(Boost REQUIRED
	system
	date_time
	regex
)
include_directories(${Boost_INCLUDE_DIRS})
add_definitions(-DBOOST_ASIO_USE_TS_EXECUTOR_AS_DEFAULT) # helps to solve errors while building Simple-Web-Server with BOOST.Asio version greater or equal 1.74
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
	"onvif_services/IOnvifService.cpp"
	"onvif_services/IOnvifService.h"
	"onvif_services/analytics_service.cpp"
	"onvif_services/analytics_service.h"
	"onvif_services/device_service.cpp"
	"onvif_services/device_service.h"
	"onvif_services/deviceio_service.cpp"
	"onvif_services/deviceio_service.h"
	"onvif_services/discovery_service.cpp"
	"onvif_services/discovery_service.h"
	"onvif_services/event_service.cpp"
	"onvif_services/event_service.h"
	"onvif_services/imaging_service.cpp"
	"onvif_services/imaging_service.h"
	"onvif_services/media2_service.cpp"
	"onvif_services/media2_service.h"
	"onvif_services/media_service.cpp"
	"onvif_services/media_service.h"
	"onvif_services/physical_components/IPhysicalComponent.h"
	"onvif_services/ptz_service.cpp"
	"onvif_services/ptz_service.cpp"
	"onvif_services/ptz_service.h"
	"onvif_services/ptz_service.h"
	"onvif_services/pullpoint/event_generators.cpp"
	"onvif_services/pullpoint/event_generators.h"
	"onvif_services/pullpoint/pull_point.cpp"
	"onvif_services/pullpoint/pull_point.h"
	"onvif_services/recording_search_service.cpp"
	"onvif_services/recording_search_service.h"
	"onvif_services/replay_control_service.cpp"
	"onvif_services/replay_control_service.h"
  	"onvif_services/physical_components/IDigitalInput.cpp"
  	"onvif_services/physical_components/IDigitalInput.h"

	"onvif/Recording.h"
	"onvif/Recording.cpp"
	"onvif/OnvifRequest.h"	
)

FILE (GLOB UTILITY_SRC
	"utility/LineCounting.cpp"
	"utility/LineCounting.h"
	"utility/FieldDetector.cpp"
	"utility/FieldDetector.h"
	"utility/LineDetector.cpp"
	"utility/LineDetector.h"
	"utility/AnalyticsRules.cpp"
	"utility/AnalyticsRules.h"
	"utility/AnalyticsReader.cpp"
	"utility/AnalyticsReader.h"
	"utility/AudioSourceReader.cpp"
	"utility/AudioSourceReader.h"
	"utility/AuthHelper.cpp"
	"utility/AuthHelper.h"
	"utility/DateTime.hpp"
	"utility/EventService.cpp"
	"utility/EventService.h"
	"utility/HttpDigestHelper.cpp"
	"utility/HttpDigestHelper.h"
	"utility/HttpHelper.cpp"
	"utility/HttpHelper.h"
	"utility/MediaProfilesManager.cpp"
	"utility/MediaProfilesManager.h"
	"utility/PtzConfigurationReader.cpp"
	"utility/PtzConfigurationReader.h"
	"utility/SoapHelper.cpp"
	"utility/SoapHelper.h"
	"utility/VideoSourceReader.cpp"
	"utility/VideoSourceReader.h"
	"utility/XmlParser.cpp"
	"utility/XmlParser.h"
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

option(ENABLE_UNIT_TESTS "Build the emulator unit tests" OFF)
if(ENABLE_UNIT_TESTS)
	enable_testing()
	add_subdirectory(unit_tests)
endif()
