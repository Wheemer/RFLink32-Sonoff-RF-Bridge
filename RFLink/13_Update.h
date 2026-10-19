#pragma once

#include <stdint.h>

namespace RFLink::UpdateSecurity {
  void setup();
  bool schedule(const char *url, const char *&error);
  void maintain();
  bool isPending();
  const char *lastError();
  const char *lastResult();
  int32_t lastErrorCode();
  uint32_t lastAttempt();
  const char *signingKeyId();
}
