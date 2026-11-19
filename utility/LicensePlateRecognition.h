#pragma once

#include <array>
#include <boost/property_tree/ptree.hpp>
#include <string>
#include <vector>

namespace utility::lpr
{
using Tree = boost::property_tree::ptree;
inline constexpr const char* Type = "tt:LicensePlateRecognition";
inline constexpr const char* Topic = "tns1:RuleEngine/Recognition/LicensePlate";
inline constexpr const char* PlateImagePath = "/analytics/lpr/plate.png";
inline constexpr const char* VehicleImagePath = "/analytics/lpr/vehicle.png";
struct Image
{
	std::string bytes;
	std::string base64;
};
struct ImagePair
{
	Image plate;
	Image vehicle;
	std::string plateUri;
	std::string vehicleUri;
};
using Images = std::vector<ImagePair>;
bool ImagesReady(const Images& images);
std::string ImagePath(size_t index, bool plate);
Images LoadImages(const std::string& configsPath, const std::string& httpAddress);
struct Sample
{
	std::string plate;
	std::string country;
	std::string location = "Front";
	double x = 0, y = 0;
	double likelihood = 0.95;
	double width = 10;	 // Percentage of scene image width.
	double speed = 12.5; // Metres per second; synthetic, independent of normalized screen motion.
	std::string direction = "Right";
	std::string vehicleType = "Car";
	std::string vehicleBrand = "Test Brand";
	std::string vehicleModel = "Synthetic Vehicle";
	double height = 5; // Percentage of scene image height.
	size_t imageIndex = 0;
};
const std::array<Sample, 5>& TestVehicles();
Sample SyntheticSample(size_t step);
Tree ReadRule(const Tree& xml, const Tree* previous);
Tree RuleXml(const Tree& rule);
Tree Description();
Tree Options();
} // namespace utility::lpr
