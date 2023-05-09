#include "../utility/MediaProfilesManager.h"

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_CASE(ConfigsReaderWriter_test0)
{
	using namespace utility::media;

	ConfigsReaderWriter readerWriter{"../../unit_tests/test_data/mediaprofiles_manager_test.config"};
	readerWriter.Read();
	BOOST_TEST(2 == readerWriter.ConfigsTree().get_child("MediaProfiles").size());

	std::string defaultValue{""};
	const auto& [n0, profile0_tree] = readerWriter.ConfigsTree().get_child("MediaProfiles").front();
	BOOST_TEST("ProfileToken0", profile0_tree.get("token", defaultValue));

	const auto& [n1, profile1_tree] = readerWriter.ConfigsTree().get_child("MediaProfiles").back();
	BOOST_TEST("ProfileToken1", profile1_tree.get("token", defaultValue));
}

BOOST_AUTO_TEST_CASE(ConfigsReaderWriter_test1)
{
	using namespace utility::media;

	ConfigsReaderWriter readerWriter{"../../unit_tests/test_data/mediaprofiles_manager_test.config"};
	readerWriter.Read();

	std::string defaultValue{""};
	std::string expectedOldValue{"MainProfile"};
	std::string newValue{"NewMainProfileName"};

	auto& [n0, profile0_tree] = readerWriter.ConfigsTree().get_child("MediaProfiles").front();

	// find and check an item's value
	auto it = profile0_tree.find("Name");
	BOOST_ASSERT(it != profile0_tree.not_found());
	BOOST_TEST(it->second.get_value<std::string>() == expectedOldValue);

	// then change the value
	profile0_tree.put("Name", newValue);

	// and save it
	readerWriter.Save();

	// read it and make sure the value was updated
	readerWriter.Read();
	auto& [n2, updated_profile0_tree] = readerWriter.ConfigsTree().get_child("MediaProfiles").front();
	auto it2 = updated_profile0_tree.find("Name");
	BOOST_ASSERT(it2 != updated_profile0_tree.not_found());
	BOOST_TEST(it2->second.get_value<std::string>() == newValue);

	// lets now restore value
	readerWriter.Reset();
	auto& [n3, restored_profile0_tree] = readerWriter.ConfigsTree().get_child("MediaProfiles").front();
	auto it3 = restored_profile0_tree.find("Name");
	BOOST_ASSERT(it3 != restored_profile0_tree.not_found());
	BOOST_TEST(it3->second.get_value<std::string>() == expectedOldValue);
}

BOOST_AUTO_TEST_CASE(MediaProfilesManager_GetProfileByToken_test0)
{
	using namespace utility::media;

	const std::string path{"../../unit_tests/test_data/mediaprofiles_manager_test.config"};

	const std::string profileToken{"ProfileToken0"};
	MediaProfilesManager manager(path);

	BOOST_TEST(true == manager.GetProfileByToken(profileToken).get<bool>("fixed"));
}

BOOST_AUTO_TEST_CASE(MediaProfilesManager_GetProfileByToken_test1)
{
	using namespace utility::media;

	const std::string path{"../../unit_tests/test_data/mediaprofiles_manager_test.config"};

	const std::string profileToken{"NotExistingProfileWithSuchToken"};
	MediaProfilesManager manager(path);

	try
	{
		manager.GetProfileByToken(profileToken);
	}
	catch (const osrv::no_such_profile&)
	{
		// it's ok - we expected exception will be thrawn
		return;
	}

	// it's not ok, it should not reach here
	BOOST_ASSERT(false);
}

