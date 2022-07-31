#include "../utility/MediaProfilesManager.h"

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_CASE(ConfigsReaderWriter_test0)
{
	using namespace utility::media;

	ConfigsReaderWriter readerWriter{"../../unit_tests/test_data/mediaprofiles_manager_test.config"};
	readerWriter.Read();
	BOOST_TEST(2 == readerWriter.ConfigsTree().get_child("MediaProfiles").size());

	std::string defaultValue{ "" };
	const auto& [n0, profile0_tree] = readerWriter.ConfigsTree().get_child("MediaProfiles").front();
	BOOST_TEST("ProfileToken0", profile0_tree.get("token", defaultValue));
	
	const auto& [n1, profile1_tree] = readerWriter.ConfigsTree().get_child("MediaProfiles").back();
	BOOST_TEST("ProfileToken1", profile1_tree.get("token", defaultValue));
}

BOOST_AUTO_TEST_CASE(ConfigsReaderWriter_test1)
{
	using namespace utility::media;

	ConfigsReaderWriter readerWriter{ "../../unit_tests/test_data/mediaprofiles_manager_test.config" };
	readerWriter.Read();

	std::string defaultValue{ "" };
	std::string expectedOldValue{ "MainProfile" };
	std::string newValue{ "NewMainProfileName" };
	
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