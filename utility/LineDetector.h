#pragma once

#include <boost/property_tree/ptree.hpp>
#include <utility>

namespace utility::line
{
using Tree = boost::property_tree::ptree;
using Point = std::pair<double, double>;
inline constexpr const char* Type = "tt:LineDetector";
inline constexpr const char* Topic = "tns1:RuleEngine/LineDetector/Crossed";

// XML has already been canonicalized by the shared Rules dispatcher.
Tree ReadRule(const Tree& xml, const Tree* previous);
Tree RuleXml(const Tree& rule);
Tree Description();
Tree Options();
bool OnLine(const Tree& rule, Point point);
bool Crossed(const Tree& rule, Point previous, Point current);
} // namespace utility::line