BOOST_AUTO_TEST_CASE(MediaProfilesManager_GetProfileByToken_test2)
{
	// here we try to get media profile with only requested configuration
	using namespace utility::media;

	const std::string path{"../../unit_tests/test_data/mediaprofiles_manager_test.config"};

	const std::string profileToken{"ProfileToken0"};
	const std::vector<std::string> requiredProfilesConfigs{};
	MediaProfilesManager manager(path);

	auto actualProfileConfig = manager.GetProfileByToken(profileToken, requiredProfilesConfigs);
	BOOST_TEST(3 == actualProfileConfig.size());
	BOOST_TEST("ProfileToken0" == actualProfileConfig.get<std::string>("token"));
	BOOST_TEST(true == actualProfileConfig.get<bool>("fixed"));
	BOOST_TEST("MainProfile" == actualProfileConfig.get<std::string>("Name"));
}

BOOST_AUTO_TEST_CASE(MediaProfilesManager_GetProfileByToken_test3)
{
	// here we try to get media profile with only requested configuration
	using namespace utility::media;

	const std::string path{"../../unit_tests/test_data/mediaprofiles_manager_test.config"};

	const std::string profileToken{"ProfileToken1"};
	const std::vector<std::string> requiredProfilesConfigs{"VideoSource", "VideoEncoder"};
	MediaProfilesManager manager(path);

	auto actualProfileConfig = manager.GetProfileByToken(profileToken, requiredProfilesConfigs);
	BOOST_TEST(5 == actualProfileConfig.size());
	BOOST_TEST("ProfileToken1", actualProfileConfig.get<std::string>("token"));
	BOOST_TEST(true, actualProfileConfig.get<bool>("fixed"));
	BOOST_TEST("SecondProfile", actualProfileConfig.get<std::string>("Name"));
	BOOST_TEST("VideoSrcConfigToken0", actualProfileConfig.get<std::string>("VideoSource"));
	BOOST_TEST("VideoEncoderToken1", actualProfileConfig.get<std::string>("VideoEncoder"));
}

BOOST_AUTO_TEST_CASE(MediaProfilesManager_GetProfileByToken_test4)
{
	// here we try to get media profile with only requested configuration
	using namespace utility::media;

	const std::string path{"../../unit_tests/test_data/mediaprofiles_manager_test.config"};

	const std::string profileToken{"ProfileToken1"};
	const std::vector<std::string> requiredProfilesConfigs{
			"AudioSource", "AudioEncoder"}; // non existing configs in the profile, check there wont be exeptions
	MediaProfilesManager manager(path);

	auto actualProfileConfig = manager.GetProfileByToken(profileToken, requiredProfilesConfigs);
	BOOST_TEST(3 == actualProfileConfig.size());
}

BOOST_AUTO_TEST_CASE(MediaProfilesManager_GetProfileByName_test0)
{
	using namespace utility::media;

	const std::string path{"../../unit_tests/test_data/mediaprofiles_manager_test.config"};

	const std::string profileName{"SecondProfile"};
	MediaProfilesManager manager(path);

	BOOST_TEST(true == manager.GetProfileByName(profileName).get<bool>("fixed"));
}

BOOST_AUTO_TEST_CASE(MediaProfilesManager_GetProfileByName_test1)
{
	using namespace utility::media;

	const std::string path{"../../unit_tests/test_data/mediaprofiles_manager_test.config"};

	const std::string profileName{"NotExistingProfileWithSuchName"};
	MediaProfilesManager manager(path);

	try
	{
		manager.GetProfileByName(profileName);
	}
	catch (const osrv::no_such_profile&)
	{
		// it's ok - we expected exception will be thrawn
		return;
	}

	// it's not ok, it should not reach here
	BOOST_ASSERT(false);
}

BOOST_AUTO_TEST_CASE(MediaProfilesManager_Create_test0)
{
	using namespace utility::media;

	const std::string path{"../../unit_tests/test_data/mediaprofiles_manager_test.config"};

	// Before creating a new profile save profiles count
	ConfigsReaderWriter readerWriter(path);
	readerWriter.Read();
	auto profilesCountBefore = readerWriter.ConfigsTree().get_child("MediaProfiles").size();

	const std::string customProfileName{"CustomProfileName"};
	MediaProfilesManager manager(path);
	manager.Create(customProfileName);

	readerWriter.Read();
	auto profilesCountAfter = readerWriter.ConfigsTree().get_child("MediaProfiles").size();
	BOOST_TEST(profilesCountBefore + 1 == profilesCountAfter);

	const auto& customProfileTree = manager.GetProfileByName(customProfileName);
	BOOST_TEST(customProfileName == customProfileTree.get<std::string>("Name"));
	BOOST_TEST(false == customProfileTree.get<bool>("fixed"));

	readerWriter.Reset();
}

