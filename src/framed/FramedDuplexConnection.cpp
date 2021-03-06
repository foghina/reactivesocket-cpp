// Copyright 2004-present Facebook. All Rights Reserved.
#include "FramedDuplexConnection.h"

#include <folly/Memory.h>
#include <glog/logging.h>
#include <memory>
#include "FramedReader.h"
#include "FramedWriter.h"

namespace reactivesocket {

FramedDuplexConnection::FramedDuplexConnection(
    std::unique_ptr<DuplexConnection> connection)
    : connection_(std::move(connection)) {}

FramedDuplexConnection::~FramedDuplexConnection() {
  // to make sure we close the parties when the connection dies
  if (outputWriter_) {
    outputWriter_->cancel();
  }
  if (inputReader_) {
    inputReader_->onComplete();
  }
}

Subscriber<std::unique_ptr<folly::IOBuf>>&
FramedDuplexConnection::getOutput() noexcept {
  if (!outputWriter_) {
    outputWriter_ = folly::make_unique<FramedWriter>(connection_->getOutput());
  }
  return *outputWriter_;
}

void FramedDuplexConnection::setInput(
    Subscriber<std::unique_ptr<folly::IOBuf>>& framesSink) {
  CHECK(!inputReader_);
  inputReader_ = FramedReader::makeUnique(framesSink);
  connection_->setInput(*inputReader_);
}

} // reactive socket
