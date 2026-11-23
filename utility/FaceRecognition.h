#pragma once

#include "LicensePlateRecognition.h"

namespace utility::face
{
using Tree = boost::property_tree::ptree;
inline constexpr const char* Type = "tt:FaceRecognition";
inline constexpr const char* Topic = "tns1:RuleEngine/Recognition/Face";
struct Image
{
	lpr::Image image;
	std::string uri;
 lpr::Image preview;
 std::string previewUri;
};
using Images = std::vector<Image>;
Images LoadImages(const std::string& configsPath, const std::string& httpAddress);
std::string ImagePath(size_t index);
std::string SceneImagePath(size_t index);
bool ImagesReady(const Images& images);
struct Sample
{
	size_t imageIndex = 0;
	double likelihood = 0.98;
};
Tree ReadRule(const Tree& xml, const Tree* previous);
Tree RuleXml(const Tree& rule);
Tree Description();
Tree Options();
} // namespace utility::face
