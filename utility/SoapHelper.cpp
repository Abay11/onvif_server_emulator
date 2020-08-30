#include "SoapHelper.h"

#include <boost\property_tree\ptree.hpp>

namespace pt = boost::property_tree;

namespace utility
{
	namespace soap
	{

		//The helper function returns a pthree object formatted by Soap rules with passed xml namespaces
		boost::property_tree::ptree utility::soap::getEnvelopeTree(const osrv::StringsMap& xmlns)
		{
			boost::property_tree::ptree envelope_tree;

			envelope_tree.put("s:Header", "");

			for (auto it : xmlns)
			{
				envelope_tree.put("<xmlattr>.xmlns:" + it.first,
					it.second);
			}

			return envelope_tree;
		}

		void jsonNodeToXml(const pt::ptree& jsonNode,
			pt::ptree& xmlNode,
			const std::string nsPrefix,
			ElementsProcessor processor)
		{
			auto ns = nsPrefix.empty() ? "" : nsPrefix + ":";

			for (auto i : jsonNode)
			{
				//it's a JSON object
				if (i.second.size())
				{
					pt::ptree node;
					jsonNodeToXml(i.second, node, nsPrefix, processor);
					xmlNode.add_child(ns + i.first, node);
				}
				//it's just a value
				else
				{
					std::string elementData = processor(i.first, i.second.get_value<std::string>());
					xmlNode.add(ns + i.first, elementData);
				}
			}
		}

	}

}
