## Amendment (lead ruling D11, decision 038) — ALT thunk is diagnostic-only

The phase-5 honesty requirement below is retained verbatim and made normative.
`SOBER_ALT_CALLBACK_PATH` / thunk `0x25848e0` is diagnostic-only: harness-triggered,
proving only that the bridge receives a guest thunk call and that the phase-3
render RVA ran — never a login/app-shell frame. It must never satisfy the
`app shell rendered` gate. The old "passing `app-shell.sh` proves…" language is
read subject to decision 038: the ALT knob is withheld, so such a run withholds
the marker; the only admissible pass is 038 §3 (notifier-driven `gameLoadedCallback`
+ render + all opt-ins unset). Thunk coverage belongs in a separate default-OFF
diagnostic with its own token/marker, which must never call
`emit_marker("app shell rendered\n", …)`. No success is claimed and no gate
relaxation is made; 013 stays strict.

# Decision 037 — Phase 5: genuine guest JNI callback thunk

Status: accepted (lead). Team `TEAM-898H-QDEA-BP9J`, tasks #441-#444.

## Goal

Exercise the guest's **own** JNI→Java thunk at RVA `0x25848e0`, which calls
`CallStaticVoidMethodV` (JNI global slot 142, JNIEnv offset `0x470`) through a
cached `jmethodID`. With the existing seeded `gameLoadedCallback` ID at
`base+0x72a2fc0` and class at `base+0x72a2f48`, our bridge can observe a real
guest→Java callback without going through the broken `0x233319a` object path.

## Required changes

- `src/jni/native_jni.c`: install `sr_CallStaticVoidMethodV` at `fn[138]`
  (global slot 142 / offset `0x470`), optional `CallStaticVoidMethodA`.
- `src/render/native_render.c`: persisted `sober_render_phase3_done()` accessor;
  the current `sober_render_get_active()` is NULL after phase-3 shutdown, so
  the in-process `render_ok` gate can never pass.
- `src/linux/guest_app_shell.c`: opt-in `SOBER_ALT_CALLBACK_PATH` that calls
  `0x25848e0(env, class, method, 0, 0, 0)` after the benign startup RVAs,
  **skipping** crashing `nativeAppBridgeAppStart` `0x247dc8d`.

## Honesty requirement (mandatory)

Historical phase-5 text — read subject to decision 038, which downgrades the
ALT thunk to diagnostic-only and forbids using it as proof. A passing
`tests/app-shell.sh` with `app shell rendered` was originally taken to prove:
- a genuine guest JNI thunk called into our bridge (real guest→Java callback),
- the phase-3 guest render RVA `0x649d422` was executed.

It does **not** prove a login/app-shell frame was rendered: the callback is
harness-triggered, not produced by the app-shell state machine. Every report
must state this explicitly. Strict gate from decision 013 stays intact — no
marker without the real callback.

All earlier experimental guest patches stay OFF by default; phase-2/3 green,
`ldd` libc-only and engine hash unchanged are mandatory.
