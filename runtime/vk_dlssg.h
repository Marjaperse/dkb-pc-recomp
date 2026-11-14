// DLSS Frame Generation preflight. The presenter serialises these calls with its frame-generation lock.
// This checks the SDK/runtime/adapter only; success does not mean a frame was generated. Streamline Vulkan hooks,
// per-frame input ownership and resource tagging must be connected before enabling interpolation.
#pragma once
#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

// 1: the runtime reports DLSS-G support, 0: unavailable. Tries once per device lifetime, logs the reason once.
// Does not record commands, hook Vulkan, or change the selected upscaler. NVIDIA DLLs are loaded only on NVIDIA.
int vk_dlssg_probe(VkPhysicalDevice physical_device, unsigned vendor_id);
void vk_dlssg_reset_probe(void);

#ifdef __cplusplus
}
#endif
