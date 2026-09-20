#include "gpu.h"

#include <dlfcn.h>

#include <cstdio>
#include <cstring>
#include <vector>

// VK_NO_PROTOTYPES because nothing here links the loader — every entry point
// is resolved through vkGetInstanceProcAddr after dlopen. See gpu.h.
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

namespace cab::gpu {
namespace {

VulkanCaps gCaps;
bool gProbed = false;

// The handle is deliberately NOT closed. The core host needs the same loader a
// moment later, and dlclose on a Vulkan ICD is a well-known way to lose a
// process — drivers register atexit handlers and thread-local state that does
// not survive being unmapped. It costs one mapping for the life of the
// process, which a console can afford.
void* gLoader = nullptr;
PFN_vkGetInstanceProcAddr gGetInstanceProcAddr = nullptr;

bool has(const std::vector<VkExtensionProperties>& list, const char* name) {
    for (const auto& e : list)
        if (std::strcmp(e.extensionName, name) == 0) return true;
    return false;
}

void probe() {
    gProbed = true;

    gLoader = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!gLoader) {
        gCaps.reason = "no libvulkan.so.1 on this machine";
        return;
    }
    gGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        dlsym(gLoader, "vkGetInstanceProcAddr"));
    if (!gGetInstanceProcAddr) {
        gCaps.reason = "libvulkan.so.1 has no vkGetInstanceProcAddr";
        return;
    }

    auto global = [&](const char* name) {
        return gGetInstanceProcAddr(nullptr, name);
    };
    auto createInstance = reinterpret_cast<PFN_vkCreateInstance>(global("vkCreateInstance"));
    if (!createInstance) {
        gCaps.reason = "the Vulkan loader would not hand over vkCreateInstance";
        return;
    }

    // Apply 1.1 rather than 1.0. Everything the interop path needs is core in
    // 1.1, and asking for it here is how a driver that only does 1.0 says no
    // now rather than half way through creating an image.
    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "CabinetOS";
    app.apiVersion = VK_API_VERSION_1_1;

    VkInstanceCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &app;

    VkInstance instance = VK_NULL_HANDLE;
    if (createInstance(&ici, nullptr, &instance) != VK_SUCCESS) {
        gCaps.reason = "a Vulkan loader is present and would not create an instance";
        return;
    }

    auto inst = [&](const char* name) { return gGetInstanceProcAddr(instance, name); };
    auto enumerate = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(
        inst("vkEnumeratePhysicalDevices"));
    auto getProps = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(
        inst("vkGetPhysicalDeviceProperties"));
    auto devExts = reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(
        inst("vkEnumerateDeviceExtensionProperties"));
    auto destroyInstance = reinterpret_cast<PFN_vkDestroyInstance>(inst("vkDestroyInstance"));

    if (!enumerate || !getProps || !devExts) {
        gCaps.reason = "the Vulkan instance is missing entry points it must have";
        if (destroyInstance) destroyInstance(instance, nullptr);
        return;
    }

    uint32_t count = 0;
    enumerate(instance, &count, nullptr);
    if (count == 0) {
        // THIS IS THE TEST VM'S ANSWER, and it is the honest one: a Vulkan
        // driver stack is installed and there is no device behind it. Saying
        // "no Vulkan" without saying which half is missing is what sent this
        // project looking at a GPU that was working fine.
        gCaps.reason = "a Vulkan driver is present and reports no devices";
        destroyInstance(instance, nullptr);
        return;
    }
    std::vector<VkPhysicalDevice> devices(count);
    enumerate(instance, &count, devices.data());

    // Prefer a discrete card, then anything that is not software. llvmpipe
    // enumerates as CPU and running a PlayStation 2 on it would be worse than
    // saying no — it is the same trap as cage's software rendering, where the
    // machine looks like it is working and every judgement made on it is
    // worthless.
    VkPhysicalDevice chosen = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties chosenProps{};
    for (VkPhysicalDevice d : devices) {
        VkPhysicalDeviceProperties p{};
        getProps(d, &p);
        if (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU) continue;
        const bool better =
            chosen == VK_NULL_HANDLE ||
            (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU &&
             chosenProps.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU);
        if (better) {
            chosen = d;
            chosenProps = p;
        }
    }
    if (chosen == VK_NULL_HANDLE) {
        gCaps.reason = "the only Vulkan device here is a software one";
        destroyInstance(instance, nullptr);
        return;
    }

    uint32_t extCount = 0;
    devExts(chosen, nullptr, &extCount, nullptr);
    std::vector<VkExtensionProperties> exts(extCount);
    if (extCount) devExts(chosen, nullptr, &extCount, exts.data());

    gCaps.deviceName = chosenProps.deviceName;
    gCaps.apiVersion = chosenProps.apiVersion;
    gCaps.driverVersion = chosenProps.driverVersion;
    gCaps.discrete = chosenProps.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
    gCaps.externalMemoryFd = has(exts, VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME);
    gCaps.externalMemoryDmaBuf = has(exts, VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME);
    gCaps.drmFormatModifier = has(exts, VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME);

    // A device with no way to hand a picture to EGL can run a core and can
    // never show one, so it does not count as available. See gpu.h.
    if (!gCaps.externalMemoryFd || !gCaps.externalMemoryDmaBuf) {
        gCaps.reason =
            "this Vulkan device cannot export an image as a dmabuf, so nothing "
            "it drew could reach the screen";
    } else {
        gCaps.available = true;
    }

    destroyInstance(instance, nullptr);
}

}  // namespace

const VulkanCaps& vulkan() {
    if (!gProbed) probe();
    return gCaps;
}

void* instanceProcAddr() {
    if (!gProbed) probe();
    return reinterpret_cast<void*>(gGetInstanceProcAddr);
}

std::string versionString(uint32_t packed) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%u.%u.%u", VK_API_VERSION_MAJOR(packed),
                  VK_API_VERSION_MINOR(packed), VK_API_VERSION_PATCH(packed));
    return buf;
}

void report() {
    const VulkanCaps& v = vulkan();
    std::printf("Vulkan\n");
    std::printf("  looked for  libvulkan.so.1, a non-software device, and the two\n");
    std::printf("              extensions that let a rendered image reach EGL\n");
    if (!v.available) {
        std::printf("  NOT USABLE  %s\n", v.reason.c_str());
        if (!v.deviceName.empty()) {
            std::printf("  device      %s\n", v.deviceName.c_str());
            std::printf("  external memory fd      %s\n", v.externalMemoryFd ? "yes" : "NO");
            std::printf("  external memory dma_buf %s\n", v.externalMemoryDmaBuf ? "yes" : "NO");
        }
        std::printf("\n  The console runs on OpenGL ES here, which plays every core it\n");
        std::printf("  ships. PlayStation 2 and GameCube need the Vulkan path.\n");
        return;
    }
    std::printf("  USABLE\n");
    std::printf("  device      %s%s\n", v.deviceName.c_str(), v.discrete ? " (discrete)" : "");
    std::printf("  api         %s\n", versionString(v.apiVersion).c_str());
    std::printf("  external memory fd      yes\n");
    std::printf("  external memory dma_buf yes\n");
    std::printf("  drm format modifier     %s\n", v.drmFormatModifier ? "yes" : "no");
}

}  // namespace cab::gpu