BOOST_AUTO_TEST_CASE(MediaProfilesManager_Delete_test0)
{
	using namespace utility::media;

	const std::string path{"../../unit_tests/test_data/mediaprofiles_manager_test.config"};

	// Before creating a new profile save profiles count
	ConfigsReaderWriter readerWriter(path);
	readerWriter.Read();
	auto profilesCountBefore = readerWriter.ConfigsTree().get_child("MediaProfiles").size();

	const std::string customProfileName{"CustomProfileName"};
	MediaProfilesManager manager(path);
	manager.Create(customProfileName);

	// check attributes of a new created profile
	const auto& customProfileTree = manager.GetProfileByName(customProfileName);
	BOOST_TEST(customProfileName == customProfileTree.get<std::string>("Name"));
	BOOST_TEST(false == customProfileTree.get<bool>("fixed"));

	manager.Delete(ProfileConfigsHelper(manager.GetProfileByName(customProfileName)).ProfileToken());

	readerWriter.Read();
	auto profilesCountAfter = readerWriter.ConfigsTree().get_child("MediaProfiles").size();
	BOOST_TEST(profilesCountBefore == profilesCountAfter);

	readerWriter.Reset();
}

BOOST_AUTO_TEST_CASE(MediaProfilesManager_Delete_test1)
{
	using namespace utility::media;

	const std::string path{"../../unit_tests/test_data/mediaprofiles_manager_test.config"};

	MediaProfilesManager manager(path);
	try
	{
		// it's fixed profile so we could not delete it
		manager.Delete(ProfileConfigsHelper(manager.Back()).ProfileToken());
	}
	catch (const osrv::deletion_of_fixed_profile&)
	{
		return;
	}

	BOOST_ASSERT(false);
}

BOOST_AUTO_TEST_CASE(MediaProfilesManager_Back_test0)
{
	using namespace utility::media;

	const std::string path{"../../unit_tests/test_data/mediaprofiles_manager_test.config"};

	ConfigsReaderWriter readerWriter(path);
	readerWriter.Read();

	MediaProfilesManager manager(path);
	const std::string expected_before_changes{"SecondProfile"};
	BOOST_TEST(expected_before_changes == manager.Back().get<std::string>("Name"));

	const std::string customProfileName{"CustomProfileName"};
	manager.Create(customProfileName);

	// check attributes of a new created profile
	const auto& customProfileTree = manager.Back();
	BOOST_TEST(customProfileName == customProfileTree.get<std::string>("Name"));
	BOOST_TEST(false == customProfileTree.get<bool>("fixed"));

	readerWriter.Reset();
}

BOOST_AUTO_TEST_CASE(MediaProfilesManager_AddConfiguration_test0)
{
	using namespace utility::media;

	const std::string path{"../../unit_tests/test_data/mediaprofiles_manager_test.config"};

	ConfigsReaderWriter readerWriter(path);
	readerWriter.Read();

	MediaProfilesManager manager(path);

	const std::string customProfileName{"CustomProfileName"};
	const std::string videoSourceToken =
			readerWriter.ConfigsTree()
					.get_child(osrv::CONFIGURATION_ENUMERATION[osrv::CONFIGURATION_TYPE::VIDEOSOURCE])
					.front()
					.second.get<std::string>("token");
	manager.Create(customProfileName);
	manager.AddConfiguration(utility::media::ProfileConfigsHelper(manager.Back()).ProfileToken(),
													 osrv::CONFIGURATION_ENUMERATION[osrv::VIDEOSOURCE], videoSourceToken);

	readerWriter.Read();
	auto mediaProfilesTree = readerWriter.ConfigsTree().get_child("MediaProfiles");
	auto actual =
			mediaProfilesTree.back().second.get<std::string>(osrv::CONFIGURATION_ENUMERATION[osrv::VIDEOSOURCE], "");

	BOOST_TEST(videoSourceToken == actual);

	readerWriter.Reset();
}

