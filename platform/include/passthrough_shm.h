#pragma once

// Dead Space Prime's passthrough bridge: opens the SharedBlock (passthrough_protocol.h) both
// games share, whichever starts first. Windows uses a named file mapping in the session's
// Local\ namespace; other systems a POSIX shared memory object, for tests and tools. Plain
// C++ and the OS, so the port, the probe tool and the host plugin can all use it.

#include "passthrough_protocol.h"

#include <string>

namespace Passthrough {

enum class OpenResult {
  Ok,
  Unsupported,     // no shared memory on this system (Android)
  Failed,          // the OS refused (see Mapping::Error)
  VersionMismatch, // the block was made by another version of the bridge
};

class Mapping {
public:
  Mapping() = default;
  ~Mapping() { Close(); }
  Mapping(const Mapping&) = delete;
  Mapping& operator=(const Mapping&) = delete;

  // Opens the block called `name` (kSharedMemoryName unless a test picks its own), making and
  // initialising it if nobody has yet.
  OpenResult Open(const char* name = kSharedMemoryName);
  void Close();
  bool IsOpen() const { return mBlock != nullptr; }
  SharedBlock* Block() const { return mBlock; }
  // The OS's reason for the last Failed, for logs and the F1 status.
  const std::string& Error() const { return mError; }
  // Removes a POSIX block's name, so the next Open makes a fresh one; for tests. Windows drops
  // a mapping by itself once nobody has it open.
  static void Unlink(const char* name);

private:
  SharedBlock* mBlock = nullptr;
  std::string mError;
#ifdef _WIN32
  void* mHandle = nullptr;
#endif
};

} // namespace Passthrough
