//{{{
/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (C) 2020, Raspberry Pi (Trading) Ltd.
 *
 * libcamera_raw.cpp - libcamera raw video record app.
 */
//}}}
//{{{  includes
#include <chrono>

#include "core/libcamera_encoder.hpp"
#include "encoder/null_encoder.hpp"
#include "output/output.hpp"

using namespace std;
using namespace placeholders;
//}}}

class LibcameraRaw : public LibcameraEncoder {
public:
  LibcameraRaw() : LibcameraEncoder() {}
protected:
  // Force the use of "null" encoder.
  void createEncoder() { encoder_ = unique_ptr<Encoder>(new NullEncoder (GetOptions())); }
  };

namespace {
  //{{{
  void event_loop (LibcameraRaw& app) {

    VideoOptions const* options = app.GetOptions();
    unique_ptr<Output> output = unique_ptr<Output>(Output::Create(options));

    app.SetEncodeOutputReadyCallback (bind (&Output::OutputReady, output.get(), _1, _2, _3, _4));
    app.SetMetadataReadyCallback (bind (&Output::MetadataReady, output.get(), _1));

    app.OpenCamera();
    app.ConfigureVideo (LibcameraRaw::FLAG_VIDEO_RAW);
    app.StartEncoder();
    app.StartCamera();

    auto start_time = chrono::high_resolution_clock::now();

    for (unsigned int count = 0; ; count++) {
      LibcameraRaw::Msg msg = app.Wait();

      if (msg.type == LibcameraApp::MsgType::Timeout) {
        LOG_ERROR ("ERROR: Device timeout detected, attempting a restart!!!");
        app.StopCamera();
        app.StartCamera();
        continue;
        }
      if (msg.type != LibcameraRaw::MsgType::RequestComplete)
        throw runtime_error ("unrecognised message!");

      if (count == 0) {
        libcamera::StreamConfiguration const &cfg = app.RawStream()->configuration();
        LOG (1, "Raw stream: " << cfg.size.width << "x" 
                               << cfg.size.height << " stride " 
                               << cfg.stride << " format "
                               << cfg.pixelFormat.toString());
        }

      LOG (2, "Viewfinder frame " << count);
      auto now = chrono::high_resolution_clock::now();
      if (options->timeout && (now - start_time) > options->timeout.value) {
        app.StopCamera();
        app.StopEncoder();
        return;
        }

      app.EncodeBuffer (get<CompletedRequestPtr>(msg.payload), app.RawStream());
      }
    }
  //}}}
  }

//{{{
int main (int argc, char* argv[]) {

  try {
    LibcameraRaw app;
    VideoOptions *options = app.GetOptions();
    if (options->Parse (argc, argv)) {
      options->denoise = "cdn_off";
      options->nopreview = true;
      if (options->verbose >= 2)
        options->Print();
      event_loop (app);
      }
    }
  catch (exception const &e) {
    LOG_ERROR ("ERROR: *** " << e.what() << " ***");
    return -1;
    }

  return 0;
  }
//}}}
