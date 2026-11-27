#pragma once

#include "../onvif/RecordingArchive.h"

#include <gst/rtsp-server/rtsp-server.h>
#include <memory>

namespace osrv::rtsp
{
GstRTSPMediaFactory* ArchiveFactory(const RecordingArchive::Entry& entry);
void AttachArchiveReplay(GstRTSPServer* server, std::shared_ptr<RecordingArchive> archive);
} // namespace osrv::rtsp
