cmake_minimum_required(VERSION 3.16)

project(OnvifServerUnitTests)

set(CMAKE_CXX_STANDARD 11)

find_package(Boost REQUIRED
	unit_test_framework
)

add_executable(test_executable
	tests_main.cpp
	auth_tests.cpp
	http_da_tests.cpp
	../utility/HttpDigestHelper.cpp
	../utility/HttpHelper.cpp
	../utility/AuthHelper.cpp
)

# indicates the include paths
target_include_directories(test_executable PRIVATE ${Boost_INCLUDE_DIRS})

# indicates the shared library variant
target_compile_definitions(test_executable PRIVATE "BOOST_TEST_DYN_LINK=1")
# indicates the link paths
target_link_libraries(test_executable ${Boost_UNIT_TEST_FRAMEWORK_LIBRARY})

# declares a test with our executable
add_test(NAME test1 COMMAND test_executable)