//{{{
/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (C) 2020, Raspberry Pi (Trading) Ltd.
 *
 * libcamera_vid.cpp - libcamera video record app.
 */
//}}}
//{{{  includes
#include <chrono>
#include <poll.h>
#include <signal.h>
#include <sys/signalfd.h>
#include <sys/stat.h>

#include "core/libcamera_encoder.hpp"
#include "output/output.hpp"

using namespace std;
using namespace placeholders;
//}}}

namepsace {
  int signal_received;
  //{{{
  void default_signal_handler (int signal_number) {

    signal_received = signal_number;
    LOG (1, "Received signal " << signal_number);
    }
  //}}}
  //{{{
  int get_key_or_signal (VideoOptions const *options, pollfd p[1]) {

    int key = 0;

    if (signal_received == SIGINT)
      return 'x';

    if (options->keypress) {
      poll(p, 1, 0);
      if (p[0].revents & POLLIN) {
        char* user_string = nullptr;
        size_t len;
        [[maybe_unused]] size_t r = getline (&user_string, &len, stdin);
        key = user_string[0];
        }
      }

    if (options->signal) {
      if (signal_received == SIGUSR1)
        key = '\n';
      else if (signal_received == SIGUSR2)
        key = 'x';
      signal_received = 0;
      }

    return key;
    }
  //}}}
  //{{{
  int get_colourspace_flags (string const &codec) {

    if (codec == "mjpeg" || codec == "yuv420")
      return LibcameraEncoder::FLAG_VIDEO_JPEG_COLOURSPACE;
    else
      return LibcameraEncoder::FLAG_VIDEO_NONE;
    }
  //}}}
  //{{{
  void event_loop (LibcameraEncoder& app) {

    VideoOptions const* options = app.GetOptions();
    unique_ptr<Output> output = unique_ptr<Output>(Output::Create( options));
    app.SetEncodeOutputReadyCallback (bind (&Output::OutputReady, output.get(), _1, _2, _3, _4));
    app.SetMetadataReadyCallback (bind (&Output::MetadataReady, output.get(), _1));

    app.OpenCamera();
    app.ConfigureVideo (get_colourspace_flags (options->codec));
    app.StartEncoder();
    app.StartCamera();
    auto start_time = chrono::high_resolution_clock::now();

    // Monitoring for keypresses and signals.
    signal (SIGUSR1, default_signal_handler);
    signal (SIGUSR2, default_signal_handler);
    signal (SIGINT, default_signal_handler);
    pollfd p[1] = { { STDIN_FILENO, POLLIN, 0 } };

    for (unsigned int count = 0; ; count++) {
      LibcameraEncoder::Msg msg = app.Wait();
      if (msg.type == LibcameraApp::MsgType::Timeout) {
        LOG_ERROR ("ERROR: Device timeout detected, attempting a restart!!!");
        app.StopCamera();
        app.StartCamera();
        continue;
        }

      if (msg.type == LibcameraEncoder::MsgType::Quit)
        return;
      else if (msg.type != LibcameraEncoder::MsgType::RequestComplete)
        throw runtime_error ("unrecognised message!");

      int key = get_key_or_signal (options, p);
      if (key == '\n')
        output->Signal();

      LOG (2, "Viewfinder frame " << count);
      auto now = chrono::high_resolution_clock::now();
      bool timeout = !options->frames &&
                     options->timeout &&
                     ((now - start_time) > options->timeout.value);
      bool frameout = options->frames && count >= options->frames;
      if (timeout || frameout || 
          key == 'x' || key == 'X'|| 
          key == 'q' || key == 'Q') {
        if (timeout)
          LOG (1, "Halting: reached timeout of " << options->timeout.get<chrono::milliseconds>() << " milliseconds.");
        app.StopCamera(); // stop complains if encoder very slow to close
        app.StopEncoder();
        return;
        }

      CompletedRequestPtr &completed_request = get<CompletedRequestPtr>(msg.payload);
      app.EncodeBuffer (completed_request, app.VideoStream());
      app.ShowPreview (completed_request, app.VideoStream());
      }
    }
  //}}}
  }

//{{{
int main (int argc, char* argv[]) {

  try {
    LibcameraEncoder app;
    VideoOptions* options = app.GetOptions();
    if (options->Parse (argc, argv)) {
      if (options->verbose >= 2)
        options->Print();
      event_loop(app);
      }
    }
  catch (exception const &e) {
    LOG_ERROR ("ERROR: *** " << e.what() << " ***");
    return -1;
    }

  return 0;
  }
//}}}