BOOST_AUTO_TEST_CASE(MediaProfilesManager_AddConfiguration_test1)
{
	using namespace utility::media;

	const std::string path{"../../unit_tests/test_data/mediaprofiles_manager_test.config"};

	ConfigsReaderWriter readerWriter(path);
	readerWriter.Read();

	MediaProfilesManager manager(path);

	const std::string customProfileName{"CustomProfileName"};
	const std::string testVideoSourceToken{"testVideoSourceToken"};
	manager.Create(customProfileName);

	const std::string invalidConfigurationType{"invalidVideoSourceConfigurationType"};

	try
	{
		manager.AddConfiguration(utility::media::ProfileConfigsHelper(manager.Back()).ProfileToken(),
														 invalidConfigurationType, testVideoSourceToken);
	}
	catch (const osrv::invalid_config_type&)
	{
		readerWriter.Reset();
		return;
	}

	readerWriter.Reset();
	BOOST_ASSERT(false);
}

BOOST_AUTO_TEST_CASE(MediaProfilesManager_AddConfiguration_test2)
{
	using namespace utility::media;

	const std::string path{"../../unit_tests/test_data/mediaprofiles_manager_test.config"};

	ConfigsReaderWriter readerWriter(path);
	readerWriter.Read();

	MediaProfilesManager manager(path);

	const std::string customProfileName{"CustomProfileName"};
	const std::string invalidVideoSourceToken{"invalidTestVideoSourceToken"};
	manager.Create(customProfileName);

	try
	{
		manager.AddConfiguration(utility::media::ProfileConfigsHelper(manager.Back()).ProfileToken(),
														 osrv::CONFIGURATION_ENUMERATION[osrv::CONFIGURATION_TYPE::VIDEOSOURCE],
														 invalidVideoSourceToken);
	}
	catch (const osrv::invalid_token&)
	{
		readerWriter.Reset();
		return;
	}

	readerWriter.Reset();
	BOOST_ASSERT(false);
}

BOOST_AUTO_TEST_CASE(MediaProfilesManager_RemoveConfiguration_test0)
{
	using namespace utility::media;

	const std::string path{"../../unit_tests/test_data/mediaprofiles_manager_test.config"};

	ConfigsReaderWriter readerWriter(path);
	readerWriter.Read();

	MediaProfilesManager manager(path);

	// create a new media profile, add a configuration
	const std::string customProfileName{"CustomProfileName"};
	const std::string videoSourceToken =
			readerWriter.ConfigsTree()
					.get_child(osrv::CONFIGURATION_ENUMERATION[osrv::CONFIGURATION_TYPE::VIDEOSOURCE])
					.front()
					.second.get<std::string>("token");
	manager.Create(customProfileName);
	manager.AddConfiguration(utility::media::ProfileConfigsHelper(manager.Back()).ProfileToken(),
													 osrv::CONFIGURATION_ENUMERATION[osrv::VIDEOSOURCE], videoSourceToken);

	// remove the configuration was added
	manager.RemoveConfiguration(utility::media::ProfileConfigsHelper(manager.Back()).ProfileToken(),
															osrv::CONFIGURATION_ENUMERATION[osrv::VIDEOSOURCE]);

	readerWriter.Read();
	auto mediaProfilesTree = readerWriter.ConfigsTree().get_child("MediaProfiles");

	// expected that if the configuration was removed there were no configuration for the create new profile
	try
	{
		auto t = mediaProfilesTree.back().second.get<std::string>(osrv::CONFIGURATION_ENUMERATION[osrv::VIDEOSOURCE]);
	}
	catch (const boost::property_tree::ptree_bad_path&)
	{
		readerWriter.Reset();
		return;
	}

	readerWriter.Reset();
	BOOST_ASSERT(false);
}

