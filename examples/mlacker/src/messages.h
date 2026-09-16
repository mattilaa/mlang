#pragma once

#include "pluginterfaces/base/ftypes.h"

// Host <-> plugin sample-upload contract.
//
// mlacker sends a VST3 IMessage with this id through the plugin's connection
// point. The plugin decodes the attributes in Plugin::notify() and loads the
// sample into the addressed pad. Two payload shapes are supported:
//
//   * kAttrPcm   : already-decoded interleaved float32 PCM (upload bytes)
//   * kAttrPath  : a WAV file path the plugin decodes itself (upload by path)
//
// A message may carry either one. When both are present, the raw PCM wins.
namespace mlacker_drum {

static const Steinberg::FIDString kUploadMessageId = "mlacker.upload.sample";

static const Steinberg::Vst::CString kAttrPad = "pad";
static const Steinberg::Vst::CString kAttrChannels = "channels";
static const Steinberg::Vst::CString kAttrSampleRate = "sampleRate";
static const Steinberg::Vst::CString kAttrPcm = "pcm";
static const Steinberg::Vst::CString kAttrPath = "path";

} // namespace mlacker_drum
