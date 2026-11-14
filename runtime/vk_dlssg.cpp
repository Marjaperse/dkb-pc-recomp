// Optional Streamline bridge for DLSS Frame Generation. Build with --streamline-sdk to use NVIDIA's public C++
// headers; the normal build needs no SDK or NVIDIA libraries. The SDK's structures cross a DLL boundary, so use the
// official declarations rather than duplicating their ABI. No SDK headers or runtime binaries are redistributed.
#include "vk_dlssg.h"

// host.h includes the C-only guest CPU helpers; this is its logging declaration.
extern "C" void rt_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

static int tried, supported;

#if defined(_WIN32) && defined(DKB_HAVE_STREAMLINE)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cwchar>
#include <sl.h>
#include <sl_dlss_g.h>

namespace {
// If shutdown fails, keep the module loaded and forbid another init: unloading code still owned by SDK threads is
// unsafe. The OS reclaims it on process exit. A device recreation must not retry this failed runtime.
HMODULE retained_module;

const char *reason(sl::Result r) {
    switch (r) {
    case sl::Result::eErrorDriverOutOfDate: return "NVIDIA driver is too old";
    case sl::Result::eErrorOSOutOfDate: return "Windows version is too old";
    case sl::Result::eErrorOSDisabledHWS: return "Windows hardware-accelerated GPU scheduling is disabled";
    case sl::Result::eErrorNoSupportedAdapterFound:
    case sl::Result::eErrorAdapterNotSupported: return "the selected GPU does not support DLSS-G";
    case sl::Result::eErrorNoPlugins:
    case sl::Result::eErrorFeatureMissing:
    case sl::Result::eErrorFeatureFailedToLoad:
    case sl::Result::eErrorFeatureMissingDependency: return "DLSS-G plugins or their dependencies are missing";
    default: return "Streamline rejected the request";
    }
}

int probe(VkPhysicalDevice physical_device) {
    if (retained_module) {
        rt_log("dlssg: an earlier Streamline initialisation or shutdown failed; restart the application before retrying");
        return 0;
    }
    wchar_t path[32768];
    DWORD n = GetModuleFileNameW(nullptr, path, (DWORD)(sizeof path / sizeof *path));
    if (!n || n >= sizeof path / sizeof *path) {
        rt_log("dlssg: the executable's folder is unknown; no generated frames");
        return 0;
    }
    wchar_t *name = std::wcsrchr(path, L'\\');
    if (!name || (size_t)(name - path) + 1 + std::wcslen(L"sl.interposer.dll") >= sizeof path / sizeof *path) {
        rt_log("dlssg: the executable's folder cannot hold the Streamline path; no generated frames");
        return 0;
    }
    std::wcscpy(name + 1, L"sl.interposer.dll");
    // An absolute module path and a restricted dependency search: no working-directory or PATH plugin lookup.
    HMODULE module = LoadLibraryExW(path, nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module) {
        rt_log("dlssg: sl.interposer.dll next to dkb.exe could not load (Windows error %lu); no generated frames",
               (unsigned long)GetLastError());
        return 0;
    }
    auto init = reinterpret_cast<PFun_slInit *>(GetProcAddress(module, "slInit"));
    auto shutdown = reinterpret_cast<PFun_slShutdown *>(GetProcAddress(module, "slShutdown"));
    auto requirements = reinterpret_cast<PFun_slGetFeatureRequirements *>(GetProcAddress(module, "slGetFeatureRequirements"));
    auto is_supported = reinterpret_cast<PFun_slIsFeatureSupported *>(GetProcAddress(module, "slIsFeatureSupported"));
    if (!init || !shutdown || !requirements || !is_supported) {
        rt_log("dlssg: sl.interposer.dll lacks a required preflight export; no generated frames");
        FreeLibrary(module);
        return 0;
    }
    *name = 0;
    const wchar_t *paths[] = {path};
    const sl::Feature features[] = {sl::kFeatureDLSS_G, sl::kFeatureReflex, sl::kFeaturePCL};
    sl::Preferences preferences{};
    preferences.showConsole = false;
    preferences.pathsToPlugins = paths;
    preferences.numPathsToPlugins = 1;
    preferences.featuresToLoad = features;
    preferences.numFeaturesToLoad = (uint32_t)(sizeof features / sizeof *features);
    // This phase queries capabilities only. No automatic interception, downloaded plugins or OTA updates.
    preferences.flags = sl::PreferenceFlags::eUseManualHooking | sl::PreferenceFlags::eDisableCLStateTracking |
                        sl::PreferenceFlags::eUseFrameBasedResourceTagging;
    preferences.engine = sl::EngineType::eCustom;
    preferences.engineVersion = "DKB Recomp";
    preferences.renderAPI = sl::RenderAPI::eVulkan;
    sl::Result result = init(preferences, sl::kSDKVersion);
    if (result != sl::Result::eOk) {
        rt_log("dlssg: Streamline initialisation failed: %s (%d); no generated frames", reason(result), (int)result);
        // A failed init can have loaded plugins. Keep its code mapped: the SDK does not promise that calling
        // slShutdown after an unsuccessful slInit is safe, or that all worker threads have already exited.
        retained_module = module;
        return 0;
    }
    sl::FeatureRequirements needs{};
    result = requirements(sl::kFeatureDLSS_G, needs);
    int ok = 0;
    if (result != sl::Result::eOk) {
        rt_log("dlssg: feature requirements unavailable: %s (%d)", reason(result), (int)result);
    } else if (!(needs.flags & sl::FeatureRequirementFlags::eVulkanSupported)) {
        rt_log("dlssg: this Streamline runtime does not support DLSS-G on Vulkan");
    } else {
        sl::AdapterInfo adapter{};
        adapter.vkPhysicalDevice = physical_device;
        result = is_supported(sl::kFeatureDLSS_G, adapter);
        if (result != sl::Result::eOk) {
            rt_log("dlssg: %s (%d)", reason(result), (int)result);
        } else {
            ok = 1;
            rt_log("dlssg: adapter supported; runtime asks for %u instance extensions, %u device extensions, "
                   "%u graphics, %u compute and %u optical-flow queues", needs.vkNumInstanceExtensions,
                   needs.vkNumDeviceExtensions, needs.vkNumGraphicsQueuesRequired, needs.vkNumComputeQueuesRequired,
                   needs.vkNumOpticalFlowQueuesRequired);
        }
    }
    result = shutdown();
    if (result == sl::Result::eOk) FreeLibrary(module);
    else {
        rt_log("dlssg: Streamline shutdown failed (%d); module retained until exit", (int)result);
        retained_module = module;
        ok = 0;
    }
    return ok;
}
} // namespace
#endif

int vk_dlssg_probe(VkPhysicalDevice physical_device, unsigned vendor_id) {
    if (tried) return supported;
    tried = 1;
    if (!physical_device || vendor_id != 0x10de) {
        rt_log("dlssg: requires a selected NVIDIA Vulkan GPU; no generated frames");
        return 0;
    }
#if defined(_WIN32) && defined(DKB_HAVE_STREAMLINE)
    supported = probe(physical_device);
#elif defined(_WIN32)
    rt_log("dlssg: Streamline support was not built (use --streamline-sdk); no generated frames");
#else
    rt_log("dlssg: the Streamline bridge currently supports Windows only; no generated frames");
#endif
    if (supported)
        rt_log("dlssg: preflight passed; frame inputs and Vulkan presentation hooks are not connected yet; no generated frames");
    return supported;
}

void vk_dlssg_reset_probe(void) {
    tried = supported = 0;
}
