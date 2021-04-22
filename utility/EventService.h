#pragma once

#include "boost/property_tree/ptree_fwd.hpp"

#include <string>
#include <vector>

namespace 
{
	using StringPairsList_t = std::vector<std::pair<std::string, std::string>>;
}

namespace osrv::event
{

	class EventPropertiesSerializer
	{
	public:
		// topic example: "tns1:Device/Trigger/DigitalInput"
		EventPropertiesSerializer(const std::string& topic,
			const StringPairsList_t& source_properties, // pairs of name and type
			const StringPairsList_t& data_properties); // pairs of name and type

		boost::property_tree::ptree Ptree() const;
		std::string Name() const;
		std::string Path() const;

	private:
		const std::string topic_;
		const StringPairsList_t& source_properties_;
		const StringPairsList_t& data_properties_;
	};
}