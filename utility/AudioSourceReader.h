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

	/* @ntrack specify number of a track in a row */
	class IAudioSetup
	{
	public:
		IAudioSetup(unsigned int bitrate, unsigned char ntrack) : bitrate_(bitrate), ntrack_(ntrack) {}

		std::string	Encoding()
		{
			return EncoderPluginName() + " bitrate=" + std::to_string(bitrate_);
		}

		virtual unsigned int PayloadNum()
		{
			return ntrack_ + 95u;
		}

		virtual std::string PayloadPluginName() = 0;

	protected:
		virtual std::string EncoderPluginName() = 0;

	private:
		const unsigned int bitrate_;
		const unsigned int ntrack_;
	};

	class PcmuSetup : public IAudioSetup
	{
	public:
		PcmuSetup() : IAudioSetup(64000u, 0u) {}
		// Inherited via IAudioSetup
		virtual unsigned int PayloadNum() override;
		virtual std::string PayloadPluginName() override;
		virtual std::string EncoderPluginName() override;
	};

	class PcmaSetup : public IAudioSetup
	{
	public:
		PcmaSetup() : IAudioSetup(64000u, 0u) {}
		// Inherited via IAudioSetup
		virtual unsigned int PayloadNum() override;
		virtual std::string PayloadPluginName() override;
		virtual std::string EncoderPluginName() override;
	};

	class G726Setup : public IAudioSetup
	{
	public:
		G726Setup(unsigned int bitrate = 16000u, unsigned int ntrack=96u) : IAudioSetup(bitrate, ntrack) {}
		// Inherited via IAudioSetup
		virtual std::string PayloadPluginName() override;
		virtual std::string EncoderPluginName() override;
	};

	class AacSetup : public IAudioSetup
	{
	public:
		AacSetup(unsigned int bitrate = 16000u, unsigned int ntrack = 96u) : IAudioSetup(bitrate, ntrack) {}
		// Inherited via IAudioSetup
		virtual std::string PayloadPluginName() override;
		virtual std::string EncoderPluginName() override;
	};

	class AudioSetupFactory
	{
	public:
		template <class ... Types>
		std::shared_ptr<IAudioSetup> AudioSetup(const std::string& name, Types...args)
		{
			if (name == "PCMU")
				return std::make_shared<PcmuSetup>();

			if (name == "PCMA")
				return std::make_shared<PcmaSetup>();
			
			if (name == "G726")
				return std::make_shared<G726Setup>(std::forward<Types>(args)...);
			
			if (name == "AAC")
				return std::make_shared<AacSetup>(std::forward<Types>(args)...);

			throw std::runtime_error("No audio setup class for " + name);
		}
	};
}