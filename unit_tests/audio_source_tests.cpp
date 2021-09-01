#include <boost/test/unit_test.hpp>

#include "../utility/AudioSourceReader.h"

#include <boost/property_tree/json_parser.hpp>

#include <fstream>

const static std::string config_file = "../../server_configs/media_profiles.config";

BOOST_AUTO_TEST_CASE(ASConfigPathTest)
{
	std::ifstream ifile(config_file);
	BOOST_ASSERT(true == ifile.is_open());
}


BOOST_AUTO_TEST_CASE(AudioEncoderReaderByTokenTest)
{
	pt::ptree configTree;
	pt::json_parser::read_json(config_file, configTree);

	const auto token = "AudioEncCfg0";
	auto aeCfg = utility::AudioEncoderReaderByToken(token, configTree).AudioEncoder();

	BOOST_TEST(token == aeCfg.get<std::string>("token"));
}

BOOST_AUTO_TEST_CASE(AudioEncoderReaderByProfileToken_RelatedAudioEncoderTokenTest)
{
	pt::ptree configTree;
	pt::json_parser::read_json(config_file, configTree);

	const auto profileToken = "ProfileToken0";

	auto aeCfgToken = utility::AudioEncoderReaderByProfileToken(profileToken, configTree).RelatedAudioEncoderToken();

	const auto token = "AudioEncCfg0";

	BOOST_TEST(token == aeCfgToken);
}

BOOST_AUTO_TEST_CASE(AudioEncoderReaderByProfileTokenTest)
{
	pt::ptree configTree;
	pt::json_parser::read_json(config_file, configTree);
	const auto profileToken = "ProfileToken0";
	auto aeCfg = utility::AudioEncoderReaderByProfileToken(profileToken, configTree).AudioEncoder();
	const auto token = "AudioEncCfg0";
	BOOST_TEST(token == aeCfg.get<std::string>("token"));
}
