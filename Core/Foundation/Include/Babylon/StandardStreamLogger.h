#pragma once

#include "Api.h"

namespace Babylon::StandardStreamLogger
{
    /**
     * Starts process-wide standard-stream forwarding.
     *
     * Android forwards to logcat, Apple platforms forward to os_log, and Windows
     * forwards to OutputDebugString while preserving the original stream destination.
     * Other Unix platforms already expose standard streams and leave them unchanged.
     *
     * Persistent private descriptors are non-inheritable, and the original standard-stream
     * inheritance flags are preserved (including the Windows CRT no-inherit state
     * when the original handle is non-inheritable). Applications must serialize
     * concurrent child-process creation with Start()/Stop(): redirection and flag
     * restoration are not a single atomic operation. Windows also uses a temporary
     * CRT mode probe and reopens non-inheritable standard descriptors; serialize
     * concurrent CRT descriptor allocation with Start()/Stop() as well. Apple
     * lacks atomic close-on-exec pipe creation.
     *
     * Platform diagnostics split long lines to fit their sink's size limit.
     * Chunking does not affect the tee to the original stream destination.
     * Windows preserves CRT translation modes for targets with valid handles. Already-translated
     * pipe bytes are teed without another translation; UTF-16 diagnostics are
     * decoded to UTF-8 before line splitting (malformed input uses U+FFFD).
     *
     * Other Windows CRT descriptor flags, including _O_APPEND, are not retained
     * in the private copies or restored target descriptors. Applications requiring
     * CRT append semantics must not use Windows forwarding.
     *
     * Returns false if a platform stream could not be redirected. Repeated calls are
     * idempotent.
     */
    bool BABYLON_API Start();

    /**
     * Flushes pending output, restores the original streams, and stops forwarding.
     *
     * Returns false if an original stream could not be restored or pending output
     * could not be drained before the shutdown timeout. Repeated calls are idempotent.
     */
    bool BABYLON_API Stop();

    /**
     * Returns whether Start() has successfully begun process-wide forwarding and
     * Stop() has not yet completed.
     *
     * This is the logical started flag, not a live probe of the underlying file
     * descriptors. On platforms that leave stdout/stderr unchanged (plain Linux
     * and other non-Android Unix hosts), Start() still succeeds and IsStarted()
     * reports true even though no redirection was installed.
     */
    bool BABYLON_API IsStarted();
}