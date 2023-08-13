#include "../utility/AnalyticsReader.h"
#include "../utility/MediaProfilesManager.h"

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_CASE(AnalyticsModulesReaderByConfigToken_test0)
{
	using namespace utility::media;
	ConfigsReaderWriter readerWriter{"../../server_configs/media_profiles.config"};
	readerWriter.Read();

	utility::AnalyticsModulesReaderByConfigToken reader("AnalyticsConfig_0", readerWriter.ConfigsTree());

	const auto& modules = reader.Modules();
	BOOST_ASSERT(modules.size() == 1);
	BOOST_ASSERT(modules.begin()->second.get<std::string>("Name") == std::string{"MyMotionRegionDetector"});
}

BOOST_AUTO_TEST_CASE(AnalyticsModulesReaderByName0)
{
	using namespace utility::media;
	ConfigsReaderWriter readerWriter{"../../server_configs/analytics.config"};
	readerWriter.Read();

	auto name = "tt:MotionRegionDetector";
	const auto& m = utility::AnalyticsModulesReaderByName(name, readerWriter.ConfigsTree()).Module();
	BOOST_ASSERT(m.get<std::string>("Name") == name);

	readerWriter.Reset();
}

BOOST_AUTO_TEST_CASE(AnalyticsModulesReaderByName1)
{
	using namespace utility::media;
	ConfigsReaderWriter readerWriter{"../../server_configs/analytics.config"};
	readerWriter.Read();

	BOOST_CHECK_THROW(utility::AnalyticsModulesReaderByName("InvalidDetectorName", readerWriter.ConfigsTree()).Module(),
										osrv::invalid_module);
}

BOOST_AUTO_TEST_CASE(AnalyticsModuleInstanceCountTest0)
{
	using namespace utility::media;
	ConfigsReaderWriter readerWriter{"../../server_configs/media_profiles.config"};
	readerWriter.Read();

	BOOST_CHECK_EQUAL(
			utility::AnalyticsModuleInstanceCount(readerWriter.ConfigsTree(), "tt:MotionRegionDetector").count(), 1);
}

BOOST_AUTO_TEST_CASE(AnalyticsModuleInstanceCountTest1)
{
	using namespace utility::media;
	ConfigsReaderWriter readerWriter{"../../server_configs/media_profiles.config"};
	readerWriter.Read();

	BOOST_CHECK_EQUAL(utility::AnalyticsModuleInstanceCount(readerWriter.ConfigsTree(), "InvalidDetectorType").count(),
										0);
}

BOOST_AUTO_TEST_CASE(CreateAnalyticsModuleTest0)
{
	using namespace utility::media;
	ConfigsReaderWriter readerWriter{"../../server_configs/media_profiles.config"};
	readerWriter.Read();

	ConfigsReaderWriter analyticsConfigsReader{"../../server_configs/analytics.config"};
	analyticsConfigsReader.Read();

	try
	{
		BOOST_CHECK_THROW(utility::AnalyticsModuleCreator("InvalidAnalyticsConfigToken", readerWriter.ConfigsTree(),
																											analyticsConfigsReader.ConfigsTree())
													.create("", "", {}),
											osrv::no_config);
	}
	catch (const std::exception& e)
	{
		readerWriter.Reset();
		analyticsConfigsReader.Reset();
		throw e;
	}

	readerWriter.Reset();
	analyticsConfigsReader.Reset();
}

BOOST_AUTO_TEST_CASE(CreateAnalyticsModuleTest1)
{
	using namespace utility::media;
	ConfigsReaderWriter readerWriter{"../../server_configs/media_profiles.config"};
	readerWriter.Read();

	ConfigsReaderWriter analyticsConfigsReader{"../../server_configs/analytics.config"};
	analyticsConfigsReader.Read();

	try
	{
		BOOST_CHECK_THROW(utility::AnalyticsModuleCreator("AnalyticsConfig_0", readerWriter.ConfigsTree(),
																											analyticsConfigsReader.ConfigsTree())
													.create("MyModuleName", "tt:NotExistingDetectorType", {}),
											osrv::invalid_module);
	}
	catch (const std::exception& e)
	{
		readerWriter.Reset();
		analyticsConfigsReader.Reset();
		throw e;
	}

	readerWriter.Reset();
	analyticsConfigsReader.Reset();
}

