#include <boost/test/unit_test.hpp>

#include "../utility/EventService.h"

#include <boost/property_tree/xml_parser.hpp>
#include <boost/property_tree/ptree.hpp>

using namespace osrv::event;

BOOST_AUTO_TEST_CASE(EventPropertiesSerializerTest1)
{
	StringPairsList_t source_props = {{"InputToken", "tt:ReferenceToken"}};
	StringPairsList_t data_props = {{"LogicalState", "xs:boolean"}};
	EventPropertiesSerializer serializer{ "tns1:Device/Trigger/DigitalInput", source_props, data_props };
	BOOST_TEST("DigitalInput" == serializer.Name());
	BOOST_TEST("tns1:Device.Trigger" == serializer.Path());
	BOOST_TEST("true" == serializer.Ptree().get<std::string>("DigitalInput.tt:MessageDescription.<xmlattr>.IsProperty"));
}

BOOST_AUTO_TEST_CASE(EventPropertiesSerializerTest2)
{
	StringPairsList_t source_props = {{"InputToken", "tt:ReferenceToken"}};
	StringPairsList_t data_props = {{"LogicalState", "xs:boolean"}};
	EventPropertiesSerializer serializer{ "DigitalInput", source_props, data_props };
	BOOST_TEST("DigitalInput" == serializer.Name());
	BOOST_TEST("" == serializer.Path());
}

BOOST_AUTO_TEST_CASE(EventPropertiesSerializerTest3)
{
	StringPairsList_t source_props = {{"InputToken", "tt:ReferenceToken"}};
	StringPairsList_t data_props = {{"LogicalState", "xs:boolean"}};

	EventPropertiesSerializer serializer("tns1:Device/Trigger/DigitalInput",
		source_props,
		data_props);

	auto res_tree = serializer.Ptree();

	BOOST_TEST("InputToken" ==
		res_tree.get<std::string>("DigitalInput.tt:MessageDescription.tt:Source.tt:SimpleItemDescription.<xmlattr>.Name"));
	BOOST_TEST("tt:ReferenceToken" ==
		res_tree.get<std::string>("DigitalInput.tt:MessageDescription.tt:Source.tt:SimpleItemDescription.<xmlattr>.Type"));

	BOOST_TEST("LogicalState" ==
		res_tree.get<std::string>("DigitalInput.tt:MessageDescription.tt:Data.tt:SimpleItemDescription.<xmlattr>.Name"));
	BOOST_TEST("xs:boolean" ==
		res_tree.get<std::string>("DigitalInput.tt:MessageDescription.tt:Data.tt:SimpleItemDescription.<xmlattr>.Type"));
}