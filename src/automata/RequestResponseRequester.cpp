// Copyright 2004-present Facebook. All Rights Reserved.

#include "RequestResponseRequester.h"

#include <iostream>

namespace reactivesocket {

void RequestResponseRequesterBase::subscribe(Subscriber<Payload>& subscriber) {
  DCHECK(!consumingSubscriber_);
  consumingSubscriber_.reset(&subscriber);
  // FIXME
  // Subscriber::onSubscribe is delivered externally, as it may attempt to
  // synchronously deliver Subscriber::request.
}

void RequestResponseRequesterBase::onNext(Payload request) {
  switch (state_) {
    case State::NEW: {
      state_ = State::REQUESTED;
      Frame_REQUEST_RESPONSE frame(
          streamId_, FrameFlags_EMPTY, std::move(std::move(request)));
      connection_->outputFrameOrEnqueue(frame.serializeOut());
      break;
    }
    case State::REQUESTED:
      // Cannot receive a request payload twice.
      CHECK(false);
      break;
    case State::CLOSED:
      break;
  }
}

void RequestResponseRequesterBase::request(size_t n) {
  if (payload_) {
    consumingSubscriber_.onNext(std::move(payload_.value()));
    payload_.clear();
  } else {
    waitingForPayload_ = true;
  }
}

void RequestResponseRequesterBase::cancel() {
  switch (state_) {
    case State::NEW:
      state_ = State::CLOSED;
      connection_->endStream(streamId_, StreamCompletionSignal::GRACEFUL);
      break;
    case State::REQUESTED: {
      state_ = State::CLOSED;
      connection_->outputFrameOrEnqueue(Frame_CANCEL(streamId_).serializeOut());
      connection_->endStream(streamId_, StreamCompletionSignal::GRACEFUL);
    } break;
    case State::CLOSED:
      break;
  }
}

void RequestResponseRequesterBase::endStream(StreamCompletionSignal signal) {
  switch (state_) {
    case State::NEW:
    case State::REQUESTED:
      // Spontaneous ::endStream signal means an error.
      DCHECK(StreamCompletionSignal::GRACEFUL != signal);
      state_ = State::CLOSED;
      break;
    case State::CLOSED:
      break;
  }
  // FIXME: switch on signal and verify that callsites propagate stream errors
  consumingSubscriber_.onComplete();
}

void RequestResponseRequesterBase::onNextFrame(Frame_ERROR&& frame) {
  switch (state_) {
    case State::NEW:
      // Cannot receive a frame before sending the initial request.
      CHECK(false);
      break;
    case State::REQUESTED:
      state_ = State::CLOSED;
      consumingSubscriber_.onError(
          std::runtime_error(frame.payload_.moveDataToString()));
      connection_->endStream(streamId_, StreamCompletionSignal::ERROR);
      break;
    case State::CLOSED:
      break;
  }
}

void RequestResponseRequesterBase::onNextFrame(Frame_RESPONSE&& frame) {
  bool end = false;
  switch (state_) {
    case State::NEW:
      // Cannot receive a frame before sending the initial request.
      CHECK(false);
      break;
    case State::REQUESTED:
      if (frame.header_.flags_ & FrameFlags_COMPLETE) {
        state_ = State::CLOSED;
        end = true;
      }
      break;
    case State::CLOSED:
      break;
  }
  if (waitingForPayload_) {
    consumingSubscriber_.onNext(std::move(frame.payload_));
  } else {
    payload_.assign(std::move(frame.payload_));
  }

  if (end) {
    connection_->endStream(streamId_, StreamCompletionSignal::GRACEFUL);
  }
}

std::ostream& RequestResponseRequesterBase::logPrefix(std::ostream& os) {
  return os << " RequestResponseRequester(" << &connection_ << ", " << streamId_
            << "): ";
}
}
