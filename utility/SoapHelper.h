#pragma once

#include "../Types.inl"

#include <functional>

#include <boost\property_tree\ptree.hpp>

namespace utility
{
	namespace soap
	{
		
		boost::property_tree::ptree getEnvelopeTree(const osrv::StringsMap& xmlns);

		using ElementsProcessor = std::function<std::string(const std::string&, const std::string&)>;
			//std::string(const std::string& element, const std::string& value);

		inline std::string DefaultProcessor(const std::string& element, const std::string& value)
		{
			return value;
		}
		
		//@xmlNode a node writing to
		//@jsonNode a node to read from
		//@nsPrefix a prefix would be prepend to every element 
		//@ElementsProcessor may be used to add or modify elements values
		void jsonNodeToXml(const boost::property_tree::ptree& jsonNode,
			boost::property_tree::ptree& xmlNode,
			const std::string nsPrefix = "",
			ElementsProcessor processor = DefaultProcessor);

	}
}