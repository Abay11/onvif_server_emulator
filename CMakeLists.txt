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

add_subdirectory(Simple-Web-Server)

FILE(GLOB SERVICES_SRC
	"onvif_services/device_service.h"
	"onvif_services/device_service.cpp"
)

FILE (GLOB UTILITY_SRC
	"utility/XmlParser.h"
	"utility/XmlParser.cpp"
	"utility/HttpHelper.h"
	"utility/SoapHelper.h"
	"utility/SoapHelper.cpp"
)

add_executable(main
	main.cpp
	Logger.hpp
	Types.inl
	Server.cpp
	Server.h
	"${SERVICES_SRC}"
	"${UTILITY_SRC}"
	)

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

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT main)
