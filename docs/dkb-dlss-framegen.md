# DLSS Frame Generation development

Branch: `feature/dlss-frame-generation`, based on upstream `bananza` at `f7f0ca9`.

**Preflight is implemented. DLSS-G does not generate or present frames yet.** `frame_gen = dlss` checks whether the
optional Streamline runtime reports support for the selected Vulkan adapter, logs why it cannot run or what it requires,
and presents the game's original frame. The existing `frame_gen = 2x` block-matching implementation remains separate.
The default is still `off`. Selecting `dlss` never silently selects the block-matching implementation.

## Build and try preflight

The ordinary build compiles a fallback bridge without NVIDIA SDK headers, import libraries or DLLs. For the Windows
development build, obtain NVIDIA's [Streamline SDK](https://github.com/NVIDIA-RTX/Streamline) separately, and add:

```powershell
python tools/build_game.py --version dkb458752 --expected tools/data/expected-dkb458752.json --streamline-sdk H:/SDKs/Streamline
```

Use the repository's usual Python environment and toolchain options. `--streamline-sdk` names the directory containing
`include/sl.h` and `include/sl_dlss_g.h`. When using a prebuilt runtime, supply the option while building that runtime
with `--make-runtime-lib`; passing it with `--runtime-lib` is rejected because it cannot change compiled library code.
Only `runtime/vk_dlssg.cpp` receives the SDK include path and enabling build define. No SDK files are packaged.

Place the matching Streamline runtime files next to `dkb.exe`, following NVIDIA's SDK distribution instructions. The
requested features are DLSS-G, Reflex and PCL: typical runtime dependencies include `sl.interposer.dll`, `sl.common.dll`,
`sl.dlss_g.dll`, `sl.reflex.dll`, `sl.pcl.dll`, `nvngx_dlssg.dll` and `NvLowLatencyVk.dll`. The SDK/runtime's support query
decides GPU, OS, driver and hardware-scheduling support; an NVIDIA vendor ID alone never establishes DLSS-G support.
No NVIDIA runtime files are shipped by this change. DLSS Super Resolution's `nvngx_dlss.dll` is a separate component.

Then set this in the executable's `dkb.ini`:

```ini
[display]
frame_gen = dlss
```

`TOTK_FRAME_GEN=dlss` overrides the file. Preflight runs on the present thread once per device lifetime and writes
`dlssg:` diagnostics. Switching from `2x` releases its history images. Switching back uses the existing implementation.
The upscaler setting is independent. A successful preflight explicitly logs that presentation hooks and frame inputs
are not connected and produces no extra frames. There is no menu row advertising DLSS-G as a completed feature.

## Implemented boundary

- `runtime/vk_dlssg.h` exposes a C interface; SDK structures remain inside the C++ bridge.
- `runtime/vk_dlssg.cpp` loads `sl.interposer.dll` by absolute executable-relative path. Dependencies use the module
  directory and Windows system directory. It resolves all preflight exports before initialisation.
- Preferences select Vulkan and manual hooking, disable OTA/downloaded plugin use, and load the FG dependencies.
  The bridge does not install hooks or supply the existing device to Streamline during this capability-only phase.
- It checks Vulkan support, queries the exact selected physical adapter, and reports required extension and queue
  counts. These requirements are reported only; they are not yet enabled on the runtime's device.
- Successful initialisation always gets a shutdown attempt, including failed capability queries. Failed shutdown or
  failed initialisation retains the module until process exit and prevents reinitialisation. A plain fallback build
  and a non-NVIDIA adapter never load Streamline.
- `runtime/vk_framegen.c` serialises preflight with its existing lock. The original present always proceeds because
  the DLSS-G path returns no generated frame. No NGX Super Resolution state or jitter changes are made.

## Next implementation milestones

1. **Vulkan startup and hooking.** Move the long-lived Streamline initialisation before Vulkan instance creation;
   query and merge required instance/device extensions, Vulkan 1.2/1.3 features and queue counts. Bind the exact
   device/queues and connect manual hooks as NVIDIA documents. Optional DLSS-G failure must leave a usable ordinary
   Vulkan renderer. Preserve coexistence with the existing direct-NGX Super Resolution implementation.
2. **Frame inputs and lifetime.** Capture scene depth, camera data, jitter and motion vectors into owned resources
   associated with the exact `PresentJob`. The current SR path reads transient scene depth and rewrites one motion
   image during replay; passing those raw handles to the asynchronous present thread would race reuse. Retire copies
   only after Streamline's GPU input-completion synchronization. Reset on camera cuts, missing inputs, native-size
   frames, resolution/crop changes, loading, resize/minimize and backend changes.
3. **Capture beyond the scaler dispatch.** `runtime/vk_dlss.c` currently makes camera motion at the game's upscaler
   dispatch only. Bananza skips that dispatch at native scene resolution, including `scene_res = full`. DLSS-G needs
   a scene-capture point that also runs then. Camera-only motion remains an experimental quality limitation for
   characters, debris and other independently moving objects; dense object motion needs separate work.
4. **Tagging and presentation.** Supply constants, frame tokens and correctly scoped depth/motion/backbuffer tags;
   identify HUD-less scene colour and UI handling. Implement DLSS-G enable/disable, Reflex markers and presentation
   through Streamline. The existing `vk_fg_show` owns two ordinary presents, whereas Streamline owns generated-frame
   presentation: avoid running both pacing mechanisms on the same frame.
5. **Hardware validation.** Verify supported and unsupported adapters, missing runtime components, DLSS SR on/off,
   native scene resolution, resolution changes, resize/alt-tab, camera cuts, HUD stability, validation errors and
   shutdown. Measure real and displayed frame rates and latency separately before exposing a player-facing option.

## Validation

The complete Windows runtime archive built successfully with the optional SDK bridge enabled and disabled (125 runtime objects,
LLVM-MinGW Clang 23 and the existing installer dependency set). No NVIDIA hardware or game session has validated this
bridge. Compile/lifecycle tests use a fake Streamline DLL
compiled against the external official SDK declarations, including the actual DLL ABI boundary:

```powershell
python tests/dlssg_build_test.py
python tests/dlssg_preflight_test.py --cc <clang.exe> --deps <Vulkan-include-dir> --streamline-sdk <Streamline-root>
python tests/fg_pace_test.py --cc <clang.exe>
python tests/cfg_settings_test.py
```

The preflight fixture covers missing runtime/exports, non-NVIDIA/null adapters, initialisation failure, missing Vulkan
support, failed requirements, driver/scheduling/adapter rejection, successful support, and failed shutdown. It checks
once-per-device logging, preference flags, the exact adapter passed across the DLL boundary and cleanup ordering at
`-O0` and `-O2`. Without `--streamline-sdk` it still exercises the ordinary fallback build. Headers were compile-checked
against NVIDIA Streamline commit `625db07d4bca5abc67d781c42af7e7fb0761f1b6`; headers stay outside tracked sources.

## Integration references

- NVIDIA [DLSS-G programming guide](https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideDLSS_G.md):
  required buffers, input lifetimes, constants and feature support.
- NVIDIA [manual hooking guide](https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideManualHooking.md):
  Vulkan startup/device binding and presentation interception requirements.
- [Existing frame generation](dkb-framegen.md) and [Super Resolution inputs](dkb-dlss.md).

The bridge is new project code using external SDK declarations; no reference implementation was copied.
