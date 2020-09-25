#include "XmlParser.h"

//return XML Element without NS
//Example: if passed value equal "ns:element", returned value is "element"
//Example: if passed value equal "element", returned value also is "element"
std::string get_element_without_ns(const std::string& el)
{
	size_t start_pos = el.find(':');
	if (start_pos != std::string::npos)
		return el.substr(start_pos + 1);
		
	return el;
}

namespace exns
{
	pt::ptree::const_assoc_iterator find(const pt::ptree::key_type& key, const pt::ptree& node)
	{
		for (auto assoc_it = node.begin(); assoc_it != node.end(); ++assoc_it)
		{
			if (key == get_element_without_ns(assoc_it->first))
			{
				//TODO: if it is possible return directly result contructred via const_assoc_iterator constructor
				return node.find(assoc_it->first);
			}
		}

		return node.not_found();
	}
}