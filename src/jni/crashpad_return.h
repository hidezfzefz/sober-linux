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
 *
 * TASK #525 — IS THERE A CLEAN (OPT-IN-FREE) JAVA BOOT PATH TO 0x2eca720?
 * ------------------------------------------------------------------------
 * NO.  Static REA/JADX analysis of the shipped DEX (classes2.dex; the same
 * object the frozen engine was extracted from) gives an unambiguous call
 * graph for the Java declaration of this guest entry:
 *
 *   nativeRunCrashpadHandler([Ljava/lang/String;)I
 *     declared: com/roblox/engine/jni/NativeSettingsInterface.smali
 *     EXACTLY ONE Java caller (REA trace_android_references, total=1):
 *       com.roblox.client.analytics.CrashpadHandler.main(String[])
 *       -> System.load(libroblox.so) then
 *          NativeSettingsInterface.nativeRunCrashpadHandler(argv)
 *       -> logs "Crash handling succeed" / "failed with result %d".
 *     CrashpadHandler.main is a standalone JAVA PROCESS ENTRY POINT (`main`),
 *     launched only when the native crash runtime starts the crash-handler
 *     subprocess.  It is NOT on the Application/Activity boot path.
 *
 *   The real boot path registers that handler class but never runs it:
 *     MainGameActivity / ExperienceSession / uh.f
 *       -> rg.a1.W0 -> X0 -> h0()
 *       -> NativeSettingsInterface.nativeInitCrashpad(
 *              NativeInitCrashpadParams.builder()
 *                .i(CrashpadHandler.class.getCanonicalName()) ... )
 *     nativeInitCrashpad is guest export 0x232d6b8; its disassembly contains
 *     NO call to 0x2eca720 / 0x2ec9eed and NO `movb $1,0x7cdd149`
 *     (cross-check analysis/A15-flags-latch-route.md §2, task510 §4).
 *     It only *installs* the handler; the latch writer runs later, in the
 *     handler subprocess, on an actual crash.
 *
 *   Therefore no unmodified Java boot step reaches the latch writer
 *   0x2ec9fae.  The Java-side step that *would* reach it is exactly
 *   CrashpadHandler.main(String[]) — but that is the crash-handler
 *   subprocess, not boot; replaying it from the harness is a host-initiated
 *   seed, by definition NOT a clean app-shell boot.  No default-ON path is
 *   added here for that reason. LEAD: any run that reaches 0x2eca720 (with or
 *   without this module) is seeded/WITHHELD under decision 013/038.
 *
 * EXACT PREREQUISITES for the (always-withheld) clean-return route:
 *   1. reach the app-shell phase: SOBER_APP_SHELL=1 and SOBER_RENDER=1.
 *   2. SOBER_CRASHPAD_RETURN=1           master kill switch (default OFF).
 *   3. SOBER_FAITHFUL_JNI_ARRAYS=1       the guest's JNI String[] walk uses
 *      fn[167]/fn[169]; with the default stubs it sees length 0 and crashpad
 *      takes its fatal Usage path. install() now FAILS CLOSED if this is not
 *      active (so a missing opt-in can never masquerade as a clean run).
 *   4. optional SOBER_CRASHPAD_RETURN_ARGV / _PASSCRED / _STDIO (sub-knobs).
 *   All of the above are already in the guest_app_shell.c seeded_state chain
 *   (SOBER_CRASHPAD_RETURN* at 1750-1754, SOBER_FAITHFUL_JNI_ARRAYS at 1856,
 *   plus sober_crashpad_return_should_withhold() at 1754).  Marker: WITHHELD.
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

/* Task #525 read-only verdict: 0 — the unmodified Java boot path has no step
 * that reaches nativeRunCrashpadHandler 0x2eca720 / latch writer 0x2ec9fae.
 * The sole Java caller is the crash-handler SUBPROCESS main(); boot only calls
 * nativeInitCrashpad 0x232d6b8 (install, no latch write).  A non-zero return
 * would mean a genuine boot route had been found; it has not.  Never use this
 * to relax the withhold chain. */
SOBER_CRASHPAD_RETURN_API int sober_crashpad_return_java_boot_reachable(void);

/* Task #525 read-only: the exact prerequisites for the always-withheld route
 * (static, comma-separated text; no state is read or mutated). */
SOBER_CRASHPAD_RETURN_API const char *sober_crashpad_return_prereqs(void);

#ifdef __cplusplus
}
#endif
#endif /* SOBER_CRASHPAD_RETURN_H */
