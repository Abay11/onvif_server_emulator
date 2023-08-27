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
	pt::ptree& m_profilesCfgs;

public:
	AnalyticsConfigurationReaderByToken(std::string_view configToken, pt::ptree& profilesConfigs);
	pt::ptree& Config();
	const pt::ptree& Config() const;
};

class AnalyticsModulesReaderByConfigToken
{
public:
	AnalyticsModulesReaderByConfigToken(std::string_view configToken, const pt::ptree& profilesConfigs);
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

class AnalyticsModuleRelatedAnalytConfig
{
	std::string_view m_configToken;
	std::string_view m_name;
	pt::ptree& m_cfgs;

public:
	AnalyticsModuleRelatedAnalytConfig(std::string_view analytConfig, std::string_view moduleName, pt::ptree& configs);
	const pt::ptree& Module() const;
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
	void create(std::string_view name, std::string_view type,
							const std::vector<std::pair<std::string_view, int>>& params); // TODO: allow to pass diff param types
};

class AnalyticsModuleDeleter
{
	pt::ptree& m_profileCfgs;

public:
	AnalyticsModuleDeleter(pt::ptree& profileConfigs);
	void doDelete(std::string_view configToken, std::string_view moduleName);
};

class AnalyticsModuleSettingsApplier
{
	pt::ptree& m_moduleToApply;
	std::string_view m_type;
	const pt::ptree& m_analytConfigs;

	void InitDefaults();

public:
	AnalyticsModuleSettingsApplier(pt::ptree& moduleToApply, std::string_view type, const pt::ptree& analytConfigs);
	void Apply(std::string_view paramName, int value);
};

class RuleReaderByName
{
public:
	RuleReaderByName(std::string_view name, const pt::ptree& configs);
	const pt::ptree& Rule() const;

private:
	std::string_view m_name;
	const pt::ptree& m_cfgs;
};
} // namespace utility
