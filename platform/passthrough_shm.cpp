#include "passthrough_shm.h"

#include <chrono>
#include <thread>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif !defined(__ANDROID__)
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace Passthrough {

namespace {
// Marks a block whose maker is still writing its header.
constexpr uint32_t kInitialising = 0xFFFFFFFFu;

// Makes a fresh (all-zero) block's header, or waits for whoever is making it, then checks the
// version. The OS zero-fills a new block, so magic 0 means nobody has claimed it.
OpenResult Initialise(SharedBlock& block) {
  std::atomic_ref< uint32_t > magic(block.magic);
  uint32_t expected = 0;
  if (magic.compare_exchange_strong(expected, kInitialising, std::memory_order_acq_rel)) {
    block.version = kVersion;
    block.size = sizeof(SharedBlock);
    magic.store(kMagic, std::memory_order_release);
  } else {
    for (int i = 0; i < 200 && magic.load(std::memory_order_acquire) == kInitialising; ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }
  if (magic.load(std::memory_order_acquire) != kMagic || block.version != kVersion ||
      block.size != sizeof(SharedBlock)) {
    return OpenResult::VersionMismatch;
  }
  return OpenResult::Ok;
}
} // namespace

#if defined(_WIN32)

OpenResult Mapping::Open(const char* name) {
  Close();
  std::wstring wide = L"Local\\";
  for (const char* c = name; *c != '\0'; ++c) {
    wide += static_cast< wchar_t >(static_cast< unsigned char >(*c));
  }
  HANDLE handle = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                     sizeof(SharedBlock), wide.c_str());
  if (handle == nullptr) {
    mError = "CreateFileMapping failed, error " + std::to_string(GetLastError());
    return OpenResult::Failed;
  }
  void* view = MapViewOfFile(handle, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedBlock));
  if (view == nullptr) {
    mError = "MapViewOfFile failed, error " + std::to_string(GetLastError());
    CloseHandle(handle);
    return OpenResult::Failed;
  }
  mHandle = handle;
  mBlock = static_cast< SharedBlock* >(view);
  const OpenResult result = Initialise(*mBlock);
  if (result != OpenResult::Ok) {
    Close();
  }
  return result;
}

void Mapping::Close() {
  if (mBlock != nullptr) {
    UnmapViewOfFile(mBlock);
    mBlock = nullptr;
  }
  if (mHandle != nullptr) {
    CloseHandle(static_cast< HANDLE >(mHandle));
    mHandle = nullptr;
  }
}

void Mapping::Unlink(const char*) {}

#elif defined(__ANDROID__)

OpenResult Mapping::Open(const char*) {
  mError = "not available on Android";
  return OpenResult::Unsupported;
}

void Mapping::Close() { mBlock = nullptr; }

void Mapping::Unlink(const char*) {}

#else

OpenResult Mapping::Open(const char* name) {
  Close();
  const std::string path = std::string("/") + name;
  const int fd = shm_open(path.c_str(), O_RDWR | O_CREAT, 0600);
  if (fd < 0) {
    mError = std::string("shm_open failed: ") + std::strerror(errno);
    return OpenResult::Failed;
  }
  struct stat info {};
  if (fstat(fd, &info) != 0 || (info.st_size < static_cast< off_t >(sizeof(SharedBlock)) &&
                                ftruncate(fd, sizeof(SharedBlock)) != 0)) {
    mError = std::string("sizing the block failed: ") + std::strerror(errno);
    close(fd);
    return OpenResult::Failed;
  }
  void* view = mmap(nullptr, sizeof(SharedBlock), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  close(fd);
  if (view == MAP_FAILED) {
    mError = std::string("mmap failed: ") + std::strerror(errno);
    return OpenResult::Failed;
  }
  mBlock = static_cast< SharedBlock* >(view);
  const OpenResult result = Initialise(*mBlock);
  if (result != OpenResult::Ok) {
    Close();
  }
  return result;
}

void Mapping::Close() {
  if (mBlock != nullptr) {
    munmap(mBlock, sizeof(SharedBlock));
    mBlock = nullptr;
  }
}

void Mapping::Unlink(const char* name) { shm_unlink((std::string("/") + name).c_str()); }

#endif

} // namespace Passthrough
