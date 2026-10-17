# Vier punten in clap-wrapper v0.16.0, klaar om te melden

Deze sessie mag in free-audio/clap-wrapper geen issue openen en niet forken (alleen eigen repos). Alles hieronder is
klaar om te plakken. De drie patches gaan schoon op v0.16.0 (= hun `main` op 17 okt 2026, commit 1cca996); in de
Pluginbouwer staan ze in `plugin/patches/` en is CI ermee groen.

**Zelf doen (±3 minuten):** open https://github.com/free-audio/clap-wrapper/issues/new, plak de titel en de tekst van
issue 1 tot en met 4 (3 heeft geen patch). Een PR is optioneel: fork op GitHub, pas de patch toe (`git apply <patch>`), push, en
gebruik de tekst onder "Fix" als PR-beschrijving.

---

## Issue 1 — AUv2: kAudioUnitProperty_TailTime is never advertised (SupportsTail() returns false)

**What happens.** `auval -strict` on a clap-wrapper AUv2 reports:

```
VERIFYING PROPERTY: Tail Time
WARNING: Recommended Property is not supported
```

even when the CLAP plugin implements `clap.tail`. In `src/detail/auv2/auv2_base_classes.h`, `WrapAsAUV2::SupportsTail()`
returns `false`, so `ausdk::AUBase` never exposes `kAudioUnitProperty_TailTime`, although `WrapAsAUV2::GetTailTime()`
already forwards `clap_plugin_tail.get()` (and `tail_changed()` already sends `PropertyChanged(kAudioUnitProperty_TailTime)`).

**Why it matters.** Hosts use TailTime to keep processing after the input ends; for example Logic's "Bounce → Include
Audio Tail" needs it for delays and reverbs.

**Fix** (`0001-auv2-advertise-tail-time.patch`): return true when the plugin implements the tail extension.

```cpp
bool WrapAsAUV2::SupportsTail()
{
  return _plugin && _plugin->_ext._tail;
}
```

Tested on macOS 15 arm64 (GitHub Actions): auval no longer warns; an AudioToolbox host reads the plugin's tail.

---

## Issue 2 — Real-time violation on macOS: lazy `thread_local` allocation in `AlwaysAudioThread()`

**What happens.** `pluginval --strictness-level 10 --rtcheck enabled` on the AUv2 fails in "Open editor whilst
processing" (any CLAP plugin, also without a GUI):

```
Real-time violation: intercepted call to real-time unsafe function calloc in real-time context!
  wrap_calloc
   _malloc_type_calloc_outlined
    dyld::ThreadLocalVariables::instantiateVariable(...)
     _tlv_get_addr
      Clap::Plugin::AlwaysAudioThread() + 24
       free_audio::auv2_wrapper::WrapAsAUV2::Render(...)
```

`src/clap_proxy.cpp` keeps `static thread_local uint32_t g_main_thread_override / g_audio_thread_override`. In a
dynamically loaded image, dyld allocates TLV storage with `calloc` on the first access in each thread, i.e. on the
host's audio thread during the first render (same path in `WrapAsVST3::process`). The same happens in any host that
renders on a new thread.

**Fix** (`0002-thread-overrides-without-thread-local.patch`): keep the counters in `pthread_key_t` slots created at
load time (`pthread_getspecific`/`pthread_setspecific` do not allocate); Windows keeps `thread_local`. The patch adds a
small `ThreadCounter` class and makes `Raise` use it.

Tested on macOS 15 arm64: pluginval 10 with `--rtcheck enabled` passes for AU and VST3 (without the patch it fails in
"Open editor whilst processing", reproduced on three different plugins).

Found by: the Pluginbouwer project (https://github.com/CKrebbers/pluginbouwer), via independent build sessions that
each hit the same violation.

---

## Issue 3 — AUv2: `clap_host_params.request_flush()` is a no-op

**What happens.** In `src/detail/auv2/auv2_base_classes.h`, `WrapAsAUV2::param_request_flush()` has an empty body. A
CLAP plugin whose GUI changes a parameter while the host is not rendering (transport stopped, track not live) calls
`request_flush()` and expects the host to call `params.flush()` on the main thread. Under AUv2 nothing happens: the
value reaches the plugin and the host (`AUEventListenerNotify` / parameter change) only at the next `Render`.

**Why it matters.** With the transport stopped, knob moves in the plugin's own UI are not reported to the host (no
automation write, no undo entry) until audio runs again.

**Suggested fix.** When not processing, call the plugin's `params.flush()` on the main thread (e.g. from the same
idle timer that runs `on_main_thread`) and forward its output events as parameter changes, as `Render` already does.
No patch included; found by an independent build session (frontpanel phase) and confirmed in the v0.16.0 source.

(Checked and *not* an issue: `_gesturedParameters.push_back` in `Render` cannot allocate in practice, the vector is
reserved for 8192 entries in `process.cpp`.)

---

## Issue 4 — AUv2: data race between SetParameter and Render (`ProcessAdapter::addParameterEvent`)

**What happens.** `pluginval --strictness-level 10` on the AUv2 intermittently fails in "Parameter thread safety"
(roughly one run in three on GitHub's macOS 15 arm64 runners):

```
*** FAILED: VALIDATION CRASHED
  process + 344
   Clap::AUv2::ProcessAdapter::process(...)
    free_audio::auv2_wrapper::WrapAsAUV2::Render(...)
```

`WrapAsAUV2::SetParameter` → `ProcessAdapter::addParameterEvent` does `_eventindices.emplace_back` / `_events.emplace_back`
on the calling thread, without synchronisation. A host may call `AudioUnitSetParameter` from any thread while `Render`
runs, and `process()` sorts, reads and clears the same vectors on the audio thread.

**Fix** (`0003-auv2-setparameter-thread-safe.patch`): `addParameterEvent` appends to a separate `_pendingParams` vector
under the existing `ClapWrapper::detail::shared::SpinLock`; `process()` moves the pending events into `_events` at its
start, briefly under the same lock. The lock is never held while the plugin processes, so a `SetParameter` from inside
`Render` cannot deadlock. `_pendingParams` is reserved in `setupProcessing` (8192, like `_events`), so the audio thread
does not allocate.

Tested on macOS 15 arm64: pluginval 10 (AU, `--rtcheck enabled`) green on four consecutive runs with the patch.

Found by: an independent build session of the Pluginbouwer project (https://github.com/CKrebbers/pluginbouwer).
