cmake_minimum_required(VERSION 3.16)

project(OnvifServerUnitTests)

set(CMAKE_CXX_STANDARD 20)

find_package(Boost REQUIRED
	date_time
	unit_test_framework
)

add_executable(test_executable
	analytics_configuration_reader_test.cpp
	audio_source_tests.cpp
	auth_tests.cpp	
	date_time_tests.cpp
	device_service_tests.cpp
	discovery_tests.cpp
	event_service_tests.cpp	
	http_da_tests.cpp
	media2_tests.cpp
	mediaprofiles_manager_tests.cpp
	pull_point_tests.cpp
	recording_tests.cpp
	server_tests.cpp	
	service_configs_tests.cpp
	tests_main.cpp
	video_source_tests.cpp
	xmlparser_tests.cpp
	xmlparser_tests.cpp
)

# indicates the include paths
target_include_directories(test_executable PRIVATE ${Boost_INCLUDE_DIRS})

# indicates the shared library variant
target_compile_definitions(test_executable PRIVATE "BOOST_TEST_DYN_LINK=1")

target_link_directories(test_executable PRIVATE ${GSTREAMER_LIBRARY_DIRS})

# indicates the link paths
target_link_libraries(test_executable onvif_server
Boost::date_time
${Boost_UNIT_TEST_FRAMEWORK_LIBRARY}
"${GSTREAMER_LIBRARIES}")

# declares a test with our executable
add_test(NAME test1 COMMAND test_executable)

set_target_properties(main PROPERTIES VS_DEBUGGER_COMMAND_ARGUMENTS
	"--run_test=* --detect_memory_leaks=0 --log_level=all")