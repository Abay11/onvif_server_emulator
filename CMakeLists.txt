cmake_minimum_required(VERSION 3.9)

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
	LINK_DIRECTORIES(${Boost_LIBRARY_DIRS})
ENDIF()

add_executable(main
	main.cpp
	Server.cpp
	Server.h
	Logger.hpp
	third/http_parser/http_parser.c)
target_link_libraries(main ${Boost_LIBRARIES})

IF(WIN32)
	### JUST HELPS TO AVOID WARNINGS WITH Boost
	add_definitions(-D_WIN32_WINDOWS)
ENDIF()

set(THIRD_LIBS_DIR
	"${CMAKE_CURRENT_SOURCE_DIR}/third"
)

FILE(GLOB HTTP_PARSER_SRC	
	"${THIRD_LIBS_DIR}/http_parser/http_parser.c"
)

source_group("http_parser" FILES ${HTTP_PARSER_SRC})

include_directories(
	"${THIRD_LIBS_DIR}"
)

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT main)
