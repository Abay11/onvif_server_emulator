#include "XmlParser.h"

#include <string>
#include <vector>

#include <boost/algorithm/string.hpp>
#include <boost\property_tree\xml_parser.hpp>

// return XML Element without NS
// Example: if passed value equal "ns:element", returned value is "element"
// Example: if passed value equal "element", returned value also is "element"
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
			// TODO: if it is possible return directly result contructred via const_assoc_iterator constructor
			return node.find(assoc_it->first);
		}
	}

	return node.not_found();
}

std::string find_hierarchy(const std::string& path, const pt::ptree& node)
{
	std::vector<std::string> tokens;
	boost::split(tokens, path, boost::is_any_of("."));

	int i = 0;
	auto assoc_it = node.begin();
	while (true)
	{
		std::string no_ns_node = get_element_without_ns(assoc_it->first);
		// we are looking for only elements in nodes, not in attributes
		if ("<xmlattr>" != no_ns_node)
		{
			if (boost::iequals(tokens[i], no_ns_node)) // do insensitive comparision
			{
				if (i == tokens.size() - 1)
				{
					return assoc_it->second.get_value<std::string>();
				}
				else if (i >= tokens.size())
				{
					return {};
				}
			}
			else
			{
				// FIX: here seems that current implementation not searching in
				// all childrens on the same level, like if we have next xml
				// and if we want value2, it returns empty value
				// <root>
				// <value>some</value>
				// <value2>some2</value>
				// </root>

				if (++assoc_it == node.end()) // Could not find match element on the same level - return not found
				{
					return {};
				}
				else
				{
					continue;
				}
			}

			if (++i >= tokens.size())
			{
				return {};
			}

			// As a node may have many childrens, need to search for
			// a children with a node name equals to our next token
			auto childrens = assoc_it;
			for (assoc_it = childrens->second.begin();
					 assoc_it != childrens->second.end() && get_element_without_ns(assoc_it->first) != tokens[i]; ++assoc_it)
			{
			}

			if (assoc_it == childrens->second.end())
				return {};
		}
		else
		{
			// move to next non-attribute value
			++assoc_it;
		}
	}

	return {};
}

std::vector<pt::ptree::const_iterator> find_hierarchy_elements(std::string_view path, const pt::ptree& root)
{
	std::vector<pt::ptree::const_iterator> result;

	if (root.empty())
		return result;

	std::vector<std::string> tokens;
	boost::split(tokens, path, boost::is_any_of("."));

	int lvl = 0;
	auto begin = root.begin();
	auto end = root.end();
	for (auto it = begin; it != end;)
	{
		if (lvl >= tokens.size())
		{
			break;
		}

		const auto no_ns_node = get_element_without_ns(it->first);
		if (boost::iequals(tokens[lvl], no_ns_node))
		{
			if (lvl == tokens.size() - 1)
			{
				result.push_back(it);
				++it;
			}
			else
			{
				// need move in depth
				// note: we process the first match children
				++lvl;
				end = it->second.end();
				it = it->second.begin();
			}
		}
		else
		{
			++it;
		}
	}

	return result;
}

pt::ptree to_ptree(const std::string& str)
{
	std::istringstream is(str);
	pt::ptree tree;
	pt::xml_parser::read_xml(is, tree);
	return tree;
}
} // namespace exns