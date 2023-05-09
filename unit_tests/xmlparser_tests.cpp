#include <boost/test/unit_test.hpp>

#include "../utility/XmlParser.h"

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>

#include <sstream>
#include <string>

namespace
{
namespace pt = boost::property_tree;
}

BOOST_AUTO_TEST_CASE(find_hierarchy_func0)
{
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

BOOST_AUTO_TEST_CASE(find_hierarchy_func3)
{
	std::string expected = "data1";
	std::string test_input_xml_text =
			R"(
			<root>
				<node0>data0</node0>
				<node1>data1</node1>
			</root>
			)";
	std::istringstream is(test_input_xml_text);
	pt::ptree test_xml;
	pt::xml_parser::read_xml(is, test_xml);
	const std::string full_hierarchy = "root.node1";
	auto actual = exns::find_hierarchy(full_hierarchy, test_xml);
	BOOST_TEST(actual == expected);
}

BOOST_AUTO_TEST_CASE(find_hierarchy_elements0)
{
	std::string test_input_xml_text =
			R"(
		<root>
			<some_node>data</some_node>
		</root>
	)";
	std::istringstream is(test_input_xml_text);
	pt::ptree test_xml;

	pt::xml_parser::read_xml(is, test_xml);

	{
		auto elements = exns::find_hierarchy_elements("root", test_xml);
		BOOST_ASSERT(1 == elements.size());
		BOOST_CHECK_EQUAL("root", elements.at(0)->first.data());
	}

	{
		auto elements = exns::find_hierarchy_elements("root.some_node", test_xml);
		BOOST_ASSERT(1, elements.size());
		BOOST_CHECK_EQUAL("some_node", elements.at(0)->first.data());
		BOOST_CHECK_EQUAL("data", elements.at(0)->second.data());
	}
}

BOOST_AUTO_TEST_CASE(find_hierarchy_elements1)
{
	std::string test_input_xml_text =
			R"(
		<root>
			<some_node>data0</some_node>
			<some_node>data1</some_node>
			<some_node>data2</some_node>
		</root>
	)";
	std::istringstream is(test_input_xml_text);
	pt::ptree test_xml;

	pt::xml_parser::read_xml(is, test_xml);
	const std::string full_hierarchy = "root.some_node";
	auto elements = exns::find_hierarchy_elements(full_hierarchy, test_xml);
	BOOST_ASSERT(3 == elements.size());
	BOOST_CHECK_EQUAL("data0", elements.at(0)->second.data());
	BOOST_CHECK_EQUAL("data1", elements.at(1)->second.data());
	BOOST_CHECK_EQUAL("data2", elements.at(2)->second.data());
}

BOOST_AUTO_TEST_CASE(find_hierarchy_elements2)
{
	std::string test_input_xml_text =
			R"(
<s:Envelope xmlns:s="http://www.w3.org/2003/05/soap-envelope">
  <s:Body xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xmlns:xsd="http://www.w3.org/2001/XMLSchema">
    <AddConfiguration xmlns="http://www.onvif.org/ver20/media/wsdl">
      <ProfileToken>ProfileToken0</ProfileToken>
      <Configuration>
        <Type>VideoSource</Type>
        <Token>VideoSrcConfigToken0</Token>
      </Configuration>
      <Configuration>
        <Type>VideoEncoder</Type>
        <Token>VideoEncoderToken0</Token>
      </Configuration>
    </AddConfiguration>Configuration.
  </s:Body>
</s:Envelope>
		)";
	std::istringstream is(test_input_xml_text);
	pt::ptree test_xml;

	pt::xml_parser::read_xml(is, test_xml);
	const std::string full_hierarchy = "Envelope.Body.AddConfiguration.Configuration";
	auto elements = exns::find_hierarchy_elements(full_hierarchy, test_xml);
	BOOST_ASSERT(2 == elements.size());
	BOOST_CHECK_EQUAL(elements.at(0)->second.get<std::string>("Type"), "VideoSource");
	BOOST_CHECK_EQUAL(elements.at(0)->second.get<std::string>("Token"), "VideoSrcConfigToken0");
	BOOST_CHECK_EQUAL(elements.at(1)->second.get<std::string>("Type"), "VideoEncoder");
	BOOST_CHECK_EQUAL(elements.at(1)->second.get<std::string>("Token"), "VideoEncoderToken0");
}

BOOST_AUTO_TEST_CASE(find_hierarchy_elements3)
{
	// what if we search for a wrong path???

	std::string test_input_xml_text =
			R"(
<s:Envelope xmlns:s="http://www.w3.org/2003/05/soap-envelope">
  <s:Body xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xmlns:xsd="http://www.w3.org/2001/XMLSchema">
    <AddConfiguration xmlns="http://www.onvif.org/ver20/media/wsdl">
      <ProfileToken>ProfileToken0</ProfileToken>
      <Configuration>
        <Type>VideoSource</Type>
        <Token>VideoSrcConfigToken0</Token>
      </Configuration>
      <Configuration>
        <Type>VideoEncoder</Type>
        <Token>VideoEncoderToken0</Token>
      </Configuration>
    </AddConfiguration>Configuration.
  </s:Body>
</s:Envelope>
		)";
	std::istringstream is(test_input_xml_text);
	pt::ptree test_xml;

	pt::xml_parser::read_xml(is, test_xml);
	const std::string full_hierarchy = "Envelope.AddConfiguration.Configuration";
	auto elements = exns::find_hierarchy_elements(full_hierarchy, test_xml);
	BOOST_CHECK_EQUAL(0, elements.size());
}
