#pragma once

#include <boost/property_tree/ptree.hpp>

namespace utility::field
{
using Tree = boost::property_tree::ptree;
inline constexpr const char* Type = "tt:FieldDetector";
inline constexpr const char* Topic = "tns1:RuleEngine/FieldDetector/ObjectsInside";

Tree ReadRule(const Tree& xml, const Tree* previous);
Tree RuleXml(const Tree& rule);
Tree Description();
Tree Options();
bool Contains(const Tree& rule, double x, double y);
} // namespace utility::field
