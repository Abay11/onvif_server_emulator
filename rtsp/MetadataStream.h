#pragma once

#include <gst/rtsp-server/rtsp-server.h>

#include <memory>

class ILogger;
namespace osrv::event
{
class IEventSource;
}

namespace osrv::metadata { struct Settings; }

namespace osrv::rtsp
{
// Factory callbacks retain shared state, never the owning RTSP Server object.
class MetadataStreams
{
public:
	MetadataStreams(std::shared_ptr<event::IEventSource> source, const ILogger& logger, GMainContext* context);
	~MetadataStreams();
	void Attach(GstRTSPMediaFactory* factory, std::shared_ptr<const metadata::Settings> settings = {});
	static void Synchronize(GstRTSPMedia* media);
	void Stop();
	static void OnPlay(GstRTSPClient*, GstRTSPContext* context, gpointer);

private:
	struct State;
	std::shared_ptr<State> state_;
};
} // namespace osrv::rtsp
