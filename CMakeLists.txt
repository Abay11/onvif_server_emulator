cmake_minimum_required(VERSION 3.9)

project(OnvifServer)

set(CMAKE_CXX_STANDARD 14)

add_executable(main main.cpp third/http_parser/http_parser.c)
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT main)

IF(WIN32)
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
