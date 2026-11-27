#pragma once

#include <stdint.h>
#include <string.h>

namespace RFLink {
  class CommandReceipts {
  public:
    static constexpr unsigned tokenLength = 16;
    struct Entry {
      char token[tokenLength + 1];
      uint32_t elapsedMs;
      bool ok;
    };

    static bool valid(const char *token) {
      return token && strlen(token) == tokenLength &&
        strspn(token, "0123456789abcdef") == tokenLength;
    }

    const Entry *find(const char *token) const {
      if(valid(token))
        for(const auto &entry : entries)
          if(strcmp(entry.token, token) == 0)
            return &entry;
      return nullptr;
    }

    void remember(const char *token, bool ok, uint32_t elapsedMs) {
      if(!valid(token) || find(token))
        return;
      auto &entry = entries[next];
      memcpy(entry.token, token, sizeof(entry.token));
      entry.ok = ok;
      entry.elapsedMs = elapsedMs;
      next = (next + 1) % 4;
    }

  private:
    Entry entries[4] = {};
    unsigned next = 0;
  };
}
