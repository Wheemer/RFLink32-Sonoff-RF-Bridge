#include <cassert>

extern "C" void *__wrap_ieee80211_getmgtframe(void **, int, int);

static int calls;
static int expected_headroom;
static int expected_body;
static bool pool_available;
static bool heap_available;
static int storage;

extern "C" void *__real_ieee80211_getmgtframe(void **frame, int headroom, int body)
{
  assert(headroom == expected_headroom);
  assert(body == (calls == 0 ? expected_body : 41));
  const bool success = calls++ == 0 ? pool_available : heap_available;
  if (success)
    *frame = &storage;
  return success ? &storage : nullptr;
}

static void check(int headroom, int body, bool pool, bool heap, int count, bool success)
{
  calls = 0;
  expected_headroom = headroom;
  expected_body = body;
  pool_available = pool;
  heap_available = heap;
  void *frame = nullptr;
  void *result = __wrap_ieee80211_getmgtframe(&frame, headroom, body);
  assert(calls == count);
  assert(result == (success ? &storage : nullptr));
  assert(result == frame);
}

int main()
{
  check(24, 0, true, true, 1, true);
  check(24, 0, false, true, 2, true);
  check(24, 0, false, false, 2, false);
  check(24, 1, false, true, 1, false);
  check(26, 0, false, true, 1, false);
}
