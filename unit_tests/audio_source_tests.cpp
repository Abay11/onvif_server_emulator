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

BOOST_AUTO_TEST_CASE(AudioSourceConfigsReaderTest)
{
	pt::ptree configTree;
	pt::json_parser::read_json(config_file, configTree);
	const auto token = "AudioSrcCfg0";
	auto asCfg = utility::AudioSourceConfigsReader(token, configTree).AudioSource();
	BOOST_TEST(token == asCfg.get<std::string>("token"));
}


BOOST_AUTO_TEST_CASE(PcmuSetupTest)
{
	auto pcmu = utility::AudioSetupFactory().AudioSetup("PCMU");

	BOOST_TEST("alawenc bitrate=64000" == pcmu->Encoding());
	BOOST_TEST(0u == pcmu->PayloadNum());
	BOOST_TEST("rtppcmapay" == pcmu->PayloadPluginName());
}

BOOST_AUTO_TEST_CASE(PcmaSetupTest)
{
	auto pcma = utility::AudioSetupFactory().AudioSetup("PCMA");

	BOOST_TEST("mulawenc bitrate=64000" == pcma->Encoding());
	BOOST_TEST(8u == pcma->PayloadNum());
	BOOST_TEST("rtppcmapay" == pcma->PayloadPluginName());
}

BOOST_AUTO_TEST_CASE(G726SetupTest)
{
	auto g726 = utility::AudioSetupFactory().AudioSetup("G726", 16000, 2);

	BOOST_TEST("avenc_g726 bitrate=16000" == g726->Encoding());
	BOOST_TEST(97u == g726->PayloadNum());
	BOOST_TEST("rtpg726pay" == g726->PayloadPluginName());
}

BOOST_AUTO_TEST_CASE(AacSetupTest)
{
	auto aac = utility::AudioSetupFactory().AudioSetup("AAC", 44100, 3);

	BOOST_TEST("avenc_aac bitrate=44100" == aac->Encoding());
	BOOST_TEST(98u == aac->PayloadNum());
	BOOST_TEST("rtpmp4apay" == aac->PayloadPluginName());
}