BOOST_AUTO_TEST_CASE(CreateAnalyticsModuleTest2)
{
	using namespace utility::media;
	ConfigsReaderWriter profilesReaderWriter{"../../server_configs/media_profiles.config"};
	profilesReaderWriter.Read();

	ConfigsReaderWriter analyticsConfigsReader{"../../server_configs/analytics.config"};
	analyticsConfigsReader.Read();

	const auto* analytConfig = "AnalyticsConfig_0";
	const auto* moduleName = "CreateAnalyticsModuleTest2_moduleName";
	const auto* moduleType = "tt:MotionRegionDetector";

	const auto& analyticsConfig =
			utility::AnalyticsConfigurationReaderByToken(analytConfig, profilesReaderWriter.ConfigsTree()).Config();
	const auto& analyticsModules = analyticsConfig.get_child("analyticsModules");
	const auto sizeBefore = analyticsModules.size();

	utility::AnalyticsModuleCreator(analytConfig, profilesReaderWriter.ConfigsTree(),
																	analyticsConfigsReader.ConfigsTree())
			.create(moduleName, moduleType, {});
	const auto& res =
			utility::AnalyticsModuleRelatedAnalytConfig(analytConfig, moduleName, profilesReaderWriter.ConfigsTree())
					.Module();

	BOOST_CHECK_EQUAL(analyticsModules.size(), sizeBefore + 1);
	BOOST_CHECK_EQUAL(res.get<std::string>("Name"), moduleName);
	BOOST_CHECK_EQUAL(res.get<std::string>("Type"), moduleType);

	const auto& params = res.get_child("Parameters");
	BOOST_CHECK_EQUAL(params.size(), 1);
	BOOST_CHECK_EQUAL(params.get<int>("Sensitivity"), 50);
}

BOOST_AUTO_TEST_CASE(AnalyticsModuleSettingsApplierTest0)
{
	using namespace utility::media;

	ConfigsReaderWriter analyticsConfigsReader{"../../server_configs/analytics.config"};
	analyticsConfigsReader.Read();

	pt::ptree testModule;

	utility::AnalyticsModuleSettingsApplier sa(testModule, "tt:MotionRegionDetector",
																						 analyticsConfigsReader.ConfigsTree());

	BOOST_CHECK_EQUAL(testModule.size(), 1);
	BOOST_CHECK_EQUAL(testModule.get<int>("Parameters.Sensitivity"), 50);
}

BOOST_AUTO_TEST_CASE(AnalyticsModuleSettingsApplierTest1)
{
	using namespace utility::media;

	ConfigsReaderWriter analyticsConfigsReader{"../../server_configs/analytics.config"};
	analyticsConfigsReader.Read();

	pt::ptree testModule;

	utility::AnalyticsModuleSettingsApplier sa(testModule, "tt:MotionRegionDetector",
																						 analyticsConfigsReader.ConfigsTree());
	sa.Apply("Sensitivity", 3);

	BOOST_CHECK_EQUAL(testModule.size(), 1);
	BOOST_CHECK_EQUAL(testModule.get<int>("Parameters.Sensitivity"), 3);
}

BOOST_AUTO_TEST_CASE(AnalyticsModuleSettingsApplierTest2)
{
	using namespace utility::media;

	ConfigsReaderWriter analyticsConfigsReader{"../../server_configs/analytics.config"};
	analyticsConfigsReader.Read();

	pt::ptree testModule;

	utility::AnalyticsModuleSettingsApplier sa(testModule, "tt:MotionRegionDetector",
																						 analyticsConfigsReader.ConfigsTree());
	BOOST_CHECK_THROW(sa.Apply("Sensitivity", 150), osrv::configuration_conflict);

	const int defaultValue = 50;
	BOOST_CHECK_EQUAL(testModule.get<int>("Parameters.Sensitivity"), defaultValue);
}

BOOST_AUTO_TEST_CASE(AnalyticsModuleDeleterTest0)
{
	using namespace utility::media;
	ConfigsReaderWriter profilesReaderWriter{"../../server_configs/media_profiles.config"};
	profilesReaderWriter.Read();

	ConfigsReaderWriter analyticsConfigsReader{"../../server_configs/analytics.config"};
	analyticsConfigsReader.Read();

	const auto* analytConfigToken = "AnalyticsConfig_0";
	const auto* moduleName = "CreateAnalyticsModuleTest2_moduleName";
	const auto* moduleType = "tt:MotionRegionDetector";

	const auto& analyticsConfig =
			utility::AnalyticsConfigurationReaderByToken(analytConfigToken, profilesReaderWriter.ConfigsTree()).Config();
	const auto& analyticsModules = analyticsConfig.get_child("analyticsModules");
	const auto sizeBefore = analyticsModules.size();

	utility::AnalyticsModuleCreator(analytConfigToken, profilesReaderWriter.ConfigsTree(),
																	analyticsConfigsReader.ConfigsTree())
			.create(moduleName, moduleType, {});

	BOOST_CHECK_EQUAL(analyticsModules.size(), sizeBefore + 1);

	utility::AnalyticsModuleDeleter(profilesReaderWriter.ConfigsTree()).doDelete(analytConfigToken, moduleName);

	BOOST_CHECK_EQUAL(analyticsModules.size(), sizeBefore);

	BOOST_CHECK_THROW(
			utility::AnalyticsModuleRelatedAnalytConfig(analytConfigToken, moduleName, profilesReaderWriter.ConfigsTree())
					.Module(),
			osrv::invalid_module);
}
