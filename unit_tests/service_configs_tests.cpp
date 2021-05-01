
#include <boost/test/unit_test.hpp>

#include "onvif_services/service_configs.h"

using namespace osrv;

BOOST_AUTO_TEST_CASE(ConfigNameTest0)
{
	const std::string EXPECTED = "testfilename.testext";
	const std::string ACTUAL = ConfigName("testfilename", "testext");
	BOOST_TEST(EXPECTED == ACTUAL);
}

BOOST_AUTO_TEST_CASE(ConfigNameTest1)
{
	const std::string EXPECTED = "testfilename.testext";
	const std::string ACTUAL = ConfigName("testfilename", ".testext");
	BOOST_TEST(EXPECTED == ACTUAL);
}

BOOST_AUTO_TEST_CASE(ConfigNameTest2)
{
	const std::string EXPECTED = "testfilename.config";
	const std::string ACTUAL = ConfigName("testfilename");
	BOOST_TEST(EXPECTED == ACTUAL);
}

BOOST_AUTO_TEST_CASE(ConfigNameTest3)
{
	const std::string EXPECTED = "testfilename";
	const std::string ACTUAL = ConfigName("testfilename", "");
	BOOST_TEST(EXPECTED == ACTUAL);
}

BOOST_AUTO_TEST_CASE(ConfigPathTest0)
{
	const std::string EXPECTED = "/test/home/testfilename.ext";
	const std::string ACTUAL = ConfigPath("/test/home/", "testfilename.ext");
	BOOST_TEST(EXPECTED == ACTUAL);
}

BOOST_AUTO_TEST_CASE(ConfigPathTest1)
{
	const std::string EXPECTED = "/test/home/testfilename.ext";
	const std::string ACTUAL = ConfigPath("/test/home", "testfilename.ext");
	BOOST_TEST(EXPECTED == ACTUAL);
}
