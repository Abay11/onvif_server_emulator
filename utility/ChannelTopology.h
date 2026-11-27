#pragma once

#include <boost/property_tree/ptree.hpp>

#include <map>
#include <string>
#include <string_view>

namespace utility::media
{
class ChannelTopology
{
public:
	explicit ChannelTopology(const boost::property_tree::ptree& document);
	static ChannelTopology Load(const std::string& path);

	std::string_view Owner(std::string_view catalog, std::string_view token) const;
	const boost::property_tree::ptree& Channels() const
	{
		return channels_;
	}
	const boost::property_tree::ptree& Media() const
	{
		return media_;
	}
	void SaveMedia(const std::string& path, const boost::property_tree::ptree& previous,
								 const boost::property_tree::ptree& next) const;

private:
	boost::property_tree::ptree channels_;
	boost::property_tree::ptree media_;
	std::map<std::string, std::map<std::string, std::string, std::less<>>, std::less<>> owners_;
};
} // namespace utility::media
