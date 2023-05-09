#include "SoapHelper.h"

#include <boost\property_tree\ptree.hpp>

#include <map>

namespace pt = boost::property_tree;

namespace utility
{
namespace soap
{

// The helper function returns a pthree object formatted by Soap rules with passed xml namespaces
boost::property_tree::ptree utility::soap::getEnvelopeTree(const std::map<std::string, std::string>& xmlns)
{
	boost::property_tree::ptree envelope_tree;

	envelope_tree.put("s:Header", "");

	for (auto it : xmlns)
	{
		envelope_tree.put("<xmlattr>.xmlns:" + it.first, it.second);
	}

	return envelope_tree;
}

void jsonNodeToXml(const pt::ptree& jsonNode, pt::ptree& xmlNode, const std::string nsPrefix,
									 ElementsProcessor processor)
{
	auto ns = nsPrefix.empty() ? "" : nsPrefix + ":";

	for (auto i : jsonNode)
	{
		// it's a JSON object
		if (i.second.size())
		{
			pt::ptree node;
			jsonNodeToXml(i.second, node, nsPrefix, processor);
			xmlNode.add_child(ns + i.first, node);
		}
		// it's just a value
		else
		{
			std::string element = i.first;
			std::string elementData = i.second.get_value<std::string>();

			// processor could somehow change elements or elements' data,
			// for example add some prefix, or dynamic generated xml ns
			processor(element, elementData);

			xmlNode.add(ns + element, elementData);
		}
	}
}

} // namespace soap

} // namespace utility
