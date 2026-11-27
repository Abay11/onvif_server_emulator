#pragma once

#include <gst/rtsp-server/rtsp-server.h>

#include <memory>
#include <string>

class ILogger;

namespace osrv::rtsp
{
class VideoProducerPool
{
public:
	explicit VideoProducerPool(ILogger& logger);
	void Attach(GstRTSPMediaFactory* factory, std::string launch);

private:
	struct State;
	std::shared_ptr<State> state_;
};
} // namespace osrv::rtsp
