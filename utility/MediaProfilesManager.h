#pragma once

#include <boost/property_tree/ptree.hpp>

#include <array>
#include <memory>
#include <string>

namespace osrv
{
using namespace std::string_literals;

static const std::array CONFIGURATION_ENUMERATION{"All"s,					 "VideoSource"s, "VideoEncoder"s, "AudioSource"s,
																									"AudioEncoder"s, "AudioOutput"s, "AudioDecoder"s, "Metadata"s,
																									"Analytics"s,		 "PTZ"s,				 "Receiver"s};

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
	const char* what() const override
	{
		return "No such profile";
	}
};

class invalid_token : public std::exception
{
public:
	const char* what() const override
	{
		return "No such configuration token";
	}
};

class invalid_config_type : public std::exception
{
public:
	const char* what() const override
	{
		return "No such configuration type";
	}
};

class deletion_of_fixed_profile : public std::exception
{
public:
	const char* what() const override
	{
		return "A fixed Profile cannot be deleted";
	}
};

class no_entity : public std::exception
{
public:
	const char* what() const override
	{
		return "No such PTZ node on the device";
	}
};

class incomplete_configuration : public std::exception
{
public:
	const char* what() const override
	{
		return "The specified media profile does contain either unused sources or encoder configurations without a corresponding source.";
	}
};


} // namespace osrv

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
	void Save() const;
	// reset all changes to the initial state how it was at the first Read() using backup instance.
	// it also writes backup instance state into a file calling Save() automatically.
	void Reset();

	boost::property_tree::ptree& ConfigsTree()
	{
		return *configsTree_;
	};
	const boost::property_tree::ptree& ConfigsTree() const
	{
		return *configsTree_;
	};

private:
	const std::string filePath_;
	std::unique_ptr<boost::property_tree::ptree> configsTree_;
	std::unique_ptr<boost::property_tree::ptree> configsTreeBackup_; // used for reset operation
};


// NOTE: This implementation is not thread-safe. For now it is ok, because the application is single-thread.
class MediaProfilesManager
{
public:
	explicit MediaProfilesManager(const std::string& filePath);

	void Create(const std::string& profileName) const;
	void Delete(const std::string& profileToken) const;
	void AddConfiguration(const std::string& profileToken, const std::string& configType,
												const std::string& configToken) const;
	void RemoveConfiguration(std::string_view profileToken, std::string_view configType,
													 std::string_view configToken = {}) const;

	boost::property_tree::ptree& GetProfileByToken(const std::string& token);
	const boost::property_tree::ptree& GetProfileByToken(const std::string& token) const;

	boost::property_tree::ptree GetProfileByToken(const std::string& token,
																								const std::vector<std::string>& configs) const;

	boost::property_tree::ptree& GetProfileByName(const std::string& name);

	const boost::property_tree::ptree& GetProfileByName(const std::string& name) const
	{
		return GetProfileByName(name);
	};

	// do not call Back on empty profiles list
	const boost::property_tree::ptree& Back() const;

	const ConfigsReaderWriter* ReaderWriter() const
	{
		return readerWriter_.get();
	}

	size_t GetUseCount(std::string_view /*token*/, std::string_view /*configType*/) const;

private:
	std::string newProfileToken(size_t n) const;
	boost::property_tree::ptree::iterator getProfileNode(std::string_view profileToken) const;

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
} // namespace utility::media