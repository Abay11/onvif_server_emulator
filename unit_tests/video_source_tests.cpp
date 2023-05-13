#include <boost/test/unit_test.hpp>

#include "../utility/MediaProfilesManager.h"
#include "../utility/VideoSourceReader.h"

#include <boost/property_tree/json_parser.hpp>

#include <fstream>

const static std::string config_file = "../../server_configs/media_profiles.config";

BOOST_AUTO_TEST_CASE(VSConfigPathTest)
{
	std::ifstream ifile(config_file);
	BOOST_ASSERT(true == ifile.is_open());
}

BOOST_AUTO_TEST_CASE(VideoSourceConfigsReaderByTokenTest_0)
{
	pt::ptree configTree;
	pt::json_parser::read_json(config_file, configTree);

	const auto token = "VideoSrcConfigToken0";
	auto cfg = utility::VideoSourceConfigsReaderByToken(token, configTree).VideoSource();

	BOOST_TEST(token == cfg.get<std::string>("token"));
}

BOOST_AUTO_TEST_CASE(VideoSourceConfigsReaderByTokenTest_1)
{
	pt::ptree configTree;
	pt::json_parser::read_json(config_file, configTree);

	const auto token = "invalidToken";

	try
	{
		auto cfg = utility::VideoSourceConfigsReaderByToken(token, configTree).VideoSource();
	}
	catch(const osrv::invalid_token&)
	{
		return;
	}

	BOOST_TEST_FAIL("osrv::invalid_token was not throwed!");
}
