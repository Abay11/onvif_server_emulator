#include "AudioSourceReader.h"

#include <boost/property_tree/ptree.hpp>

utility::AudioSourceConfigsReader::AudioSourceConfigsReader(const pt::ptree& cfgs)
	: cfgs_(cfgs)
{
}

utility::AudioEncoderReaderByToken::AudioEncoderReaderByToken(const std::string& token, const pt::ptree& cfgs)
	: token_(token), cfgs_(cfgs) {}

pt::ptree utility::AudioEncoderReaderByToken::AudioEncoder()
{
	for (const auto& p : cfgs_.get_child("AudioEncoderConfigurations"))
		if (p.second.get<std::string>("token") == token_)
			return p.second;

	throw std::runtime_error("Not audio encoder configuration with token: " + token_);
}
