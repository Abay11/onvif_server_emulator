#pragma once

#include <boost/property_tree/ptree.hpp>

#include <string>
#include <memory>

namespace utility::media
{
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

		void Create(const std::string& profileName);
		void Delete(const std::string& profileToken);

		boost::property_tree::ptree& GetProfileByToken(const std::string& token);
		const boost::property_tree::ptree& GetProfileByToken(const std::string& token) const;
		boost::property_tree::ptree& GetProfileByName(const std::string& token);
		const boost::property_tree::ptree& GetProfileByName(const std::string& token) const;

		void AddConfiguration();
		void RemoveConfiguration();

	private:
		std::unique_ptr<ConfigsReaderWriter> readerWriter_;
	};
}