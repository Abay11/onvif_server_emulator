#include "PtzConfigurationReader.h"

#include "MediaProfilesManager.h"

#include <boost/property_tree/ptree.hpp>

namespace utility
{

PtzConfigsReaderByToken::PtzConfigsReaderByToken(const std::string& configToken, const pt::ptree& configs)
		: m_token(configToken), m_cfgs(configs)
{
}

pt::ptree PtzConfigsReaderByToken::Ptz() const
{
	for (const auto& [key, node] : m_cfgs.get_child(osrv::CONFIGURATION_ENUMERATION[osrv::CONFIGURATION_TYPE::PTZ], {}))
		if (node.get<std::string>("token") == m_token)
			return node;

	throw osrv::no_config();
}

} // namespace utility