#pragma once

#include <boost/property_tree/ptree.hpp>

#include <string>
#include <memory>
#include <array>

namespace osrv
{
	using namespace std::string_literals;

	static const std::array CONFIGURATION_ENUMERATION
	{
		"All"s,
		"VideoSource"s,
		"VideoEncoder"s,
		"AudioSource"s,
		"AudioEncoder"s,
		"AudioOutput"s,
		"AudioDecoder"s,
		"Metadata"s,
		"Analytics"s,
		"PTZ"s,
		"Receiver"s
	};

	enum CONFIGURATION_TYPE : int
	{
		ALL = 0,
		VIDEOSOURCE,
		VIDEOENCODER,
		AUDIOSOURCE,
		AUDIOENCODER,
		AUDIOOUTPUT,
		AUDIODECODER,
		METADATA,
		ANALYTICS,
		PTZ,
		RECEIVER
	};

	class no_such_profile : public std::exception
	{
	public:
		const char* what() const override { return "No such profile"; }
	};
}

namespace utility::media
{
	// NOTE, before any operation you should load configuration with Read()
	// Otherwise correct work not guaranteed
	class ConfigsReaderWriter
	{
	public:
		explicit ConfigsReaderWriter(const std::string& filePath);

		// read configs from JSON file 
		void Read();
		// save current configs into a file
		void Save();
		// reset all changes to the initial state how it was at the first Read() using backup instance.
		// it also writes backup instance state into a file calling Save() automatically.
		void Reset();

		boost::property_tree::ptree& ConfigsTree() { return *configsTree_; };
		const boost::property_tree::ptree& ConfigsTree() const { return ConfigsTree(); };

	private:
		const std::string filePath_;
		std::unique_ptr<boost::property_tree::ptree> configsTree_;
		std::unique_ptr<boost::property_tree::ptree> configsTreeBackup_; // used for reset operation
	};

	class MediaProfilesManager
	{
	public:
		explicit MediaProfilesManager(const std::string& filePath);

		void Create(const std::string& profileName) const;
		void Delete(const std::string& profileToken) const;

		boost::property_tree::ptree& GetProfileByToken(const std::string& token);
		const boost::property_tree::ptree& GetProfileByToken(const std::string& token) const { return GetProfileByToken(token); };
		boost::property_tree::ptree& GetProfileByName(const std::string& name);
		const boost::property_tree::ptree& GetProfileByName(const std::string& name) const { return GetProfileByName(name); };
		// do not call Back on empty profiles list
		const boost::property_tree::ptree& Back() const;

		void AddConfiguration();
		void RemoveConfiguration();

	private:
		std::string newProfileToken(size_t n) const;

	private:
		std::unique_ptr<ConfigsReaderWriter> readerWriter_;
	};

	class ProfileConfigsHelper
	{
	public:
		explicit ProfileConfigsHelper(const boost::property_tree::ptree& profileTree);
		std::string ProfileToken() const;

	private:
		const boost::property_tree::ptree& profileTree_;
	};
}