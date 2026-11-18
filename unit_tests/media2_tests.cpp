#include <boost/test/unit_test.hpp>

#include "../onvif_services/media2_service.h"
#include "../onvif_services/analytics_service.h"
#include "../utility/MediaProfilesManager.h"

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <vector>

BOOST_AUTO_TEST_CASE(analytics_configuration_uses_schema_field_order_and_reference_count)
{
	using boost::property_tree::ptree;
	utility::media::MediaProfilesManager manager("../../server_configs/media_profiles.config");
	auto& tree = manager.ReaderWriter()->ConfigsTree();
	auto config = tree.get_child("Analytics").front().second;
	const auto token = config.get<std::string>("token");
	// Exercise both unused and shared configurations without writing fixture files.
	for (int references : {0, 1, 3})
	{
		ptree profiles;
		for (int i = 0; i < references; ++i)
		{
			ptree profile;
			profile.put("Analytics", token);
			profiles.push_back({"", profile});
		}
		ptree unrelated;
		unrelated.put("Analytics", "OtherAnalyticsConfig");
		profiles.push_back({"", unrelated});
		tree.put_child("MediaProfiles", profiles);

		ptree result;
		osrv::analytics::fillConfiguration(config, result, manager);
		BOOST_TEST(result.get<std::string>("<xmlattr>.token") == token);
		BOOST_TEST(result.get<int>("tt:UseCount") == references);
		const std::vector<std::string> expected{
				"tt:Name", "tt:UseCount", "tt:AnalyticsEngineConfiguration", "tt:RuleEngineConfiguration"};
		std::vector<std::string> actual;
		for (const auto& [name, child] : result)
			if (name != "<xmlattr>")
				actual.push_back(name);
		BOOST_TEST(actual == expected, boost::test_tools::per_element());
	}
}

BOOST_AUTO_TEST_CASE(profiles_to_soap_func)
{
	using namespace osrv::media2::util;

	namespace pt = boost::property_tree;
	using ptree = boost::property_tree::ptree;

	const auto configPath = "../../unit_tests/test_data/media2_service_test.config";

	ptree configs_file;
	pt::json_parser::read_json(configPath, configs_file);

	utility::media::MediaProfilesManager profilesManager(configPath);

	// GENERAL FIELDS OF MEDIA PROFILE 1
	ptree profile_configs = configs_file.get_child("MediaProfiles").front().second;
	ptree result_profile_tree;
	profile_to_soap(profile_configs, configs_file, result_profile_tree, profilesManager);

	auto token = result_profile_tree.get<std::string>("<xmlattr>.token");
	BOOST_TEST(token == "ProfileToken0");
	auto isFixed = result_profile_tree.get<bool>("<xmlattr>.fixed");
	BOOST_TEST(isFixed);
	auto name = result_profile_tree.get<std::string>("tr2:Name");
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
	profile_to_soap(profile2_configs, configs_file, result_profile_tree2, profilesManager);
	BOOST_TEST(result_profile_tree2.get<std::string>("<xmlattr>.token") == "ProfileToken1");

	// TESTS FOR ROOT TREE
	ptree result_root_tree;
	result_root_tree.add_child("tr2:Profiles", result_profile_tree);
	result_root_tree.add_child("tr2:Profiles", result_profile_tree2);
	BOOST_TEST(result_root_tree.size() == 2);

	BOOST_TEST(result_root_tree.front().second.get<std::string>("<xmlattr>.token") == "ProfileToken0");
	BOOST_TEST(result_root_tree.back().second.get<std::string>("<xmlattr>.token") == "ProfileToken1");
}

#include "../Server.h"
#include "../onvif_services/media_service.h"

BOOST_AUTO_TEST_CASE(media_urls_use_advertised_ports)
{
    osrv::ServerConfigs cfg{};
    cfg.ipv4_address_ = "0.0.0.0";
    cfg.http_port_ = "8080";
    cfg.rtsp_port_ = "8554";
    cfg.advertised_ipv4_ = "192.168.1.10";
    cfg.enabled_http_port_forwarding = true;
    cfg.forwarded_http_port = 1080;
    // Regression: snapshot previously tested the RTSP forwarding flag.
    BOOST_TEST(osrv::media2::util::generate_snapshot_url(cfg) == "http://192.168.1.10:1080/snapshot.jpeg");
    cfg.advertised_rtsp_port_ = 18554;
    BOOST_TEST(osrv::media::util::generate_rtsp_url(cfg, "Live&HighStream") ==
        "rtsp://192.168.1.10:18554/Live&HighStream");
}
