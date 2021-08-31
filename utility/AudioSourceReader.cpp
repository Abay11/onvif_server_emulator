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

utility::AudioEncoderReaderByProfileToken::AudioEncoderReaderByProfileToken(const std::string& token, const pt::ptree& cfgs) : profileToken_(token), cfgs_(cfgs)
{
}

const std::string utility::AudioEncoderReaderByProfileToken::RelatedAudioEncoderToken()
{
	auto profiles = cfgs_.find("MediaProfiles")->second;
	for (const auto& p : profiles)
	{
		if (p.second.get<std::string>("token") == profileToken_)
			return p.second.get<std::string>("AudioEncoderConfiguration");
	}

	throw std::runtime_error("Not found profile token: " + profileToken_);
}

pt::ptree utility::AudioEncoderReaderByProfileToken::AudioEncoder()
{
	return AudioEncoderReaderByToken(RelatedAudioEncoderToken(), cfgs_).AudioEncoder();
}
