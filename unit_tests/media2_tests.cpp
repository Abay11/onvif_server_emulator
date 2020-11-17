#include <boost/test/unit_test.hpp>

#include "../onvif_services/media2_service.h"

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

BOOST_AUTO_TEST_CASE(profiles_to_soap_func)
{
	using namespace osrv::media2::util;

	namespace pt = boost::property_tree;
	using ptree = boost::property_tree::ptree;

	ptree configs_file;
	pt::json_parser::read_json("../../unit_tests/test_data/media2_service_test.config", configs_file);

	// GENERAL FIELDS OF MEDIA PROFILE 1
	ptree profile_configs = configs_file.get_child("MediaProfiles").front().second;
	ptree result_profile_tree;
	profile_to_soap(profile_configs, configs_file, result_profile_tree);

	auto token = result_profile_tree.get<std::string>("<xmlattr>.token");
	BOOST_TEST(token == "ProfileToken0");
	auto isFixed = result_profile_tree.get<bool>("<xmlattr>.fixed");
	BOOST_TEST(isFixed);
	auto name = result_profile_tree.get<std::string>("Name");
	BOOST_TEST(name == "MainProfile");

	// VIDEOSOURCE
	// TODO: add check of other items if required, here is only for a few values
	// Also note, all test data is fixed
	auto vs_config_result = result_profile_tree.get_child("tr2:Configurations.tr2:VideoSource");
	BOOST_TEST(vs_config_result.get<std::string>("<xmlattr>.token") == "VideoSrcConfigToken0");
	BOOST_TEST(vs_config_result.get<std::string>("tt:Name") == "VideoSrcConfigToken0");


	// VIDEOENCODERS
	auto ve_config_result = result_profile_tree.get_child("tr2:Configurations.tr2:VideoEncoder");
	BOOST_TEST(ve_config_result.get<std::string>("<xmlattr>.token") == "VideoEncoderToken0");
	BOOST_TEST(ve_config_result.get<std::string>("tt:Name") == "VideoEncConfig0");

	
	// Just make sure that the profile node also is in the result tree
	ptree profile2_configs = configs_file.get_child("MediaProfiles").back().second;
	ptree result_profile_tree2;
	profile_to_soap(profile2_configs, configs_file, result_profile_tree2);
	BOOST_TEST(result_profile_tree2.get<std::string>("<xmlattr>.token") == "ProfileToken1");


	// TESTS FOR ROOT TREE
	ptree result_root_tree;
	result_root_tree.add_child("tr2:Profiles", result_profile_tree);
	result_root_tree.add_child("tr2:Profiles", result_profile_tree2);
	BOOST_TEST(result_root_tree.size() == 2);
	
	BOOST_TEST(result_root_tree.front().second.get<std::string>("<xmlattr>.token") == "ProfileToken0");
	BOOST_TEST(result_root_tree.back().second.get<std::string>("<xmlattr>.token") == "ProfileToken1");
}
