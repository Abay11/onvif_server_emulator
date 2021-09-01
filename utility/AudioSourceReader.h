#pragma once

#include <boost/property_tree/ptree_fwd.hpp>

namespace
{
	namespace pt = boost::property_tree;
}

namespace utility
{
	class AudioSourceConfigsReader
	{
	public:
		AudioSourceConfigsReader(const std::string&, const pt::ptree&);

		pt::ptree AudioSource();

	private:
		const std::string& token_;
		const pt::ptree& cfgs_;
	};

	class AudioEncoderReaderByToken
	{
	public:
		AudioEncoderReaderByToken(const std::string&, const pt::ptree&);

		pt::ptree AudioEncoder();

	private:
		const std::string& token_;
		const pt::ptree& cfgs_;
	};
	
	class AudioEncoderReaderByProfileToken
	{
	public:
		AudioEncoderReaderByProfileToken(const std::string&, const pt::ptree&);

		const std::string RelatedAudioEncoderToken();

		pt::ptree AudioEncoder();

	private:
		const std::string& profileToken_;
		const pt::ptree& cfgs_;
	};
}