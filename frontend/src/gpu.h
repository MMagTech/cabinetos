// What this machine's graphics hardware can actually do, asked once at startup.
//
// WHY THIS EXISTS. The console has to run on two machines that differ in the
// one way that matters here: the A9 Max has a Radeon with Vulkan, and the test
// VM has no Vulkan at all — gamescope already rejects it for exactly that
// reason. A host that assumed Vulkan would not run on the machine this project
// is developed on, and a host that assumes GLES cannot play PlayStation 2.
//
// So capability is DISCOVERED, never assumed, which is the rule *Hardware*
// already states and what docs/PROJECT.md open question 20 asks for. This is
// the one place that goes and looks.
//
// WHAT IT IS FOR, IN PRODUCT TERMS. Three of the twenty-one cores draw with
// hardware and take whatever the host offers. The two heavy systems do not
// have that luxury: PCSX2's graphics synthesiser refuses an OpenGL ES context
// outright — measured on the A9, `OpenGL is not supported. Only OpenGL 3.2 was
// found` — and Dolphin renders from a thread of its own, which GLES cannot
// serve because a GL context belongs to one thread at a time. Vulkan has no
// current context and no thread affinity, which is not a workaround for those
// two faults, it is the absence of the thing that causes them.
//
// NOTHING HERE LINKS THE VULKAN LOADER. It is opened with dlopen at runtime, so
// this binary still starts on a machine with no Vulkan and says so, rather than
// failing to load with a missing library. That also means `ldd` cannot see it —
// the same blind spot `net.cpp`'s nmcli has — so `build_files/build.sh` asserts
// libvulkan.so.1 is in the image the way it already asserts nmcli and pkcheck.

#pragma once

#include <cstdint>
#include <string>

namespace cab::gpu {

// What a Vulkan probe found. `available` is the only field worth branching on;
// the rest is for the report, and for saying WHY when the answer is no.
struct VulkanCaps {
    bool available = false;
    // Why not, when not. One line, in words that name the thing to change:
    // "no libvulkan.so.1 on this machine" is a different problem from "a
    // Vulkan driver is present and reports no devices", and only the second
    // means the GPU.
    std::string reason;

    std::string deviceName;      // "AMD Radeon 890M Graphics (RADV STRIX1)"
    uint32_t apiVersion = 0;     // VK_MAKE_API_VERSION packed
    uint32_t driverVersion = 0;
    bool discrete = false;

    // THE INTEROP EXTENSIONS, AND WHY THEY DECIDE THE WHOLE SHAPE.
    //
    // The UI is drawn in OpenGL ES and every screen that exists was looked at
    // on a television through it. A core rendering with Vulkan produces a
    // VkImage, and something has to get that picture into the texture the UI
    // already knows how to draw.
    //
    // The Linux answer is a dmabuf: Vulkan exports the image as a file
    // descriptor and EGL imports the same memory as a texture, with no copy
    // and no readback. Measured present on the A9 —
    // EGL_EXT_image_dma_buf_import, EGL_EXT_image_dma_buf_import_modifiers and
    // EGL_MESA_image_dma_buf_export are all there on radeonsi.
    //
    // Without these three a Vulkan core could run and could never be SEEN, so
    // they are part of "is Vulkan available", not a detail below it.
    bool externalMemoryFd = false;      // VK_KHR_external_memory_fd
    bool externalMemoryDmaBuf = false;  // VK_EXT_external_memory_dma_buf
    bool drmFormatModifier = false;     // VK_EXT_image_drm_format_modifier
};

// Probed once, on first call, and cached. Safe to call before SDL is up: it
// opens no window and touches no display.
const VulkanCaps& vulkan();

// Human-readable, for `--gpu-probe`. Prints what was found and, when Vulkan is
// not usable, the one line that says which part is missing.
//
// A PROBE THAT PRINTS ONE NUMBER WITHOUT SAYING WHAT IT ASKED IS THE THIRD
// LYING INSTRUMENT THIS PROJECT HAS HAD TO FIX. So this says what it looked
// for as well as what it found.
void report();

// Version numbers as "1.4.341", for the report.
std::string versionString(uint32_t packed);

// The loader's one entry point, or null when Vulkan is not usable here.
//
// Shared rather than opened twice: dlopen'ing an ICD a second time is a way to
// end up with two copies of a driver's thread-local state in one process, and
// the whole point of probing once is that there is a single answer in this
// process to what the hardware can do. `vkhost.cpp` builds everything else out
// of this pointer.
void* instanceProcAddr();

}  // namespace cab::gpu
