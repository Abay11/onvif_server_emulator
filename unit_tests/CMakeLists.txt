cmake_minimum_required(VERSION 3.16)

project(OnvifServerUnitTests)

set(CMAKE_CXX_STANDARD 11)

find_package(Boost REQUIRED
	date_time
	unit_test_framework
)

add_executable(test_executable
	tests_main.cpp
	auth_tests.cpp
	media2_tests.cpp
	http_da_tests.cpp
	server_tests.cpp
	date_time_tests.cpp
	pull_point_tests.cpp
	xmlparser_tests.cpp
)

# indicates the include paths
target_include_directories(test_executable PRIVATE ${Boost_INCLUDE_DIRS})

# indicates the shared library variant
target_compile_definitions(test_executable PRIVATE "BOOST_TEST_DYN_LINK=1")

target_link_directories(test_executable PUBLIC "${GST_INSTALLATION_PATH}/gstreamer/1.0/x86_64/lib")
# indicates the link paths
target_link_libraries(test_executable onvif_server
Boost::date_time
${Boost_UNIT_TEST_FRAMEWORK_LIBRARY}
"${GST_LIBRARIES}")

# declares a test with our executable
add_test(NAME test1 COMMAND test_executable)