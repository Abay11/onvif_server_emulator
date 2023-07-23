#include "../utility/AnalyticsReader.h"
#include "../utility/MediaProfilesManager.h"

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_CASE(AnalyticsModulesReaderByConfigToken_test0)
{
	using namespace utility::media;
	ConfigsReaderWriter readerWriter{"../../server_configs/media_profiles.config"};
	readerWriter.Read();

	utility::AnalyticsModulesReaderByConfigToken reader("AnalyticsConfig_0", readerWriter.ConfigsTree());

	try
	{
		const auto& modules = reader.Modules();
		BOOST_ASSERT(modules.size() == 1);
		BOOST_ASSERT(modules.begin()->second.get<std::string>("Name") == std::string{"MyMotionRegionDetector"});
	}
	catch (const std::exception& e)
	{
		readerWriter.Reset();
		throw e;
	}

	readerWriter.Reset();
}