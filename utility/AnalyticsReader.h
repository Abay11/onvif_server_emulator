#pragma once

#include <boost/property_tree/ptree_fwd.hpp>

namespace
{
namespace pt = boost::property_tree;
}

namespace utility
{

class AnalyticsModulesReaderByConfigToken
{
public:
	AnalyticsModulesReaderByConfigToken(std::string_view configToken, const pt::ptree& configs);
	const pt::ptree& Modules() const;

private:
	std::string_view m_configToken;
	const pt::ptree& m_cfgs;
};
} // namespace utility
