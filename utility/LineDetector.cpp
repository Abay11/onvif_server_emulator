#include "LineDetector.h"
#include "AnalyticsRules.h"
#include <boost/geometry.hpp>
#include <boost/geometry/geometries/linestring.hpp>
#include <boost/geometry/geometries/point_xy.hpp>
#include <cmath>
#include <vector>

namespace utility::line
{
namespace
{
constexpr double Epsilon = 1e-9;
bool hasText(const Tree& node)
{
	return node.data().find_first_not_of(" \t\r\n") != std::string::npos;
}
double side(Point a, Point b, Point p)
{
	return (b.first - a.first) * (p.second - a.second) - (b.second - a.second) * (p.first - a.first);
}
int sign(double value)
{
	return value > Epsilon ? 1 : value < -Epsilon ? -1 : 0;
}
bool between(Point a, Point b, Point p)
{
	return p.first >= std::min(a.first, b.first) - Epsilon && p.first <= std::max(a.first, b.first) + Epsilon &&
				 p.second >= std::min(a.second, b.second) - Epsilon && p.second <= std::max(a.second, b.second) + Epsilon;
}
std::vector<Point> points(const Tree& rule)
{
	std::vector<Point> result;
	for (const auto& [_, p] : rule.get_child("Segments"))
		result.emplace_back(p.get<double>("x"), p.get<double>("y"));
	return result;
}
bool direction(const Tree& rule, int destination)
{
	auto d = rule.get<std::string>("Direction");
	return d == "Any" || (d == "Left" && destination > 0) || (d == "Right" && destination < 0);
}
} // namespace

Tree ReadRule(const Tree& xml, const Tree* previous)
{
	if (xml.get<std::string>("<xmlattr>.Type") != Type || xml.count("tt:Parameters") != 1 || xml.size() != 2)
		throw RuleFault("InvalidRule");
	if (xml.get_child("<xmlattr>").size() != 2 || hasText(xml) || hasText(xml.get_child("tt:Parameters")))
		throw RuleFault("InvalidRule");
	Tree result;
	result.put("Name", xml.get<std::string>("<xmlattr>.Name"));
	if (result.get<std::string>("Name").empty())
		throw RuleFault("InvalidRule");
	result.put("Type", Type);
	result.put("Direction", previous ? previous->get<std::string>("Direction") : "Any");
	if (previous)
		result.put_child("Segments", previous->get_child("Segments"));
	else
	{
		Tree list;
		for (double y : {-1.0, 1.0})
		{
			Tree p;
			p.put("x", 0.0);
			p.put("y", y);
			list.push_back({"", p});
		}
		result.add_child("Segments", list);
	}
	bool seenDirection = false, seenSegments = false;
	for (const auto& [key, item] : xml.get_child("tt:Parameters"))
	{
		if (hasText(item))
			throw RuleFault("InvalidRule");
		if (key == "tt:SimpleItem" && item.get<std::string>("<xmlattr>.Name") == "Direction" && !seenDirection &&
				!seenSegments && item.size() == 1 && item.get_child("<xmlattr>").size() == 2)
		{
			seenDirection = true;
			result.put("Direction", item.get<std::string>("<xmlattr>.Value"));
		}
		else if (key == "tt:ElementItem" && item.get<std::string>("<xmlattr>.Name") == "Segments" && !seenSegments &&
						 item.size() == 2 && item.count("tt:Polyline") == 1 && item.get_child("<xmlattr>").size() == 1)
		{
			seenSegments = true;
			if (hasText(item.get_child("tt:Polyline")))
				throw RuleFault("InvalidRule");
			Tree list;
			for (const auto& [pointKey, point] : item.get_child("tt:Polyline"))
			{
				if (pointKey != "tt:Point" || point.size() != 1 || point.get_child("<xmlattr>").size() != 2 || hasText(point))
					throw RuleFault("InvalidRule");
				Tree p;
				p.put("x", point.get<double>("<xmlattr>.x"));
				p.put("y", point.get<double>("<xmlattr>.y"));
				list.push_back({"", p});
			}
			result.put_child("Segments", list);
		}
		else
			throw RuleFault("InvalidRule");
	}
	auto d = result.get<std::string>("Direction");
	if (d != "Any" && d != "Left" && d != "Right")
		throw RuleFault("InvalidRule");
	auto pts = points(result);
	if (pts.size() < 2 || pts.size() > 8)
		throw RuleFault("InvalidRule");
	boost::geometry::model::linestring<boost::geometry::model::d2::point_xy<double>> path;
	for (size_t i = 0; i < pts.size(); ++i)
	{
		auto [x, y] = pts[i];
		if (!std::isfinite(x) || !std::isfinite(y) || x < -1 || x > 1 || y < -1 || y > 1)
			throw RuleFault("InvalidRule");
		for (size_t j = 0; j < i; ++j)
			if (std::hypot(x - pts[j].first, y - pts[j].second) <= Epsilon)
				throw RuleFault("InvalidRule");
		path.emplace_back(x, y);
	}
	if (!boost::geometry::is_simple(path))
		throw RuleFault("InvalidRule");
	return result;
}

Tree RuleXml(const Tree& rule)
{
	Tree result, polyline;
	result.put("<xmlattr>.Name", rule.get<std::string>("Name"));
	result.put("<xmlattr>.Type", Type);
	result.put("tt:Parameters.tt:SimpleItem.<xmlattr>.Name", "Direction");
	result.put("tt:Parameters.tt:SimpleItem.<xmlattr>.Value", rule.get<std::string>("Direction"));
	for (auto [x, y] : points(rule))
	{
		Tree p;
		p.put("<xmlattr>.x", x);
		p.put("<xmlattr>.y", y);
		polyline.add_child("tt:Point", p);
	}
	result.put("tt:Parameters.tt:ElementItem.<xmlattr>.Name", "Segments");
	result.put_child("tt:Parameters.tt:ElementItem.tt:Polyline", polyline);
	return result;
}

Tree Description()
{
	Tree desc;
	desc.put("<xmlattr>.Name", Type);
	desc.put("<xmlattr>.fixed", false);
	desc.put("<xmlattr>.maxInstances", 4);
	desc.put("tt:Parameters.tt:SimpleItemDescription.<xmlattr>.Name", "Direction");
	desc.put("tt:Parameters.tt:SimpleItemDescription.<xmlattr>.Type", "tt:Direction");
	desc.put("tt:Parameters.tt:ElementItemDescription.<xmlattr>.Name", "Segments");
	desc.put("tt:Parameters.tt:ElementItemDescription.<xmlattr>.Type", "tt:Polyline");
	desc.put("tt:Messages.<xmlattr>.IsProperty", false);
	for (auto [name, type] : {std::pair{"VideoSource", "tt:ReferenceToken"},
														{"AnalyticsConfiguration", "tt:ReferenceToken"},
														{"Rule", "xs:string"}})
	{
		Tree item;
		item.put("<xmlattr>.Name", name);
		item.put("<xmlattr>.Type", type);
		desc.add_child("tt:Messages.tt:Source.tt:SimpleItemDescription", item);
	}
	desc.put("tt:Messages.tt:Data.tt:SimpleItemDescription.<xmlattr>.Name", "ObjectId");
	desc.put("tt:Messages.tt:Data.tt:SimpleItemDescription.<xmlattr>.Type", "tt:StringList");
	desc.put("tt:Messages.tt:ParentTopic", Topic);
	return desc;
}

Tree Options()
{
	Tree result, d, s;
	d.put("<xmlattr>.Name", "Direction");
	d.put("<xmlattr>.RuleType", Type);
	d.put("<xmlattr>.minOccurs", 0);
	d.put("<xmlattr>.maxOccurs", 1);
	d.put("tt:StringList", "Left Right Any");
	result.add_child("tan:RuleOptions", d);
	s.put("<xmlattr>.Name", "Segments");
	s.put("<xmlattr>.RuleType", Type);
	s.put("<xmlattr>.minOccurs", 1);
	s.put("<xmlattr>.maxOccurs", 1);
	s.put("tt:IntRange.tt:Min", 2);
	s.put("tt:IntRange.tt:Max", 8);
	result.add_child("tan:RuleOptions", s);
	return result;
}

bool OnLine(const Tree& rule, Point point)
{
	auto pts = points(rule);
	for (size_t i = 1; i < pts.size(); ++i)
		if (sign(side(pts[i - 1], pts[i], point)) == 0 && between(pts[i - 1], pts[i], point))
			return true;
	return false;
}

bool Crossed(const Tree& rule, Point previous, Point current)
{
	auto pts = points(rule);
	for (size_t i = 1; i < pts.size(); ++i)
	{
		auto a = pts[i - 1], b = pts[i];
		int before = sign(side(a, b, previous)), after = sign(side(a, b, current));
		if (before * after < 0 && sign(side(previous, current, a)) * sign(side(previous, current, b)) < 0 &&
				direction(rule, after))
			return true;
	}
	// At an interior vertex, both adjacent edges must lie on opposite sides of the trajectory.
	for (size_t i = 1; i + 1 < pts.size(); ++i)
		if (sign(side(previous, current, pts[i])) == 0 && between(previous, current, pts[i]) &&
				sign(side(previous, current, pts[i - 1])) * sign(side(previous, current, pts[i + 1])) < 0 &&
				direction(rule, sign(side(pts[i - 1], pts[i], current))))
			return true;
	return false;
}
} // namespace utility::line
