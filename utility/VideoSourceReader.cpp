#include "VideoSourceReader.h"

#include "MediaProfilesManager.h"

#include <boost/property_tree/ptree.hpp>

namespace utility
{

VideoSourceConfigsReaderByToken::VideoSourceConfigsReaderByToken(const std::string& configToken,
																																 const pt::ptree& configs)
		: token_(configToken), cfgs_(configs)
{
}

pt::ptree utility::VideoSourceConfigsReaderByToken::VideoSource()
{
	for (const auto& p : cfgs_.get_child(osrv::CONFIGURATION_ENUMERATION[osrv::CONFIGURATION_TYPE::VIDEOSOURCE]))
		if (p.second.get<std::string>("token") == token_)
			return p.second;

	throw osrv::invalid_token();
}
} // namespace utility
