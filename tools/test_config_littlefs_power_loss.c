#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "lfs.h"

enum { BLOCK = 4096, COUNT = 8 };
static unsigned char disk[BLOCK * COUNT], snapshot[sizeof disk];
static int cut, writes;
static lfs_size_t partial;
static jmp_buf reboot;

static int read_block(const struct lfs_config *c, lfs_block_t b,
                      lfs_off_t off, void *out, lfs_size_t n) {
  (void)c;
  memcpy(out, disk + b * BLOCK + off, n);
  return 0;
}

static void power_loss(void) {
  if (++writes == cut)
    longjmp(reboot, 1);
}

static int program(const struct lfs_config *c, lfs_block_t b,
                   lfs_off_t off, const void *in, lfs_size_t n) {
  (void)c;
  if (writes + 1 == cut)
    for (lfs_size_t i = 0; i < n && i < partial; i++)
      disk[b * BLOCK + off + i] &= ((const unsigned char *)in)[i];
  power_loss();
  for (lfs_size_t i = 0; i < n; i++)
    disk[b * BLOCK + off + i] &= ((const unsigned char *)in)[i];
  return 0;
}

static int erase(const struct lfs_config *c, lfs_block_t b) {
  (void)c;
  power_loss();
  memset(disk + b * BLOCK, 255, BLOCK);
  return 0;
}

static int sync_disk(const struct lfs_config *c) {
  (void)c;
  return 0;
}

static unsigned char rbuf[256], pbuf[256], lookahead[16];
static const struct lfs_config cfg = {
  .read = read_block, .prog = program, .erase = erase, .sync = sync_disk,
  .read_size = 16, .prog_size = 16, .block_size = BLOCK, .block_count = COUNT,
  .block_cycles = 100, .cache_size = 256, .lookahead_size = 16,
  .read_buffer = rbuf, .prog_buffer = pbuf, .lookahead_buffer = lookahead,
};

static void put(lfs_t *fs, const char *path, char value) {
  lfs_file_t file;
  assert(lfs_file_open(fs, &file, path, LFS_O_WRONLY | LFS_O_CREAT) == 0);
  assert(lfs_file_write(fs, &file, &value, 1) == 1);
  assert(lfs_file_close(fs, &file) == 0);
}

static char get(lfs_t *fs, const char *path) {
  lfs_file_t file;
  char value;
  int result = lfs_file_open(fs, &file, path, LFS_O_RDONLY);
  if (result == LFS_ERR_NOENT)
    return 0;
  assert(result == 0 && lfs_file_read(fs, &file, &value, 1) == 1);
  assert(lfs_file_close(fs, &file) == 0);
  return value;
}

static int attempt(int fault, lfs_size_t bytes) {
  lfs_t fs;
  memcpy(disk, snapshot, sizeof disk);
  assert(lfs_mount(&fs, &cfg) == 0);
  writes = 0;
  cut = fault;
  partial = bytes;
  if (setjmp(reboot) == 0) {
    assert(lfs_rename(&fs, "/tmp.json", "/config.json") == 0);
    int result = lfs_remove(&fs, "/config.backup");
    assert(result == 0 || result == LFS_ERR_NOENT);
    assert(lfs_unmount(&fs) == 0);
  }
  cut = 0;
  memset(&fs, 0, sizeof fs);
  assert(lfs_mount(&fs, &cfg) == 0);
  const char primary = get(&fs, "/config.json");
  const char backup = get(&fs, "/config.backup");
  assert(primary == 'n' || primary == 'o' || (!primary && backup == 'o'));
  if (!fault)
    assert(primary == 'n' && !backup);
  assert(lfs_unmount(&fs) == 0);
  return writes;
}

int main(void) {
  int tested = 0;
  for (int recovered = 0; recovered < 2; recovered++) {
    lfs_t fs;
    memset(disk, 255, sizeof disk);
    assert(lfs_format(&fs, &cfg) == 0);
    assert(lfs_mount(&fs, &cfg) == 0);
    put(&fs, recovered ? "/config.backup" : "/config.json", 'o');
    put(&fs, "/tmp.json", 'n');
    assert(lfs_unmount(&fs) == 0);
    memcpy(snapshot, disk, sizeof disk);
    const int total = attempt(0, 0);
    for (int fault = 1; fault <= total; fault++) {
      for (lfs_size_t bytes = 0; bytes < cfg.cache_size; bytes += 4) {
        attempt(fault, bytes);
        tested++;
      }
    }
  }
  printf("LittleFS replacement survived %d simulated interruptions\n", tested);
}
