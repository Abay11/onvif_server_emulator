#include "EventService.h"

#include <boost/property_tree/ptree.hpp>

#include <boost/algorithm/string.hpp>

namespace pt = boost::property_tree;

namespace osrv::event
{
	EventPropertiesSerializer::EventPropertiesSerializer(const std::string& topic,
			const StringPairsList_t& source_properties,
			const StringPairsList_t& data_properties)
		: topic_(topic)
		, source_properties_(source_properties)
		, data_properties_(data_properties)
	{
		if (topic_.empty())
			throw std::runtime_error("Topic is empty");

		if (source_properties_.empty() || data_properties_.empty())
			throw std::runtime_error("Properties are empty");
	}

	std::string EventPropertiesSerializer::Name() const
	{
		auto last_delim_pos = topic_.find_last_of('/');
		if (last_delim_pos == std::string_view::npos)
		{
			last_delim_pos = 0;
		}
		else
		{
			++last_delim_pos;
		}

		return topic_.substr(last_delim_pos).data();
	}

	std::string EventPropertiesSerializer::Path() const
	{
		auto last_delim_pos = topic_.find_last_of('/');
		if (last_delim_pos == std::string_view::npos)
		{
			return {};
		}

		std::string path = topic_.substr(0, last_delim_pos).data();
		std::replace(path.begin(), path.end(), '/', '.');
		return path;
	}

	boost::property_tree::ptree EventPropertiesSerializer::Ptree() const
	{
		pt::ptree p;

		if (topic_.empty())
			return p;

		auto node_name = Name();

		p.add(node_name + ".<xmlattr>.wstop:topic", "true");

		if (source_properties_.size() < 1)
			throw std::runtime_error("source properties is empty");

		for (auto [prop_name, prop_type] : source_properties_)
		{
			p.add(node_name + ".tt:MessageDescription.tt:Source.tt:SimpleItemDescription.<xmlattr>.Name",
				prop_name);
			p.add(node_name + ".tt:MessageDescription.tt:Source.tt:SimpleItemDescription.<xmlattr>.Type",
				prop_type);
		}
		
		for (auto [prop_name, prop_type] : data_properties_)
		{
			p.add(node_name + ".tt:MessageDescription.tt:Data.tt:SimpleItemDescription.<xmlattr>.Name",
				prop_name);
			p.add(node_name + ".tt:MessageDescription.tt:Data.tt:SimpleItemDescription.<xmlattr>.Type",
				prop_type);
		}


		return p;
	}
}