BOOST_AUTO_TEST_CASE(MediaProfilesManager_RemoveConfiguration_test1)
{
	using namespace utility::media;

	const std::string path{"../../unit_tests/test_data/mediaprofiles_manager_test.config"};

	ConfigsReaderWriter readerWriter(path);
	readerWriter.Read();

	MediaProfilesManager manager(path);

	// create a new media profile, add a configuration
	const std::string customProfileName{"CustomProfileName"};
	const std::string videoSourceToken =
			readerWriter.ConfigsTree()
					.get_child(osrv::CONFIGURATION_ENUMERATION[osrv::CONFIGURATION_TYPE::VIDEOSOURCE])
					.front()
					.second.get<std::string>("token");
	const std::string videoEncoderToken =
			readerWriter.ConfigsTree()
					.get_child(osrv::CONFIGURATION_ENUMERATION[osrv::CONFIGURATION_TYPE::VIDEOENCODER])
					.front()
					.second.get<std::string>("token");

	manager.Create(customProfileName);

	manager.AddConfiguration(utility::media::ProfileConfigsHelper(manager.Back()).ProfileToken(),
													 osrv::CONFIGURATION_ENUMERATION[osrv::VIDEOSOURCE], videoSourceToken);
	manager.AddConfiguration(utility::media::ProfileConfigsHelper(manager.Back()).ProfileToken(),
													 osrv::CONFIGURATION_ENUMERATION[osrv::VIDEOENCODER], videoEncoderToken);

	// remove one configuratin by token
	manager.RemoveConfiguration(utility::media::ProfileConfigsHelper(manager.Back()).ProfileToken(),
															osrv::CONFIGURATION_ENUMERATION[osrv::VIDEOENCODER], videoEncoderToken);

	readerWriter.Read();
	auto customMediaProfile = readerWriter.ConfigsTree().get_child("MediaProfiles").back();

	BOOST_TEST(customMediaProfile.second.size() == 4);
	BOOST_TEST(customMediaProfile.second.get<std::string>(osrv::CONFIGURATION_ENUMERATION[osrv::VIDEOSOURCE]) ==
						 videoSourceToken);

	readerWriter.Reset();
}

BOOST_AUTO_TEST_CASE(MediaProfilesManager_RemoveConfiguration_test2)
{
	using namespace utility::media;

	const std::string path{"../../unit_tests/test_data/mediaprofiles_manager_test.config"};

	ConfigsReaderWriter readerWriter(path);
	readerWriter.Read();

	MediaProfilesManager manager(path);

	// create a new media profile, add a configuration
	const std::string customProfileName{"CustomProfileName"};
	const std::string videoSourceToken =
			readerWriter.ConfigsTree()
					.get_child(osrv::CONFIGURATION_ENUMERATION[osrv::CONFIGURATION_TYPE::VIDEOSOURCE])
					.front()
					.second.get<std::string>("token");
	manager.Create(customProfileName);

	// remove non-existing configuration. expected it just should be ignored, no exception and etc
	manager.RemoveConfiguration(utility::media::ProfileConfigsHelper(manager.Back()).ProfileToken(),
															osrv::CONFIGURATION_ENUMERATION[osrv::VIDEOSOURCE]);

	// just check configuration file still is readable and could be parsed
	readerWriter.Read();
	auto mediaProfilesTree = readerWriter.ConfigsTree().get_child("MediaProfiles");

	readerWriter.Reset();
}

