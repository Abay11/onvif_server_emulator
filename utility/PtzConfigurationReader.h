#pragma once

#include <boost/property_tree/ptree_fwd.hpp>


namespace
{
namespace pt = boost::property_tree;
}

namespace utility
{
class PtzConfigsReaderByToken
{
public:
	PtzConfigsReaderByToken(const std::string& configToken, const pt::ptree& configs);
	pt::ptree Ptz() const;

private:
	const std::string& m_token;
	const pt::ptree& m_cfgs;
};
}