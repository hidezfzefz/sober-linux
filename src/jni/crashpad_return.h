/*
 * crashpad_return.h - opt-in clean-return route for crashpad HandlerMain
 *                     (worker-2, team task #509).
 *
 * WHY THIS MODULE EXISTS
 * ----------------------
 * Wave #499/#508 proved that a *valid* crashpad argv (real JNI String[]) removes
 * crashpad::HandlerMain's "Usage: %s" fprintf abort, but 0x2ec9eed still
 * SIGSEGVs in crashpad IPC setup.  The fault was re-derived on the frozen
 * engine (BuildID 204ad049…, SHA 3e498d6494…):
 *
 *   0x2ec9eed (latch writer) -> 0x2eca565 call 0x2a31a50
 *     -> crashpad::HandlerMain
 *       -> ExceptionHandlerServer::InitializeWithClient / ReceiveClientMessage
 *         -> crashpad::UnixCredentialSocket::RecvMsg (util/linux/socket.cc)
 *           -> 0x2a2665d fputs@plt / 0x2a26665 fflush@plt on __sF+0x130
 *             -> SIGSEGV in glibc _IO_fputs / _IO_fflush
 *
 * The guest assumes a Bionic FILE layout (__sF stride 0x98, stderr = +0x130).
 * The compat shim bridges fwrite/fflush only under SOBER_BIONIC_SF_FULL and
 * does not bridge fputs at all, so any crashpad ERROR log line faults.
 * RecvMsg logs "missing credentials"/"unexpected msg name"/"incorrect payload
 * size" unless the peer supplies BOTH SCM_CREDENTIALS and an exact-size
 * ClientToServerMessage (32 bytes in this engine: +0 int32 version=1,
 * +4 uint32 type=0 == kTypeCheckCredentials).
 *
 * THE VIABLE ROUTE (validated end to end, see analysis/crashpad-return-509.md)
 * ---------------------------------------------------------------------------
 * 1. Real AF_UNIX socketpair with SO_PASSCRED on the guest fd.  The kernel then
 *    attaches SCM_CREDENTIALS to anything the peer sends.  No guest byte write.
 * 2. A real server-peer thread sends a genuine 32-byte CheckCredentials
 *    ExceptionHandlerProtocol message and closes the peer, so RecvMsg succeeds
 *    and the handler's epoll loop observes EPOLLERR and exits cleanly.
 * 3. A host-side Bionic __sF bridge maps fputs AND fflush on __sF+0x098/+0x130
 *    to host stdout/stderr (the surviving error-log path).  This changes no
 *    guest byte and fabricates no callback, vtable or marker.
 * 4. The channel string is seeded with the guest's OWN setter (0x2e57c20), and
 *    argv is handed over as a faithful JNI String[] (sober_jni_new_string_array)
 *    to nativeRunCrashpadHandler (0x2eca720).
 *
 * Result: "crashpad handler: succeed", 0x2ec9eed returns, latch 0x7cdd149 == 1,
 * no SIGSEGV.  This module is OPT-IN (off unless SOBER_CRASHPAD_RETURN=1) and
 * every active run forces the decision-013/038 marker withhold.
 *
 * LEAD: add SOBER_CRASHPAD_RETURN* to the seeded_state chain in
 * guest_app_shell.c and OR in sober_crashpad_return_should_withhold().
 */
#ifndef SOBER_CRASHPAD_RETURN_H
#define SOBER_CRASHPAD_RETURN_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__GNUC__) || defined(__clang__)
#  define SOBER_CRASHPAD_RETURN_API __attribute__((visibility("default")))
#else
#  define SOBER_CRASHPAD_RETURN_API
#endif

/* Kill switch / enable flag.  Default OFF; set to 1 to activate the route. */
#define SOBER_CRASHPAD_RETURN_ENV        "SOBER_CRASHPAD_RETURN"
/* Comma-separated argv strings; "@SOCK@" is replaced by a real AF_UNIX fd. */
#define SOBER_CRASHPAD_RETURN_ARGV_ENV   "SOBER_CRASHPAD_RETURN_ARGV"
/* Default "1": set SO_PASSCRED on both socketpair ends (0 disables, for A/B). */
#define SOBER_CRASHPAD_RETURN_PASSCRED_ENV "SOBER_CRASHPAD_RETURN_PASSCRED"
/* Default "1": bridge Bionic __sF fputs/fflush (0 disables, for A/B). */
#define SOBER_CRASHPAD_RETURN_STDIO_ENV  "SOBER_CRASHPAD_RETURN_STDIO"

/* Frozen engine RVAs (BuildID 204ad049… / SHA 3e498d6494…). */
#define SOBER_CRASHPAD_RETURN_RVA_LATCH    0x7cdd149u /* flags-loaded latch    */
#define SOBER_CRASHPAD_RETURN_RVA_SETTER   0x2e57c20u /* guest std::string set */
#define SOBER_CRASHPAD_RETURN_RVA_CHANNEL  0x72a3710u /* channel std::string   */
#define SOBER_CRASHPAD_RETURN_RVA_JNI      0x2eca720u /* nativeRunCrashpadHdlr */

/* Result codes. */
#define SOBER_CRASHPAD_RETURN_OK        0
#define SOBER_CRASHPAD_RETURN_DISABLED  1
#define SOBER_CRASHPAD_RETURN_ERR      (-1)
#define SOBER_CRASHPAD_RETURN_ZERO     (-2)

/* 1 only when SOBER_CRASHPAD_RETURN=1 (on/yes/true).  Default 0. */
SOBER_CRASHPAD_RETURN_API int sober_crashpad_return_enabled(void);

/* 1 whenever this module seeded guest state (enable or stdio bridge active).
 * The lead ORs this into decision-013 seeded_state; no such run emits a marker. */
SOBER_CRASHPAD_RETURN_API int sober_crashpad_return_should_withhold(void);

/* Perform the genuine clean-return crashpad route.  Inert (no guest call) when
 * disabled.  `jni` is SoberJni* (void* avoids a header dependency);
 * `jni_env`/`jclass` are the real JNI handles. */
SOBER_CRASHPAD_RETURN_API int sober_crashpad_return_install(
    void *guest_base, void *jni_env, void *jclass, void *jni);

/* Read-only: current value of guest byte 0x7cdd149 (0 before install()). */
SOBER_CRASHPAD_RETURN_API unsigned sober_crashpad_return_latch(void);

/* Diagnostic: the env-var names this module reads (comma-separated). */
SOBER_CRASHPAD_RETURN_API const char *sober_crashpad_return_env_vars(void);

#ifdef __cplusplus
}
#endif
#endif /* SOBER_CRASHPAD_RETURN_H */
