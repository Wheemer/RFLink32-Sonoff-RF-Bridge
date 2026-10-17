#pragma once

namespace RFLink::UpdateSecurity {
  void setup();
  bool schedule(const char *url, const char *&error);
  void maintain();
  bool isPending();
  const char *lastError();
}
