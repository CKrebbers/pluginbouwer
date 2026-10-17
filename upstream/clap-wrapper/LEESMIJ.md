# Zeven punten in clap-wrapper v0.16.0 (en één in clap-validator), klaar om te melden

Deze sessie mag in free-audio/clap-wrapper geen issue openen en niet forken (alleen eigen repos). Alles hieronder is
klaar om te plakken. De patches 0001–0004 gaan elk los schoon op v0.16.0, 0005 na 0004, 0006 na 0004 en 0005 (= hun `main` op 17 okt 2026, commit 1cca996); in de
Pluginbouwer staan ze in `plugin/patches/` en is CI ermee groen.

**Zelf doen (±3 minuten):** open https://github.com/free-audio/clap-wrapper/issues/new, plak de titel en de tekst van
issue 1 tot en met 7 (3 heeft geen patch). Het punt voor clap-validator staat onderaan en gaat
naar https://github.com/free-audio/clap-validator/issues/new. Een PR is optioneel: fork op GitHub, pas de patch toe (`git apply <patch>`), push, en
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

---

## Issue 5 — AUv2: parameters set before `Initialize()` never reach the CLAP

**What happens.** A host that sets parameters on an uninitialized AU (`AudioUnitSetParameter` before
`AudioUnitInitialize`, which Logic and AudioToolbox-based hosts do when restoring a project or when the user tweaks a
plugin on a track that has not rendered yet) only changes the value in the `AUBase` element. There is no
`ProcessAdapter` yet, so `WrapAsAUV2::SetParameter` cannot queue a CLAP event; after `Initialize` the CLAP still has
its own defaults while the host shows the new values. A null test between CLAP and AU (same input, same parameter
values, set before activation) differs.

**Fix** (`0004-auv2-parameters-set-before-initialize.patch`): remember `(clap_id, value)` pairs set before
`Initialize` in `_paramsBeforeInit` and send them to the CLAP through `params.flush()` in `activateCLAP()`, before the
first render.

Tested on macOS 15 arm64: CLAP and AU renders of the same input with the same parameters (set before `Initialize`) are
bit-identical with the patch; without it they differ.

---

## Issue 6 — AUv2: factory presets from the CLAP preset-discovery factory (feature)

**What happens.** A CLAP that ships presets through `clap.preset-discovery-factory` (location kind
`CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN`, one load key per preset) and implements `clap.preset-load` shows no factory
presets when wrapped as AUv2: `GetPresets` is not overridden, so Logic's preset menu stays empty.

**Proposal** (`0005-auv2-factory-presets-from-preset-discovery.patch`, applies after 0004): after the instance is
created, walk the CLAP's preset-discovery factory (all providers, locations of kind `PLUGIN`, presets for this plugin
id) and expose them through `GetPresets`; `NewFactoryPresetSet` loads the chosen one via `preset-load` on the main
thread, sets it as the current preset and keeps its name for `SaveState`/ClassInfo. A preset overrides values the host
set before `Initialize` (from 0004). Nothing in the patch is plugin-specific.

Tested on macOS 15 arm64: `auval -strict` lists the presets ("HAS FACTORY PRESETS"); an AudioToolbox host reads
`kAudioUnitProperty_FactoryPresets`, sets `kAudioUnitProperty_PresentPreset` before and after `Initialize`, and every
parameter and the ClassInfo match the preset; an unknown preset number returns an error. Not tested in Logic itself.
Known gap: VST3 gets no program list from the same presets.
Related: `auval` warns "Preset name is not retained in retrieved class data" because `SaveState` writes a fixed
name ("Program"); one session fixed it by keeping the current preset name (factory or `NewCustomPresetSet`) in
ClassInfo and restoring it in `RestoreState`.

Found by: an independent build session of the Pluginbouwer project (preset phase).

---

## Issue 7 — AUv2: only the fixed port layout is offered (no mono, no mono → stereo)

**What happens.** A CLAP that implements `clap.audio-ports-config` (stereo 2→2, mono 1→1, mono→stereo 1→2) is offered
by the AUv2 wrapper only in its current layout: `SupportedNumChannels` and `ValidFormat` use the port cache taken at
construction, so Logic offers the AU on a stereo track only. The VST3 side already supports this through
`clap.configurable-audio-ports` (`setBusArrangements`).

**Proposal** (`0006-auv2-channel-layouts-from-audio-ports-config.patch`, applies after 0004 and 0005): snapshot the
`audio-ports-config` layouts with one main input and one main output at `PostConstructor`; `SupportedNumChannels`
lists them, `ValidFormat` accepts their channel counts on element 0, and `Initialize` selects the matching config
(`clap_plugin_audio_ports_config.select`) before activating the CLAP, or returns `kAudioUnitErr_FormatNotSupported`.
Without `audio-ports-config` nothing changes.

Tested on macOS 15 arm64: `auval -strict` reports channel capabilities `[2, 2] [1, 1] [1, 2]` and passes; pluginval 10
passes on the AU. Mono renders were compared bit for bit through the CLAP (same process code), not through the AU.
Needed independently by two build sessions.

---

## clap-validator 0.4.1 — `begin_preset()` deadlocks on the second preset of a container

Paste at https://github.com/free-audio/clap-validator/issues/new (patch: `../clap-validator-0001-begin-preset-deadlock.patch`).

**What happens.** `preset-discovery-crawl` and `preset-discovery-load` time out ("Timed out after 45 seconds") for any
plugin whose preset provider declares a container with more than one preset (`begin_preset` with a load key, called
twice for the same location).

In `src/plugin/preset_discovery/metadata_receiver.rs`, `MetadataReceiver::begin_preset` takes
`let result = this.result.lock().unwrap();` for its checks and keeps the guard alive. For a container preset it then
calls `this.flush_preset()`, which does `self.result.lock()` again. `std::sync::Mutex` is not reentrant, so from the
second preset on the thread waits on itself (stack: `lock_contended ← flush_preset ← begin_preset ←
providerMetadata`). On the first preset `flush_preset` returns early (nothing to flush), which is why single-preset
containers pass. Same code on master (b2f1d9b).

**Fix**: drop the guard after the checks (`drop(result);`), before `flush_preset()`. The patch compiles (`cargo check`,
rustc 1.97); it has not yet been run against a plugin with a multi-preset container.

Workaround used meanwhile: one provider per preset, each a container of one (valid CLAP, but unusual for hosts).
Found independently by three build sessions of the Pluginbouwer project (each hit the 45 s timeout with a container of
five presets). Side effect of the workaround: `preset-discovery-load` only keeps the result of the last provider.
