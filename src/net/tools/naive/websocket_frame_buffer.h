// Copyright 2026 klzgrad <kizdiv@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_TOOLS_NAIVE_WEBSOCKET_FRAME_BUFFER_H_
#define NET_TOOLS_NAIVE_WEBSOCKET_FRAME_BUFFER_H_

#include <memory>
#include <optional>
#include <vector>

#include "net/base/io_buffer.h"
#include "net/websockets/websocket_frame.h"

namespace net {

// Presents decoded WebSocket frames to Naive as a byte stream while keeping
// partially consumed frames intact.
//
// Lifetime: the frames are owned by WebSocketStream, which only guarantees
// their payloads until the next ReadFrames() call (see
// net/websockets/websocket_stream.h). Callers must drain this buffer before
// reading more frames.
//
// Consumption:
// - Append() takes the frames over; ProcessControlFrames() consumes Ping/Pong
//   and reports Close, leaving data frames in place for Read().
// - TakeStatus() consumes the status byte at the head of the first non-empty
//   binary frame; the rest of that frame, if any, stays buffered as payload.
// - Read() consumes payload bytes and resumes a partially consumed frame.
// - Clear() drops everything, including a partially consumed frame.
class WebSocketFrameBuffer {
 public:
  struct ControlFrames {
    bool closed = false;
    std::optional<std::vector<uint8_t>> ping_payload;
  };

  WebSocketFrameBuffer();
  ~WebSocketFrameBuffer();

  WebSocketFrameBuffer(const WebSocketFrameBuffer&) = delete;
  WebSocketFrameBuffer& operator=(const WebSocketFrameBuffer&) = delete;

  void Append(std::vector<std::unique_ptr<WebSocketFrame>> frames);
  bool TakeStatus(uint8_t* status);
  ControlFrames ProcessControlFrames();
  int Read(IOBuffer* buffer, int length);
  bool empty() const { return frames_.empty(); }
  void Clear();

 private:
  std::vector<std::unique_ptr<WebSocketFrame>> frames_;
  size_t read_offset_ = 0;
};

}  // namespace net

#endif  // NET_TOOLS_NAIVE_WEBSOCKET_FRAME_BUFFER_H_
