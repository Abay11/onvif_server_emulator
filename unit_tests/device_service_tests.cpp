#include <boost/test/unit_test.hpp>

#include <boost/property_tree/json_parser.hpp>

#include <fstream>

namespace pt = boost::property_tree;

BOOST_AUTO_TEST_CASE(device_service_configs_test)
{
	// check real config file
	std::fstream config_file("../../server_configs/device.config", std::ios_base::in);
	if (!config_file.is_open())
		BOOST_TEST_FAIL("Could not open device config");

	pt::ptree configs;
	pt::read_json(config_file, configs);

	BOOST_CHECK_MESSAGE(!configs.empty(), "Could not open device config");

	auto getservices_configs = configs.find("GetServices");
	BOOST_CHECK_MESSAGE(getservices_configs != configs.not_found(), "Not found GetService configs");

	// BOOST_TEST();
}
