#pragma once

#include "../onvif/Notification.h"
#include "LineCounting.h"
#include "LicensePlateRecognition.h"
#include "FaceRecognition.h"
#include <optional>
#include <boost/property_tree/ptree.hpp>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <tuple>

namespace osrv { class DetectorControls; }

namespace utility
{
class RuleFault : public std::runtime_error
{
public:
	RuleFault(std::string detail, bool receiver = false)
			: std::runtime_error(detail), detail(std::move(detail)), receiver(receiver)
	{
	}
	std::string detail;
	bool receiver;
};

// Rules and event state share a lock; the event thread never accesses the media manager.
class AnalyticsRules
{
public:
	using Tree = boost::property_tree::ptree;
	using Message = osrv::event::NotificationMessage;
	using SaveRules = std::function<void(const std::string&, const Tree&)>;
	using Clock = std::function<counting::Time()>;
	AnalyticsRules(const Tree& profiles, SaveRules save, Clock clock = std::chrono::system_clock::now,
			lpr::Images images = {}, face::Images faces = {});
	Tree Execute(const std::string& operation, const std::string& xml);
	Tree Snapshot() const;
	Tree DefaultRule(const std::string& token, const std::string& type) const;
	void BindControls(std::shared_ptr<osrv::DetectorControls> controls);
	std::deque<Message> Synchronize() const;
	std::deque<Message> Sample(double x, double y, double strength, const std::string& videoSource = {});
	std::deque<Message> SampleObject(const std::string& videoSource, const std::string& objectId, double x, double y,
																	 const std::string& classification = {});
	std::deque<Message> TickCounting(const std::string& videoSource = {});
	std::deque<Message> SynchronizeCounts() const;
	std::deque<Message> SynchronizeFields() const;
	std::deque<Message> TickFields(const std::string& videoSource = {});
	std::deque<Message> SamplePlate(const std::string& videoSource, const lpr::Sample& sample);
	osrv::scene::Frame SupportedScene(const std::string& module = osrv::scene::LprModule) const;
	std::deque<Message> SampleFace(const std::string& videoSource, const face::Sample& sample);
	void ForgetObject(const std::string& videoSource, const std::string& objectId);
	std::vector<std::string> VideoSources() const;
	std::shared_ptr<osrv::scene::ISource> Scenes() const
	{
		return scenes_;
	}
	static Tree RuleXml(const Tree& rule);
	static bool SupportsRule(const Tree& config, std::string_view type);
	static bool SupportsModule(const Tree& config, std::string_view name, bool installed = false);
	static constexpr const char* Topic = "tns1:RuleEngine/MotionRegionDetector/Motion";

private:
	Message Notification(const std::string& token, const Tree& rule, const std::string& operation, bool state) const;
	Message CountNotification(const std::string& token, const Tree& rule, const std::string& operation, int count) const;
	Message FieldNotification(const std::string& token, const Tree& rule, const std::string& operation,
														bool inside) const;
	struct PlateState
	{
		std::optional<std::pair<double, double>> position;
		std::optional<counting::Time> last;
	};
	std::map<std::tuple<std::string, std::string, std::string, std::string>, PlateState> plates_;
	std::shared_ptr<osrv::DetectorControls> controls_;
	Tree profiles_;
	SaveRules save_;
	Clock clock_;
	lpr::Images images_;
	std::vector<osrv::scene::ImagePtr> scene_images_;
 std::vector<osrv::scene::ImagePtr> face_scene_images_;
	face::Images faces_;
	std::shared_ptr<osrv::scene::Buffer> scenes_ = std::make_shared<osrv::scene::Buffer>();
	mutable std::mutex mutex_;
	std::map<std::pair<std::string, std::string>, bool> states_;
	std::map<std::tuple<std::string, std::string, std::string>, std::pair<double, double>> tracks_;
	std::deque<Message> pending_;
	std::map<std::pair<std::string, std::string>, counting::State> counters_;
	std::deque<Message> pending_counts_;
	std::map<std::pair<std::string, std::string>, std::set<std::string>> inside_;
	std::deque<Message> pending_fields_;
};
} // namespace utility
