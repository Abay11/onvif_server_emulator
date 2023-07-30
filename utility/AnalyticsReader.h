#pragma once

#include <boost/property_tree/ptree_fwd.hpp>

namespace
{
namespace pt = boost::property_tree;
}

namespace utility
{

class AnalyticsConfigurationReaderByToken
{
	std::string_view m_configToken;
	const pt::ptree& m_profilesCfgs;

public:
	AnalyticsConfigurationReaderByToken(std::string_view configToken, const pt::ptree& profilesConfigs);
	const pt::ptree& Config() const;
};

class AnalyticsModulesReaderByConfigToken
{
public:
	AnalyticsModulesReaderByConfigToken(std::string_view configToken, const pt::ptree& configs);
	const pt::ptree& Modules() const;

private:
	std::string_view m_configToken;
	const pt::ptree& m_cfgs;
};

class AnalyticsModulesReaderByName
{
public:
	AnalyticsModulesReaderByName(std::string_view name, const pt::ptree& configs);
	const pt::ptree& Module() const;

private:
	std::string_view m_name;
	const pt::ptree& m_cfgs;
};

class AnalyticsModuleInstanceCount
{
	const pt::ptree& m_configs;
	std::string_view m_type;

public:
	AnalyticsModuleInstanceCount(const pt::ptree& profilesConfigs, std::string_view type);
	int count() const;
};

class AnalyticsModuleCreator
{
	std::string_view m_configToken;
	pt::ptree& m_profileCfgs;
	const pt::ptree& m_analyticsCfgs;

public:
	AnalyticsModuleCreator(std::string_view configToken, pt::ptree& configs, const pt::ptree& analytConfigs);
	void create(std::string_view name, std::string_view type, std::vector<std::pair<std::string_view, int>>);
};

} // namespace utility
