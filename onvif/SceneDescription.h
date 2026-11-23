#pragma once

#include <boost/property_tree/ptree.hpp>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

namespace osrv::scene
{
inline constexpr const char* ModuleName = "ObjectTracker";
inline constexpr const char* ModuleType = "osrv:ObjectTracker";
inline constexpr const char* ModuleNamespace = "urn:onvif-server-emulator:analytics";

inline constexpr const char* LprModule = "LicensePlateRecognition";
inline constexpr const char* FaceModule = "FaceRecognition";
struct Module
{
	std::string name, type;
};
const std::vector<Module>& Modules();

struct Image
{
	std::string uri, base64;
};
using ImagePtr = std::shared_ptr<const Image>;

struct LicensePlate
{
	std::string number, country;
	double likelihood = 1;
	double width = 10, height = 5; // Percentage of scene image dimensions.
};
struct Vehicle
{
	std::string type, brand, model;
	double speed = 0;
	int red = 0, green = 0, blue = 0;
};
struct Object
{
	std::string id;
	double x = 0, y = 0;
	std::string classification;
	std::optional<LicensePlate> plate;
	ImagePtr image;
	std::optional<Vehicle> vehicle;
	std::string parent;
};
struct Frame
{
	std::uint64_t sequence = 0;
	std::string video_source, configuration, utc_time;
	std::string image_object_id;
	std::string module = ModuleName;
	std::vector<Object> objects;
	std::vector<std::string> deleted;
};
using FramePtr = std::shared_ptr<const Frame>;
struct Batch
{
	std::vector<FramePtr> frames;
	bool skipped = false;
};
class ISource
{
public:
	virtual ~ISource() = default;
	virtual Batch Read(const std::string& source, const std::string& configuration, std::uint64_t after,
										 std::size_t limit = 8) const = 0;
};
// Simulator writes full immutable snapshots. Readers never retain an unbounded queue.
class Buffer : public ISource
{
public:
	std::string Sample(const std::string& source, const std::vector<std::string>& configurations,
										 const std::string& input_id, double x, double y, const std::string& utc_time,
										 const std::string& classification = {}, std::optional<LicensePlate> plate = {},
										 ImagePtr image = {}, const std::string& module = ModuleName, std::optional<Vehicle> vehicle = {},
										 const std::string& parent = {});
	void Forget(const std::string& source, const std::vector<std::string>& configurations, const std::string& input_id,
							const std::string& utc_time);
	Batch Read(const std::string& source, const std::string& configuration, std::uint64_t after,
						 std::size_t limit = 8) const override;

private:
	struct Source
	{

		std::map<std::string, Object> objects;
	};
	void Publish(const std::string& source, const std::vector<std::string>& configurations, const std::string& utc_time,
							 const std::vector<std::string>& deleted, const std::string& image_object_id = {},
							 const std::string& module = ModuleName);
	mutable std::mutex mutex_;
	std::map<std::pair<std::string, std::string>, Source> sources_;
	std::map<std::string, std::uint64_t> next_ids_;
	std::deque<FramePtr> frames_;
	std::map<std::tuple<std::string, std::string, std::string>, FramePtr> latest_;
	std::uint64_t sequence_ = 0;
};
boost::property_tree::ptree FrameXml(const Frame& frame);
boost::property_tree::ptree ModuleXml(const std::string& module = ModuleName);
Frame SampleFrame(ImagePtr image = {});
} // namespace osrv::scene
