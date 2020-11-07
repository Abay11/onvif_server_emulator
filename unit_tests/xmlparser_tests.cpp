#include <boost/test/unit_test.hpp>

#include "../utility/XmlParser.h"

#include <string>

#include <boost/property_tree/ptree.hpp>

BOOST_AUTO_TEST_CASE(find_hierarchy_func0)
{
	namespace pt = boost::property_tree;
	pt::ptree test_xml;
	std::string expected = "data";
	const std::string full_hierarchy = "root.root2.element";
	test_xml.put(full_hierarchy, expected);

	// little check that XML tree is created correctly
	auto it = test_xml.begin();
	BOOST_TEST(it->first == "root");
	it = it->second.begin();
	BOOST_TEST(it->first == "root2");
	it = it->second.begin();
	BOOST_TEST(it->first == "element");
	BOOST_TEST(it->second.get_value<std::string>() == expected);

	auto actual = exns::find_hierarchy(full_hierarchy, test_xml);

	BOOST_TEST(expected == actual);

	// Test for a case when an element does not exist
	auto actual1 = exns::find_hierarchy("root.root1.element", test_xml);
	auto expected1 = "";
	BOOST_TEST(expected1 == actual1);
}

BOOST_AUTO_TEST_CASE(find_hierarchy_func1)
{
	namespace pt = boost::property_tree;
	pt::ptree test_xml;
	std::string expected = "data";
	const std::string full_hierarchy = "root.root2.element";
	test_xml.put(full_hierarchy, expected);
	
	// XML tree with attributes
	test_xml.add("root.<xmlattr>.attr1", "attr_data1");
	test_xml.add("root.<xmlattr>.attr2", "attr_data2");

	// little check that XML tree is created correctly
	auto it = test_xml.begin();
	BOOST_TEST(it->first == "root");
	BOOST_TEST(it->second.get<std::string>("<xmlattr>.attr1") == "attr_data1");
	BOOST_TEST(it->second.get<std::string>("<xmlattr>.attr2") == "attr_data2");

	it = it->second.begin();
	BOOST_TEST(it->first == "root2");
	it = it->second.begin();
	BOOST_TEST(it->first == "element");
	BOOST_TEST(it->second.get_value<std::string>() == expected);

	auto actual = exns::find_hierarchy(full_hierarchy, test_xml);

	BOOST_TEST(expected == actual);
}

BOOST_AUTO_TEST_CASE(find_hierarchy_func2)
{
	std::string expected = "data";
	
	namespace pt = boost::property_tree;
	pt::ptree test_xml;
	// now a XML tree with a several childs
	test_xml.add("root.root1.other_element", "other_data");
	const std::string full_hierarchy = "root.root2.element";
	test_xml.add(full_hierarchy, expected);

	// little check that XML tree is created correctly
	auto it = test_xml.begin();
	BOOST_TEST(it->first == "root");

	it = it->second.begin();
	BOOST_TEST(it->first == "root1");
	it = it->second.begin();
	BOOST_TEST(it->first == "other_element");
	BOOST_TEST(it->second.get_value<std::string>() == "other_data");


	auto root2 = test_xml.get_child("root.root2");
	it = root2.begin();
	BOOST_TEST(it->first == "element");
	BOOST_TEST(it->second.get_value<std::string>() == expected);

	auto actual = exns::find_hierarchy(full_hierarchy, test_xml);

	BOOST_TEST(expected == actual);
}
