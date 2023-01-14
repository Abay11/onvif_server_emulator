#include "AudioSourceReader.h"

#include "MediaProfilesManager.h"

#include <boost/property_tree/ptree.hpp>

utility::AudioEncoderReaderByToken::AudioEncoderReaderByToken(const std::string& token, const pt::ptree& cfgs)
	: token_(token), cfgs_(cfgs) {}

pt::ptree utility::AudioEncoderReaderByToken::AudioEncoder()
{
	for (const auto& p : cfgs_.get_child(osrv::CONFIGURATION_ENUMERATION[osrv::CONFIGURATION_TYPE::AUDIOENCODER]))
		if (p.second.get<std::string>("token") == token_)
			return p.second;

	throw std::runtime_error("Not found audio encoder configuration with token: " + token_);
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
			return p.second.get<std::string>("AudioEncoder");
	}

	throw std::runtime_error("Not found profile token: " + profileToken_);
}

pt::ptree utility::AudioEncoderReaderByProfileToken::AudioEncoder()
{
	return AudioEncoderReaderByToken(RelatedAudioEncoderToken(), cfgs_).AudioEncoder();
}

utility::AudioSourceConfigsReader::AudioSourceConfigsReader(const std::string& token, const pt::ptree& cfgs)
	: token_(token), cfgs_(cfgs)
{
}

pt::ptree utility::AudioSourceConfigsReader::AudioSource()
{
	for (const auto& p : cfgs_.get_child(osrv::CONFIGURATION_ENUMERATION[osrv::CONFIGURATION_TYPE::AUDIOSOURCE]))
		if (p.second.get<std::string>("token") == token_)
			return p.second;

	throw std::runtime_error("Not found audio source configuration with token: " + token_);
}

unsigned int utility::PcmuSetup::PayloadNum()
{
	return 0;
}

std::string utility::PcmuSetup::PayloadPluginName()
{
	return "rtppcmupay";
}

std::string utility::PcmuSetup::EncoderPluginName()
{
	return "mulawenc";
}

unsigned int utility::PcmaSetup::PayloadNum()
{
	return 8u;
}

std::string utility::PcmaSetup::PayloadPluginName()
{
	return "rtppcmapay";
}

std::string utility::PcmaSetup::EncoderPluginName()
{
	return "alawenc";
}

std::string utility::G726Setup::PayloadPluginName()
{
	return "rtpg726pay";
}

std::string utility::G726Setup::EncoderPluginName()
{
	return "avenc_g726";
}

std::string utility::AacSetup::PayloadPluginName()
{
	return "rtpmp4apay";
}

std::string utility::AacSetup::EncoderPluginName()
{
	return "avenc_aac";
}
