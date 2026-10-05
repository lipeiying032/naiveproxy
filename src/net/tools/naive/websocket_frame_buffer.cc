// Copyright 2026 klzgrad <kizdiv@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/tools/naive/websocket_frame_buffer.h"

#ifdef UNSAFE_BUFFERS_BUILD
// TODO(crbug.com/40284755): Remove this and spanify the buffer arithmetic.
#pragma allow_unsafe_buffers
#endif

#include <algorithm>
#include <utility>

#include "base/check_op.h"
#include "base/numerics/safe_conversions.h"

namespace net {
namespace {

bool IsData(const WebSocketFrame& frame) {
  return frame.header.opcode == WebSocketFrameHeader::kOpCodeBinary ||
         frame.header.opcode == WebSocketFrameHeader::kOpCodeContinuation;
}

}  // namespace

WebSocketFrameBuffer::WebSocketFrameBuffer() = default;

WebSocketFrameBuffer::~WebSocketFrameBuffer() = default;

void WebSocketFrameBuffer::Append(
    std::vector<std::unique_ptr<WebSocketFrame>> frames) {
  // Takes ownership of the frames; the caller's vector is left empty.
  frames_.insert(frames_.end(), std::make_move_iterator(frames.begin()),
                 std::make_move_iterator(frames.end()));
}

bool WebSocketFrameBuffer::TakeStatus(uint8_t* status) {
  // The status byte is expected to be the first byte of its own binary message.
  // A peer that packs more bytes into that message is not losing them here:
  // the rest of the frame is kept and read back as tunnel payload.
  for (auto it = frames_.begin(); it != frames_.end(); ++it) {
    if ((*it)->header.opcode != WebSocketFrameHeader::kOpCodeBinary ||
        (*it)->payload.empty()) {
      continue;
    }
    *status = (*it)->payload[0];
    if ((*it)->payload.size() == 1) {
      frames_.erase(it);
      return true;
    }
    (*it)->payload = (*it)->payload.subspan(1);
    (*it)->header.payload_length = (*it)->payload.size();
    return true;
  }
  return false;
}

WebSocketFrameBuffer::ControlFrames
WebSocketFrameBuffer::ProcessControlFrames() {
  // Consumes Ping/Pong frames and reports a Close frame. Data frames are left
  // in place for Read().
  ControlFrames result;
  for (auto it = frames_.begin(); it != frames_.end();) {
    const auto opcode = (*it)->header.opcode;
    if (opcode == WebSocketFrameHeader::kOpCodePing) {
      result.ping_payload.emplace((*it)->payload.begin(),
                                  (*it)->payload.end());
      it = frames_.erase(it);
    } else if (opcode == WebSocketFrameHeader::kOpCodePong) {
      it = frames_.erase(it);
    } else if (opcode == WebSocketFrameHeader::kOpCodeClose) {
      result.closed = true;
      return result;
    } else {
      ++it;
    }
  }
  return result;
}

int WebSocketFrameBuffer::Read(IOBuffer* buffer, int length) {
  // Delivers up to |length| payload bytes. A partially consumed frame stays in
  // the buffer and is resumed by the next call.
  int copied = 0;
  for (auto it = frames_.begin(); it != frames_.end();) {
    if (!IsData(**it)) {
      it = frames_.erase(it);
      continue;
    }
    DCHECK_LE(read_offset_, (*it)->payload.size());
    const size_t available = (*it)->payload.size() - read_offset_;
    const size_t space = base::checked_cast<size_t>(length - copied);
    const size_t count = std::min(available, space);
    buffer->first(base::checked_cast<size_t>(length))
        .subspan(base::checked_cast<size_t>(copied), count)
        .copy_from((*it)->payload.subspan(read_offset_, count));
    copied += base::checked_cast<int>(count);
    read_offset_ += count;
    if (read_offset_ == (*it)->payload.size()) {
      it = frames_.erase(it);
      read_offset_ = 0;
    } else {
      // A partial frame can only remain when the caller's buffer is full.
      break;
    }
    if (copied == length) {
      break;
    }
  }
  return copied;
}

void WebSocketFrameBuffer::Clear() {
  frames_.clear();
  read_offset_ = 0;
}

}  // namespace net
