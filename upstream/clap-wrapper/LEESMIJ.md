# Twee fouten in clap-wrapper v0.16.0, klaar om te melden

Deze sessie mag in free-audio/clap-wrapper geen issue openen en niet forken (alleen eigen repos). Alles hieronder is
klaar om te plakken. Beide patches gaan schoon op v0.16.0 (= hun `main` op 17 okt 2026, commit 1cca996); in de
Pluginbouwer staan ze in `plugin/patches/` en is CI ermee groen.

**Zelf doen (±3 minuten):** open https://github.com/free-audio/clap-wrapper/issues/new, plak de titel en de tekst van
issue 1, daarna van issue 2. Een PR is optioneel: fork op GitHub, pas de patch toe (`git apply <patch>`), push, en
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
