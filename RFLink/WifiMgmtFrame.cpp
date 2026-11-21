extern "C" void *__real_ieee80211_getmgtframe(void **frame, int headroom, int body);

extern "C" void *__wrap_ieee80211_getmgtframe(void **frame, int headroom, int body)
{
  void *buffer = __real_ieee80211_getmgtframe(frame, headroom, body);
  // The SDK keepalive asserts on pool exhaustion. Ask its allocator for
  // capacity above the small-pool limit; the sender still emits 24 bytes.
  // See https://github.com/esphome/esphome/pull/19386.
  if (buffer == nullptr && headroom == 24 && body == 0)
    buffer = __real_ieee80211_getmgtframe(frame, headroom, 65 - headroom);
  return buffer;
}
