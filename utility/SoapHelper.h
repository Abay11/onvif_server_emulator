#pragma once

#include "../HttpServerFwd.h"

#include <boost\property_tree\ptree.hpp>

#include <functional>
#include <string>
#include <map>

namespace utility
{
	namespace soap
	{
		
		boost::property_tree::ptree getEnvelopeTree(const std::map<std::string, std::string>& xmlns);

		using ElementsProcessor = std::function<void(std::string&, std::string&)>;

		inline void DefaultProcessor(const std::string& element, const std::string& value)
		{
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