#include "AnalyticsReader.h"

#include "MediaProfilesManager.h"

#include <../utility/XmlParser.h>

#include <boost/property_tree/ptree.hpp>

#include <iostream>
namespace utility
{

AnalyticsConfigurationReaderByToken::AnalyticsConfigurationReaderByToken(std::string_view configToken,
																																				 pt::ptree& profilesConfigs)
		: m_configToken(configToken), m_profilesCfgs(profilesConfigs)
{
}

pt::ptree& AnalyticsConfigurationReaderByToken::Config()
{
	auto& analyticsNode = m_profilesCfgs.get_child("Analytics");
	auto it = std::ranges::find_if(
			analyticsNode, [this](const auto& pt) { return pt.second.get<std::string>("token") == m_configToken; });
	if (it == analyticsNode.end())
		throw osrv::no_config{};

	return it->second;
}

const pt::ptree& AnalyticsConfigurationReaderByToken::Config() const
{
	return Config();
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

void AnalyticsModuleCreator::create(std::string_view moduleName, std::string_view moduleType,
																		const std::vector<std::pair<std::string_view, int>>& params)
{
	auto& analyticsConfigNode = utility::AnalyticsConfigurationReaderByToken(m_configToken, m_profileCfgs).Config();
	const auto& analyticsModuleIt = utility::AnalyticsModulesReaderByName(moduleType, m_analyticsCfgs).Module();

	try
	{
		utility::AnalyticsModulesReaderByName(moduleName, m_analyticsCfgs).Module();
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

	if (!std::ranges::any_of(analyticsConfigNode.get_child("compatibleModuleAnalytics"),
													 [mt = exns::get_element_without_ns(moduleType)](const auto& t) {
														 return exns::get_element_without_ns(t.second.get_value<std::string>()) == mt;
													 }))
	{
		throw osrv::configuration_conflict{};
	}

	const auto& moduleConfig = utility::AnalyticsModulesReaderByName(moduleType, m_analyticsCfgs).Module();

	pt::ptree newModule;
	newModule.add("Name", moduleName);
	newModule.add("Type", moduleConfig.get<std::string>("Name"));

	utility::AnalyticsModuleSettingsApplier applier{newModule, moduleType, m_analyticsCfgs};
	for (auto&& [name, value] : params)
	{
		applier.Apply(name, value);
	}

	auto& analytics = analyticsConfigNode.get_child("analyticsModules");
	analytics.push_back(std::make_pair(std::string{}, newModule));
}

AnalyticsModuleDeleter::AnalyticsModuleDeleter(pt::ptree& profileConfigs) : m_profileCfgs(profileConfigs)
{
}

void AnalyticsModuleDeleter::doDelete(std::string_view configToken, std::string_view moduleName)
{
	auto& analytConfig = utility::AnalyticsConfigurationReaderByToken(configToken, m_profileCfgs).Config();
	auto& modules = analytConfig.get_child("analyticsModules");
	auto it = std::ranges::find_if(
			modules, [&moduleName](const auto& pt) { return pt.second.get<std::string>("Name") == moduleName; });

	if (it == modules.end())
		throw osrv::invalid_module{};

	// todo: check if a module is fixed or not

	modules.erase(it);
}

AnalyticsModuleRelatedAnalytConfig::AnalyticsModuleRelatedAnalytConfig(std::string_view analytConfig,
																																			 std::string_view moduleName, pt::ptree& configs)
		: m_configToken(analytConfig), m_name(moduleName), m_cfgs(configs)
{
}

const pt::ptree& AnalyticsModuleRelatedAnalytConfig::Module() const
{
	const auto& analytConfig = AnalyticsConfigurationReaderByToken(m_configToken, m_cfgs).Config();
	const auto& modules = analytConfig.get_child("analyticsModules");

	const auto it =
			std::ranges::find_if(modules, [this](const auto& pt) { return pt.second.get<std::string>("Name") == m_name; });
	if (it == modules.end())
		throw osrv::invalid_module{};

	return it->second;
}

AnalyticsModuleSettingsApplier::AnalyticsModuleSettingsApplier(pt::ptree& moduleToApply, std::string_view type,
																															 const pt::ptree& analytConfigs)
		: m_moduleToApply(moduleToApply), m_type(type), m_analytConfigs(analytConfigs)
{
	InitDefaults();
}

void AnalyticsModuleSettingsApplier::InitDefaults()
{
	const auto& moduleInfo = AnalyticsModulesReaderByName(m_type, m_analytConfigs).Module();
	const auto& options = moduleInfo.get_child("Options");
	for (const auto& [_, node] : options)
	{
		m_moduleToApply.add("Parameters." + node.get<std::string>("Name"), node.get<std::string>("Default"));
	}
}

void AnalyticsModuleSettingsApplier::Apply(std::string_view paramName, int value)
{
	auto& params = m_moduleToApply.get_child("Parameters");

	auto it = std::ranges::find_if(params, [&paramName](const auto& t) { return t.first == paramName; });
	if (it == params.end())
		throw osrv::configuration_conflict{};

	const auto& moduleInfo = AnalyticsModulesReaderByName(m_type, m_analytConfigs).Module();
	const auto& options = moduleInfo.get_child("Options");

	const auto optIt = std::ranges::find_if(
			options, [&paramName](const auto& pt) { return pt.second.get<std::string>("Name") == paramName; });

	if (value < optIt->second.get<int>("Min") || value > optIt->second.get<int>("Max"))
		throw osrv::configuration_conflict{};

	it->second.put_value(value);
}

} // namespace utility