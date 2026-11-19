#include "FieldDetector.h"
#include "AnalyticsRules.h"
#include <boost/geometry.hpp>
#include <boost/geometry/geometries/point_xy.hpp>
#include <boost/geometry/geometries/polygon.hpp>
#include <cmath>
#include <set>

namespace utility::field
{
namespace
{
namespace bg = boost::geometry;
using Point = bg::model::d2::point_xy<double>;
using Polygon = bg::model::polygon<Point>;
bool hasText(const Tree& node)
{
	return node.data().find_first_not_of(" \t\r\n") != std::string::npos;
}
Polygon polygon(const Tree& points)
{
	Polygon result;
	for (const auto& [_, p] : points)
		bg::append(result.outer(), Point(p.get<double>("x"), p.get<double>("y")));
	bg::correct(result);
	return result;
}
} // namespace

Tree ReadRule(const Tree& xml, const Tree* previous)
{
	if (xml.get<std::string>("<xmlattr>.Type") != Type || xml.count("tt:Parameters") != 1 || xml.size() != 2 ||
			xml.get_child("<xmlattr>").size() != 2 || hasText(xml))
		throw RuleFault("InvalidRule");
	Tree result, points;
	result.put("Name", xml.get<std::string>("<xmlattr>.Name"));
	if (result.get<std::string>("Name").empty())
		throw RuleFault("InvalidRule");
	result.put("Type", Type);
	if (previous)
		points = previous->get_child("Field");
	else
		for (auto [x, y] : {std::pair{-1.0, -1.0}, {1.0, -1.0}, {1.0, 1.0}, {-1.0, 1.0}})
		{
			Tree p;
			p.put("x", x);
			p.put("y", y);
			points.push_back({"", p});
		}
	const auto& params = xml.get_child("tt:Parameters");
	if (hasText(params) || params.size() > 1)
		throw RuleFault("InvalidRule");
	if (!params.empty())
	{
		const auto& [key, item] = params.front();
		if (key != "tt:ElementItem" || item.get<std::string>("<xmlattr>.Name") != "Field" || item.size() != 2 ||
				item.get_child("<xmlattr>").size() != 1 || item.count("tt:Polygon") != 1 || hasText(item))
			throw RuleFault("InvalidRule");
		const auto& value = item.get_child("tt:Polygon");
		if (hasText(value))
			throw RuleFault("InvalidRule");
		points.clear();
		for (const auto& [key, point] : value)
		{
			if (key != "tt:Point" || point.size() != 1 || point.get_child("<xmlattr>").size() != 2 || hasText(point))
				throw RuleFault("InvalidRule");
			Tree p;
			p.put("x", point.get<double>("<xmlattr>.x"));
			p.put("y", point.get<double>("<xmlattr>.y"));
			points.push_back({"", p});
		}
	}
	if (points.size() < 3 || points.size() > 8)
		throw RuleFault("InvalidRule");
	std::set<std::pair<double, double>> unique;
	for (const auto& [_, p] : points)
	{
		auto x = p.get<double>("x"), y = p.get<double>("y");
		if (!std::isfinite(x) || !std::isfinite(y) || x < -1 || x > 1 || y < -1 || y > 1 || !unique.emplace(x, y).second)
			throw RuleFault("InvalidRule");
	}
	if (!bg::is_valid(polygon(points)))
		throw RuleFault("InvalidRule");
	result.add_child("Field", points);
	return result;
}

Tree RuleXml(const Tree& rule)
{
	Tree result, points;
	result.put("<xmlattr>.Name", rule.get<std::string>("Name"));
	result.put("<xmlattr>.Type", Type);
	for (const auto& [_, p] : rule.get_child("Field"))
	{
		Tree point;
		point.put("<xmlattr>.x", p.get<double>("x"));
		point.put("<xmlattr>.y", p.get<double>("y"));
		points.add_child("tt:Point", point);
	}
	result.put("tt:Parameters.tt:ElementItem.<xmlattr>.Name", "Field");
	result.put_child("tt:Parameters.tt:ElementItem.tt:Polygon", points);
	return result;
}

Tree Description()
{
	Tree desc;
	desc.put("<xmlattr>.Name", Type);
	desc.put("<xmlattr>.fixed", false);
	desc.put("<xmlattr>.maxInstances", 4);
	desc.put("tt:Parameters.tt:ElementItemDescription.<xmlattr>.Name", "Field");
	desc.put("tt:Parameters.tt:ElementItemDescription.<xmlattr>.Type", "tt:Polygon");
	desc.put("tt:Messages.<xmlattr>.IsProperty", true);
	for (auto [name, type] : {std::pair{"VideoSource", "tt:ReferenceToken"},
														{"AnalyticsConfiguration", "tt:ReferenceToken"},
														{"Rule", "xs:string"}})
	{
		Tree item;
		item.put("<xmlattr>.Name", name);
		item.put("<xmlattr>.Type", type);
		desc.add_child("tt:Messages.tt:Source.tt:SimpleItemDescription", item);
	}
	desc.put("tt:Messages.tt:Data.tt:SimpleItemDescription.<xmlattr>.Name", "IsInside");
	desc.put("tt:Messages.tt:Data.tt:SimpleItemDescription.<xmlattr>.Type", "xs:boolean");
	desc.put("tt:Messages.tt:ParentTopic", Topic);
	return desc;
}

Tree Options()
{
	Tree result, option;
	option.put("<xmlattr>.Name", "Field");
	option.put("<xmlattr>.RuleType", Type);
	option.put("<xmlattr>.minOccurs", 1);
	option.put("<xmlattr>.maxOccurs", 1);
	option.put("tt:PolygonOptions.tt:RectangleOnly", false);
	option.put("tt:PolygonOptions.tt:VertexLimits.tt:Min", 3);
	option.put("tt:PolygonOptions.tt:VertexLimits.tt:Max", 8);
	result.add_child("tan:RuleOptions", option);
	return result;
}

bool Contains(const Tree& rule, double x, double y)
{
	return bg::covered_by(Point(x, y), polygon(rule.get_child("Field")));
}
} // namespace utility::field
