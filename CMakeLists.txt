cmake_minimum_required(VERSION 3.9)

project(OnvifServer)

set(CMAKE_CXX_STANDARD 14)

add_executable(main main.cpp third/http_parser/http_parser.c)
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT main)


set(HTTP_PARSER_DIR
	"${CMAKE_CURRENT_SOURCE_DIR}/third/http_parser"
)

FILE(GLOB HTTP_PARSER_SRC	
	"${HTTP_PARSER_DIR}/http_parser.c"
)

source_group("http_parser" FILES ${HTTP_PARSER_SRC})

include_directories(
	"${HTTP_PARSER_DIR}"
)
