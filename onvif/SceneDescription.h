#pragma once

#include <boost/property_tree/ptree.hpp>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace osrv::scene
{
inline constexpr const char* ModuleName = "SyntheticObjectTracker";
inline constexpr const char* ModuleType = "osrv:SyntheticObjectTracker";
inline constexpr const char* ModuleNamespace = "urn:onvif-server-emulator:analytics";

struct Object
{
	std::string id;
	double x = 0, y = 0;
	std::string classification;
};
struct Frame
{
	std::uint64_t sequence = 0;
	std::string video_source, configuration, utc_time;
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
										 const std::string& classification = {});
	void Forget(const std::string& source, const std::vector<std::string>& configurations, const std::string& input_id,
							const std::string& utc_time);
	Batch Read(const std::string& source, const std::string& configuration, std::uint64_t after,
						 std::size_t limit = 8) const override;

private:
	struct Source
	{
		std::uint64_t next_id = 1;
		std::map<std::string, Object> objects;
	};
	void Publish(const std::string& source, const std::vector<std::string>& configurations, const std::string& utc_time,
							 const std::vector<std::string>& deleted);
	mutable std::mutex mutex_;
	std::map<std::string, Source> sources_;
	std::deque<FramePtr> frames_;
	std::map<std::pair<std::string, std::string>, FramePtr> latest_;
	std::uint64_t sequence_ = 0;
};
boost::property_tree::ptree FrameXml(const Frame& frame);
boost::property_tree::ptree ModuleXml();
Frame SampleFrame();
} // namespace osrv::scene
