#pragma once

#include <boost/property_tree/ptree_fwd.hpp>

namespace
{
namespace pt = boost::property_tree;
}

namespace utility
{
class VideoSourceConfigsReaderByToken
{
public:
	VideoSourceConfigsReaderByToken(const std::string& configToken, const pt::ptree& configs);
	pt::ptree VideoSource();

private:
	const std::string& token_;
	const pt::ptree& cfgs_;
};

class VideoEncoderReaderByToken
{
public:
	VideoEncoderReaderByToken(const std::string& configToken, const pt::ptree& configs);

	pt::ptree VideoEncoder();

private:
	const std::string& token_;
	const pt::ptree& cfgs_;
};

} // namespace utility