BOOST_AUTO_TEST_CASE(MediaProfilesManager_RemoveConfiguration_test3)
{
	using namespace utility::media;

	const std::string path{"../../unit_tests/test_data/mediaprofiles_manager_test.config"};

	ConfigsReaderWriter readerWriter(path);
	readerWriter.Read();

	MediaProfilesManager manager(path);

	// create a new media profile, add a configuration
	const std::string customProfileName{"CustomProfileName"};
	const std::string videoSourceToken =
			readerWriter.ConfigsTree()
					.get_child(osrv::CONFIGURATION_ENUMERATION[osrv::CONFIGURATION_TYPE::VIDEOSOURCE])
					.front()
					.second.get<std::string>("token");
	const std::string videoEncoderToken =
			readerWriter.ConfigsTree()
					.get_child(osrv::CONFIGURATION_ENUMERATION[osrv::CONFIGURATION_TYPE::VIDEOENCODER])
					.front()
					.second.get<std::string>("token");
	const std::string audioSourceToken =
			readerWriter.ConfigsTree()
					.get_child(osrv::CONFIGURATION_ENUMERATION[osrv::CONFIGURATION_TYPE::AUDIOSOURCE])
					.front()
					.second.get<std::string>("token");
	const std::string audioEncoderToken =
			readerWriter.ConfigsTree()
					.get_child(osrv::CONFIGURATION_ENUMERATION[osrv::CONFIGURATION_TYPE::AUDIOENCODER])
					.front()
					.second.get<std::string>("token");

	manager.Create(customProfileName);
	manager.AddConfiguration(utility::media::ProfileConfigsHelper(manager.Back()).ProfileToken(),
													 osrv::CONFIGURATION_ENUMERATION[osrv::VIDEOSOURCE], videoSourceToken);
	manager.AddConfiguration(utility::media::ProfileConfigsHelper(manager.Back()).ProfileToken(),
													 osrv::CONFIGURATION_ENUMERATION[osrv::VIDEOENCODER], videoEncoderToken);
	manager.AddConfiguration(utility::media::ProfileConfigsHelper(manager.Back()).ProfileToken(),
													 osrv::CONFIGURATION_ENUMERATION[osrv::AUDIOSOURCE], audioSourceToken);
	manager.AddConfiguration(utility::media::ProfileConfigsHelper(manager.Back()).ProfileToken(),
													 osrv::CONFIGURATION_ENUMERATION[osrv::AUDIOENCODER], audioEncoderToken);

	// remove all configurations was added
	manager.RemoveConfiguration(utility::media::ProfileConfigsHelper(manager.Back()).ProfileToken(),
															osrv::CONFIGURATION_ENUMERATION[osrv::ALL]);

	readerWriter.Read();
	auto mediaProfilesTree = readerWriter.ConfigsTree().get_child("MediaProfiles");

	// expected that if the configuration was removed there were no configuration for the create new profile
	try
	{
		std::string defaultValueOnEmptyItem = "defaultValueOnEmptyItem";
		BOOST_CHECK_EQUAL(mediaProfilesTree.back().second.get<std::string>(
													osrv::CONFIGURATION_ENUMERATION[osrv::VIDEOSOURCE], defaultValueOnEmptyItem),
											defaultValueOnEmptyItem);
		BOOST_CHECK_EQUAL(mediaProfilesTree.back().second.get<std::string>(
													osrv::CONFIGURATION_ENUMERATION[osrv::VIDEOENCODER], defaultValueOnEmptyItem),
											defaultValueOnEmptyItem);
		BOOST_CHECK_EQUAL(mediaProfilesTree.back().second.get<std::string>(
													osrv::CONFIGURATION_ENUMERATION[osrv::AUDIOENCODER], defaultValueOnEmptyItem),
											defaultValueOnEmptyItem);
		BOOST_CHECK_EQUAL(mediaProfilesTree.back().second.get<std::string>(
													osrv::CONFIGURATION_ENUMERATION[osrv::AUDIOSOURCE], defaultValueOnEmptyItem),
											defaultValueOnEmptyItem);
	}
	catch (const boost::property_tree::ptree_bad_path&)
	{
		readerWriter.Reset();
		return;
	}

	readerWriter.Reset();
}
