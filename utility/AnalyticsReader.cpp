#include "AnalyticsReader.h"

#include "MediaProfilesManager.h"

#include <../utility/XmlParser.h>

#include <boost/property_tree/ptree.hpp>

#include <iostream>
namespace utility
{

AnalyticsConfigurationReaderByToken::AnalyticsConfigurationReaderByToken(std::string_view configToken,
																																				 const pt::ptree& profilesConfigs)
		: m_configToken(configToken), m_profilesCfgs(profilesConfigs)
{
}

const pt::ptree& AnalyticsConfigurationReaderByToken::Config() const
{
	const auto& analyticsNode = m_profilesCfgs.get_child("Analytics");
	auto it = std::ranges::find_if(
			analyticsNode, [this](const auto& pt) { return pt.second.get<std::string>("token") == m_configToken; });
	if (it == analyticsNode.end())
		throw osrv::no_config{};

	return it->second;
}

AnalyticsModulesReaderByConfigToken::AnalyticsModulesReaderByConfigToken(std::string_view configToken,
																																				 const pt::ptree& configs)
		: m_configToken(configToken), m_cfgs(configs)
{
}

const pt::ptree& AnalyticsModulesReaderByConfigToken::Modules() const
{
	const auto& analytics = m_cfgs.get_child("Analytics");
	const auto cfgIt = std::ranges::find_if(
			analytics, [token = m_configToken](const auto& tree) { return tree.second.get<std::string>("token") == token; });

	if (cfgIt == analytics.end())
		throw osrv::no_config{};

	return cfgIt->second.get_child("analyticsModules");
}

AnalyticsModulesReaderByName::AnalyticsModulesReaderByName(std::string_view name, const pt::ptree& configs)
		: m_name(name), m_cfgs(configs)
{
}

const pt::ptree& AnalyticsModulesReaderByName::Module() const
{
	const auto& modulesCfg = m_cfgs.get_child("SupportedAnalyticsModules").get_child("modules");
	auto it = std::ranges::find_if(modulesCfg, [this](const auto& it) {
		return exns::get_element_without_ns(it.second.get<std::string>("Name")) ==
					 exns::get_element_without_ns(m_name.data());
	});

	if (it == modulesCfg.end())
		throw osrv::invalid_module{};

	return it->second;
}

AnalyticsModuleInstanceCount::AnalyticsModuleInstanceCount(const pt::ptree& profilesConfigs, std::string_view type)
		: m_configs(profilesConfigs), m_type(type)
{
}

int AnalyticsModuleInstanceCount::count() const
{
	int countRes = 0;

	const auto& analyticsNode = m_configs.get_child("Analytics");
	std::ranges::for_each(analyticsNode, [&](const auto& n) {
		for (const auto& [name, node] : n.second.get_child("analyticsModules"))
		{
			if (exns::get_element_without_ns(node.get<std::string>("Type")) == exns::get_element_without_ns(m_type.data()))
				++countRes;
		}
	});

	return countRes;
}

AnalyticsModuleCreator::AnalyticsModuleCreator(std::string_view configToken, pt::ptree& profileConfigs,
																							 const pt::ptree& analytConfigs)
		: m_configToken(configToken), m_profileCfgs(profileConfigs), m_analyticsCfgs(analytConfigs)

{
}

void AnalyticsModuleCreator::create(std::string_view name, std::string_view moduleType,
																		std::vector<std::pair<std::string_view, int>>)
{
	const auto& analytics = m_profileCfgs.get_child("Analytics");
	const auto cfgIt = std::ranges::find_if(
			analytics, [token = m_configToken](const auto& tree) { return tree.second.get<std::string>("token") == token; });

	if (cfgIt == analytics.end())
		throw osrv::no_config{};

	const auto& analyticsModuleIt = utility::AnalyticsModulesReaderByName(moduleType, m_analyticsCfgs).Module();

	try
	{
		utility::AnalyticsModulesReaderByName(name, m_analyticsCfgs).Module();
		// with line above we check that there is no module with same name yet
		// and if an exception was not thrown we need ot throw an error
		throw osrv::name_already_existent{};
	}
	catch (const osrv::invalid_module&)
	{
	}

	if (utility::AnalyticsModuleInstanceCount(m_profileCfgs, moduleType).count() + 1 >
			analyticsModuleIt.get<int>("maxInstances"))
	{
		throw osrv::too_many_modules{};
	}

	const auto& analyticsConfigNode = utility::AnalyticsConfigurationReaderByToken(m_configToken, m_profileCfgs).Config();
	if (!std::ranges::any_of(analyticsConfigNode.get_child("compatibleModuleAnalytics"),
													 [mt = exns::get_element_without_ns(moduleType)](const auto& t) {
														 return exns::get_element_without_ns(t.second.get_value<std::string>()) == mt;
													 }))
	{
		throw osrv::configuration_conflict{};
	}
	//  m_profileCfgs.
	//  if ()
}

} // namespace utility