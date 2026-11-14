// A deliberately small Streamline DLL fixture, compiled against the user's external SDK headers. No NVIDIA code or
// binaries are copied here. Validates preferences, adapter identity and shutdown ordering in the production bridge.
#define SL_INTERPOSER
#include <vulkan/vulkan.h>
#include <sl.h>
#include <sl_dlss_g.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static bool is_case(const char *name) {
    const char *v = std::getenv("DKB_TEST_DLSSG_CASE");
    return v && !std::strcmp(v, name);
}

static void event(const char *name) {
    if (const char *path = std::getenv("DKB_TEST_DLSSG_EVENTS")) {
        if (FILE *f = std::fopen(path, "ab")) {
            std::fprintf(f, "%s\n", name);
            std::fclose(f);
        }
    }
}

sl::Result slInit(const sl::Preferences &p, uint64_t version) {
    event("init");
    bool fg = false, reflex = false, pcl = false;
    for (unsigned i = 0; i < p.numFeaturesToLoad; i++) {
        fg |= p.featuresToLoad[i] == sl::kFeatureDLSS_G;
        reflex |= p.featuresToLoad[i] == sl::kFeatureReflex;
        pcl |= p.featuresToLoad[i] == sl::kFeaturePCL;
    }
    if (!fg || !reflex || !pcl || version != sl::kSDKVersion || p.renderAPI != sl::RenderAPI::eVulkan ||
        !(p.flags & sl::PreferenceFlags::eUseManualHooking) || (p.flags & sl::PreferenceFlags::eAllowOTA) ||
        (p.flags & sl::PreferenceFlags::eLoadDownloadedPlugins) || p.numPathsToPlugins != 1 || !p.pathsToPlugins[0]) {
        event("bad preferences");
        return sl::Result::eErrorInvalidIntegration;
    }
    return is_case("init") ? sl::Result::eErrorNoPlugins : sl::Result::eOk;
}

sl::Result slShutdown() {
    event("shutdown");
    return is_case("shutdown") ? sl::Result::eErrorInvalidState : sl::Result::eOk;
}

sl::Result slGetFeatureRequirements(sl::Feature f, sl::FeatureRequirements &r) {
    event("requirements");
    if (f != sl::kFeatureDLSS_G) return sl::Result::eErrorInvalidParameter;
    if (is_case("requirements")) return sl::Result::eErrorFeatureFailedToLoad;
    r.flags = is_case("no-vulkan") ? sl::FeatureRequirementFlags{} : sl::FeatureRequirementFlags::eVulkanSupported;
    r.vkNumGraphicsQueuesRequired = 1;
    r.vkNumComputeQueuesRequired = 2;
    return sl::Result::eOk;
}

#ifndef DKB_TEST_NO_EXPORT
sl::Result slIsFeatureSupported(sl::Feature f, const sl::AdapterInfo &adapter) {
    event("support");
    if (f != sl::kFeatureDLSS_G || adapter.vkPhysicalDevice != (void *)(uintptr_t)0x1234)
        return sl::Result::eErrorInvalidParameter;
    if (is_case("driver")) return sl::Result::eErrorDriverOutOfDate;
    if (is_case("hws")) return sl::Result::eErrorOSDisabledHWS;
    if (is_case("adapter")) return sl::Result::eErrorAdapterNotSupported;
    return sl::Result::eOk;
}
#endif
