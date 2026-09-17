// Hands rtc-mv frame rings from the process that creates them to the one that
// reads them, where rings can't be opened by name (Android's memfd rings). The
// creating process serves an abstract unix socket; a reader asks for a ring by
// name and gets a read-only fd. Only peers with the same uid are served.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define RINGBROKER_ANDROID_SOCKET "quackchat.frames"

// Serves on a background thread. Returns 0, or -1 if the socket can't be bound.
// A process runs one broker; later calls return the first call's result.
int ringbroker_start(const char *socket_name);

// Returns a read-only fd for the ring, or -1. The caller owns the fd.
int ringbroker_fetch(const char *socket_name, const char *ring_name);

#ifdef __cplusplus
}
#endif
