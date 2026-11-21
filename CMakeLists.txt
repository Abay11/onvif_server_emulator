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
find_package(OpenSSL REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GSTREAMER REQUIRED gstreamer-rtsp-server-1.0 gstreamer-rtsp-1.0 gstreamer-app-1.0 gstreamer-video-1.0)
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
	"onvif_services/EventSource.cpp"
	"onvif_services/EventSource.h"
	"onvif_services/imaging_service.cpp"
	"onvif_services/imaging_service.h"
	"onvif_services/media2_service.cpp"
	"onvif_services/media2_service.h"
	"onvif_services/media_service.cpp"
	"onvif_services/metadata_handlers.cpp"
	"onvif_services/metadata_handlers.h"
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
	"onvif/Notification.cpp"
	"onvif/SceneDescription.cpp"
	"onvif/SceneDescription.h"
	"onvif/Notification.h"
	"onvif/Recording.cpp"
	"onvif/OnvifRequest.h"	
)

FILE (GLOB UTILITY_SRC
	"utility/LineCounting.cpp"
	"utility/LineCounting.h"
	"utility/FaceRecognition.cpp"
	"utility/FaceRecognition.h"
	"utility/LicensePlateRecognition.cpp"
	"utility/LicensePlateRecognition.h"
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
	"utility/RequestAuthentication.cpp"
	"utility/HttpDigestHelper.cpp"
	"utility/HttpDigestHelper.h"
	"utility/HttpHelper.cpp"
	"utility/HttpHelper.h"
	"utility/MetadataConfiguration.cpp"
	"utility/MetadataConfiguration.h"
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
	web/AdminUi.cpp
	web/AdminUi.h
	web/WebAuthentication.cpp
	web/WebAuthentication.h
	utility/ConfigValidation.cpp
	utility/ConfigValidation.h
	utility/DetectorControls.cpp
	utility/DetectorControls.h
	web/ConfigurationUi.cpp
	web/ConfigurationUi.h
	web/DetectorUi.cpp
	web/DetectorUi.h
	utility/SystemJournal.cpp
	utility/SystemJournal.h
	utility/ApplicationReboot.cpp
	utility/ApplicationReboot.h
	RtspServer.cpp
	RtspServer.h
	rtsp/MetadataStream.cpp
	rtsp/MetadataStream.h
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

# The application reuses the bundled offline JSON Schema checker before writes.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
execute_process(COMMAND "${Python3_EXECUTABLE}" -c "from jsonschema import Draft202012Validator; from referencing import Registry"
    RESULT_VARIABLE CONFIG_VALIDATION_IMPORT_RESULT ERROR_QUIET)
if(NOT CONFIG_VALIDATION_IMPORT_RESULT EQUAL 0)
    message(FATAL_ERROR "Configuration validation requires Python jsonschema and referencing; install python3-jsonschema or schemas/requirements.txt for ${Python3_EXECUTABLE}")
endif()
set(CONFIG_VALIDATION_DIR "${CMAKE_CURRENT_BINARY_DIR}/config_validation")
file(MAKE_DIRECTORY "${CONFIG_VALIDATION_DIR}/scripts" "${CONFIG_VALIDATION_DIR}/schemas")
configure_file(scripts/validate_configs.py "${CONFIG_VALIDATION_DIR}/scripts/validate_configs.py" COPYONLY)
file(GLOB VALIDATION_SCHEMAS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/schemas/*.json")
foreach(SCHEMA_FILE IN LISTS VALIDATION_SCHEMAS)
    get_filename_component(SCHEMA_NAME "${SCHEMA_FILE}" NAME)
    configure_file("${SCHEMA_FILE}" "${CONFIG_VALIDATION_DIR}/schemas/${SCHEMA_NAME}" COPYONLY)
endforeach()
target_compile_definitions(onvif_server PRIVATE
    OSRV_CONFIG_VALIDATION_DIR="${CONFIG_VALIDATION_DIR}"
    OSRV_VALIDATION_PYTHON="${Python3_EXECUTABLE}")

# Embed the UI so native and container builds need no extra runtime asset mount.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/web/index.html"
    "${CMAKE_CURRENT_SOURCE_DIR}/web/login.html"
    "${CMAKE_CURRENT_SOURCE_DIR}/web/login.js"
    "${CMAKE_CURRENT_SOURCE_DIR}/web/session.js"
    "${CMAKE_CURRENT_SOURCE_DIR}/web/app.css"
    "${CMAKE_CURRENT_SOURCE_DIR}/web/app.js"
    "${CMAKE_CURRENT_SOURCE_DIR}/web/detectors.js"
    "${CMAKE_CURRENT_SOURCE_DIR}/web/configuration.js")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/web/index.html" ADMIN_HTML)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/web/login.html" LOGIN_HTML)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/web/login.js" LOGIN_JS)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/web/session.js" SESSION_JS)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/web/app.css" ADMIN_CSS)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/web/app.js" ADMIN_JS)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/web/detectors.js" DETECTORS_JS)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/web/configuration.js" CONFIGURATION_JS)
configure_file(web/Assets.h.in generated/AdminAssets.h @ONLY)
target_include_directories(onvif_server PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")

include_directories(main
	"${CMAKE_CURRENT_SOURCE_DIR}/include"
	"${CMAKE_CURRENT_SOURCE_DIR}/Simple-Web-Server"
	"${GSTREAMER_INCLUDE_DIRS}"
)
	
target_compile_options(main PUBLIC "${GSTREAMER_CFLAGS}")
target_link_libraries(onvif_server PUBLIC OpenSSL::Crypto)
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
