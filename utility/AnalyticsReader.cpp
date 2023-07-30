#include "AnalyticsReader.h"

#include "MediaProfilesManager.h"

#include <boost/property_tree/ptree.hpp>

namespace utility
{

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
	auto it =
			std::ranges::find_if(modulesCfg, [this](const auto& it) { return it.second.get<std::string>("Name") == m_name; });

	if (it == modulesCfg.end())
		throw osrv::invalid_module{};

	return it->second;
}
} // namespace utility