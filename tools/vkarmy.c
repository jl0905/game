// vkarmy (V157, RENDERER.md phase 2, milestones 2+3): a real Vulkan
// GRAPHICS PIPELINE — SPIR-V shaders, depth buffer, and an INSTANCED ARMY:
// 2001 boxes (a ground slab + 2000 marching soldiers in two team colours,
// Lambert-lit) at uncapped speed, per-frame instance upload, exactly the
// transform-list workload the game's V126/V128 batcher produces. This is
// the core of the game's battle renderer running on Vulkan.
// V158 adds the BATTLEFIELD: a hills heightfield terrain mesh (same
// smoothstep-hill construction as the game's Terrain) with slope/height
// colouring and smooth normals, plus scattered trees - the whole battle
// scene composition, all Vulkan.
// V159 retires the LAST unknown: TEXT AND 2D on Vulkan - a GDI-rasterised
// font atlas (no font files), a descriptor-sampled alpha-blended overlay
// pipeline, and a live HUD (title + fps readout + panel) over the battle.
//
//   build/vkarmy.exe [frames]   (default 600; prints avg ms + fps)
#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_WIN32_KHR
#include <windows.h>
#include <vulkan/vulkan.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---- tiny matrix kit (column-major, Vulkan clip space) ----
typedef struct { float m[16]; } M4;
static M4 m4mul(M4 a, M4 b) {
    M4 r;
    for (int c = 0; c < 4; ++c)
        for (int rr = 0; rr < 4; ++rr) {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += a.m[k * 4 + rr] * b.m[c * 4 + k];
            r.m[c * 4 + rr] = s;
        }
    return r;
}
static M4 m4persp(float fovy, float aspect, float zn, float zf) {
    const float f = 1.0f / tanf(fovy * 0.5f);
    M4 r = { 0 };
    r.m[0] = f / aspect;
    r.m[5] = -f;                       // Vulkan: y flips
    r.m[10] = zf / (zn - zf);
    r.m[11] = -1.0f;
    r.m[14] = (zn * zf) / (zn - zf);
    return r;
}
static M4 m4look(float ex, float ey, float ez, float cx, float cy, float cz) {
    float fx = cx - ex, fy = cy - ey, fz = cz - ez;
    float fl = sqrtf(fx * fx + fy * fy + fz * fz);
    fx /= fl; fy /= fl; fz /= fl;
    float sx = fy * 0.0f - fz * 1.0f, sy = fz * 0.0f - fx * 0.0f,
          sz = fx * 1.0f - fy * 0.0f;              // f x up(0,1,0)
    float sl = sqrtf(sx * sx + sy * sy + sz * sz);
    sx /= sl; sy /= sl; sz /= sl;
    float ux = sy * fz - sz * fy, uy = sz * fx - sx * fz, uz = sx * fy - sy * fx;
    M4 r = { 0 };
    r.m[0] = sx; r.m[4] = sy; r.m[8] = sz;
    r.m[1] = ux; r.m[5] = uy; r.m[9] = uz;
    r.m[2] = -fx; r.m[6] = -fy; r.m[10] = -fz;
    r.m[12] = -(sx * ex + sy * ey + sz * ez);
    r.m[13] = -(ux * ex + uy * ey + uz * ez);
    r.m[14] = fx * ex + fy * ey + fz * ez;
    r.m[15] = 1.0f;
    return r;
}

typedef struct { float row0[4], row1[4], row2[4], row3[4], color[4]; } Inst;

// ---- the battlefield heightfield (V158): the game's hill construction ----
#define NHILLS 7
static float g_hx[NHILLS], g_hz[NHILLS], g_hr[NHILLS], g_hh[NHILLS];
static unsigned g_seed = 20260726u;
static float frand(void) {
    g_seed ^= g_seed << 13; g_seed ^= g_seed >> 17; g_seed ^= g_seed << 5;
    return (g_seed & 0xFFFFFF) / (float)0xFFFFFF;
}
static float smoothstepf(float x) { return x * x * (3.0f - 2.0f * x); }
static float heightAt(float x, float z) {
    float h = 0.0f;
    for (int i = 0; i < NHILLS; ++i) {
        const float dx = x - g_hx[i], dz = z - g_hz[i];
        const float d = sqrtf(dx * dx + dz * dz);
        if (d < g_hr[i]) h += g_hh[i] * smoothstepf(1.0f - d / g_hr[i]);
    }
    return h;
}
typedef struct { float pos[3], nrm[3], col[4]; } MeshVert;
typedef struct { float pos[2], uv[2], col[4]; } UiVert;

// GDI font atlas (V159): ASCII 32..126 rasterised once by Windows itself.
enum { ATLAS_W = 512, ATLAS_H = 256, GLYPH_W = 16, GLYPH_H = 28, GLYPH_COLS = 32 };
static unsigned char g_atlas[ATLAS_W * ATLAS_H];
static void BuildAtlas(void) {
    BITMAPINFO bmi = { 0 };
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = ATLAS_W;
    bmi.bmiHeader.biHeight = -ATLAS_H;   // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HDC dc = CreateCompatibleDC(NULL);
    HBITMAP bmp = CreateDIBSection(dc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    SelectObject(dc, bmp);
    HFONT font = CreateFontA(24, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, ANSI_CHARSET,
                             OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                             FF_DONTCARE, "Consolas");
    SelectObject(dc, font);
    SetBkColor(dc, RGB(0, 0, 0));
    SetTextColor(dc, RGB(255, 255, 255));
    RECT full = { 0, 0, ATLAS_W, ATLAS_H };
    FillRect(dc, &full, (HBRUSH)GetStockObject(BLACK_BRUSH));
    for (int c = 32; c < 127; ++c) {
        const int slot = c - 32;
        const int gx = (slot % GLYPH_COLS) * GLYPH_W;
        const int gy = (slot / GLYPH_COLS) * GLYPH_H;
        char ch = (char)c;
        TextOutA(dc, gx, gy, &ch, 1);
    }
    GdiFlush();
    const unsigned* px = (const unsigned*)bits;
    for (int i = 0; i < ATLAS_W * ATLAS_H; ++i)
        g_atlas[i] = (unsigned char)(px[i] & 0xFF);   // blue channel = coverage
    DeleteObject(font);
    DeleteObject(bmp);
    DeleteDC(dc);
}

// quad batcher into a mapped vertex buffer
static UiVert* g_uiCur;
static int g_uiCount;
static void UiQuad(float x, float y, float w, float h, float u0, float v0,
                   float u1, float v1, float r, float g, float b, float a) {
    UiVert q[6] = {
        { { x, y },         { u0, v0 }, { r, g, b, a } },
        { { x, y + h },     { u0, v1 }, { r, g, b, a } },
        { { x + w, y },     { u1, v0 }, { r, g, b, a } },
        { { x + w, y },     { u1, v0 }, { r, g, b, a } },
        { { x, y + h },     { u0, v1 }, { r, g, b, a } },
        { { x + w, y + h }, { u1, v1 }, { r, g, b, a } },
    };
    memcpy(g_uiCur + g_uiCount, q, sizeof(q));
    g_uiCount += 6;
}
static void UiText(float x, float y, float scale, const char* txt,
                   float r, float g, float b) {
    for (; *txt; ++txt) {
        const int c = (unsigned char)*txt;
        if (c < 32 || c > 126) continue;
        const int slot = c - 32;
        const float u0 = (slot % GLYPH_COLS) * GLYPH_W / (float)ATLAS_W;
        const float v0 = (slot / GLYPH_COLS) * GLYPH_H / (float)ATLAS_H;
        UiQuad(x, y, GLYPH_W * scale, GLYPH_H * scale, u0, v0,
               u0 + GLYPH_W / (float)ATLAS_W, v0 + GLYPH_H / (float)ATLAS_H,
               r, g, b, 1.0f);
        x += (GLYPH_W - 3) * scale;
    }
}

static PFN_vkGetInstanceProcAddr gipa;
static PFN_vkGetDeviceProcAddr gdpa;
#define INST_FN(inst, name) PFN_##name name = (PFN_##name)gipa(inst, #name)
#define DEV_FN(dev, name)   PFN_##name name = (PFN_##name)gdpa(dev, #name)

static int g_wantClose = 0;
static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_CLOSE || m == WM_DESTROY) { g_wantClose = 1; return 0; }
    return DefWindowProcA(h, m, w, l);
}

static void* readFile(const char* path, size_t* size) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    *size = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    void* buf = malloc(*size);
    fread(buf, 1, *size, f);
    fclose(f);
    return buf;
}

int main(int argc, char** argv) {
    const int wantFrames = argc > 1 ? atoi(argv[1]) : 600;
    enum { SOLDIERS = 2000, TREES = 80, INSTANCES = SOLDIERS + TREES + 1 };

    HMODULE lib = LoadLibraryA("vulkan-1.dll");
    if (!lib) { printf("vkarmy: no loader\n"); return 1; }
    gipa = (PFN_vkGetInstanceProcAddr)GetProcAddress(lib, "vkGetInstanceProcAddr");

    INST_FN(NULL, vkCreateInstance);
    const char* iext[] = { "VK_KHR_surface", "VK_KHR_win32_surface" };
    VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    app.apiVersion = VK_API_VERSION_1_2;
    VkInstanceCreateInfo ici = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    ici.pApplicationInfo = &app;
    ici.enabledExtensionCount = 2;
    ici.ppEnabledExtensionNames = iext;
    VkInstance inst;
    if (vkCreateInstance(&ici, NULL, &inst)) { printf("vkarmy: instance FAILED\n"); return 1; }
    gdpa = (PFN_vkGetDeviceProcAddr)gipa(inst, "vkGetDeviceProcAddr");

    WNDCLASSA wc = { 0 };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "vkarmy";
    RegisterClassA(&wc);
    HWND hwnd = CreateWindowExA(0, "vkarmy", "OpenWarband Vulkan army (V157)",
                                WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT,
                                CW_USEDEFAULT, 1280, 720, NULL, NULL,
                                wc.hInstance, NULL);
    INST_FN(inst, vkCreateWin32SurfaceKHR);
    VkWin32SurfaceCreateInfoKHR sci = { VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR };
    sci.hinstance = wc.hInstance;
    sci.hwnd = hwnd;
    VkSurfaceKHR surface;
    vkCreateWin32SurfaceKHR(inst, &sci, NULL, &surface);

    INST_FN(inst, vkEnumeratePhysicalDevices);
    INST_FN(inst, vkGetPhysicalDeviceProperties);
    INST_FN(inst, vkGetPhysicalDeviceQueueFamilyProperties);
    INST_FN(inst, vkGetPhysicalDeviceSurfaceSupportKHR);
    INST_FN(inst, vkGetPhysicalDeviceSurfaceCapabilitiesKHR);
    INST_FN(inst, vkGetPhysicalDeviceSurfaceFormatsKHR);
    INST_FN(inst, vkGetPhysicalDeviceMemoryProperties);
    INST_FN(inst, vkCreateDevice);
    uint32_t nd = 0;
    vkEnumeratePhysicalDevices(inst, &nd, NULL);
    VkPhysicalDevice devs[8];
    if (nd > 8) nd = 8;
    vkEnumeratePhysicalDevices(inst, &nd, devs);
    VkPhysicalDevice phys = NULL;
    VkPhysicalDeviceProperties props;
    uint32_t qfam = 0;
    for (int pass = 0; pass < 2 && !phys; ++pass)
        for (uint32_t i = 0; i < nd && !phys; ++i) {
            VkPhysicalDeviceProperties p;
            vkGetPhysicalDeviceProperties(devs[i], &p);
            if (pass == 0 && p.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
                continue;
            uint32_t nq = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(devs[i], &nq, NULL);
            VkQueueFamilyProperties qf[16];
            if (nq > 16) nq = 16;
            vkGetPhysicalDeviceQueueFamilyProperties(devs[i], &nq, qf);
            for (uint32_t q = 0; q < nq; ++q) {
                VkBool32 pres = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR(devs[i], q, surface, &pres);
                if ((qf[q].queueFlags & VK_QUEUE_GRAPHICS_BIT) && pres) {
                    phys = devs[i]; props = p; qfam = q; break;
                }
            }
        }
    if (!phys) { printf("vkarmy: no GPU\n"); return 1; }
    VkPhysicalDeviceMemoryProperties memp;
    vkGetPhysicalDeviceMemoryProperties(phys, &memp);

    const float prio = 1.0f;
    VkDeviceQueueCreateInfo qci = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    qci.queueFamilyIndex = qfam;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;
    const char* dext[] = { "VK_KHR_swapchain" };
    VkDeviceCreateInfo dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = 1;
    dci.ppEnabledExtensionNames = dext;
    VkDevice dev;
    if (vkCreateDevice(phys, &dci, NULL, &dev)) { printf("vkarmy: device FAILED\n"); return 1; }

    DEV_FN(dev, vkGetDeviceQueue);
    DEV_FN(dev, vkCreateSwapchainKHR);
    DEV_FN(dev, vkGetSwapchainImagesKHR);
    DEV_FN(dev, vkCreateImageView);
    DEV_FN(dev, vkCreateImage);
    DEV_FN(dev, vkGetImageMemoryRequirements);
    DEV_FN(dev, vkAllocateMemory);
    DEV_FN(dev, vkBindImageMemory);
    DEV_FN(dev, vkCreateBuffer);
    DEV_FN(dev, vkGetBufferMemoryRequirements);
    DEV_FN(dev, vkBindBufferMemory);
    DEV_FN(dev, vkMapMemory);
    DEV_FN(dev, vkCreateRenderPass);
    DEV_FN(dev, vkCreateFramebuffer);
    DEV_FN(dev, vkCreateShaderModule);
    DEV_FN(dev, vkCreatePipelineLayout);
    DEV_FN(dev, vkCreateGraphicsPipelines);
    DEV_FN(dev, vkCreateCommandPool);
    DEV_FN(dev, vkAllocateCommandBuffers);
    DEV_FN(dev, vkBeginCommandBuffer);
    DEV_FN(dev, vkEndCommandBuffer);
    DEV_FN(dev, vkCmdBeginRenderPass);
    DEV_FN(dev, vkCmdEndRenderPass);
    DEV_FN(dev, vkCmdBindPipeline);
    DEV_FN(dev, vkCmdBindVertexBuffers);
    DEV_FN(dev, vkCmdPushConstants);
    DEV_FN(dev, vkCmdDraw);
    DEV_FN(dev, vkCreateSampler);
    DEV_FN(dev, vkCreateDescriptorSetLayout);
    DEV_FN(dev, vkCreateDescriptorPool);
    DEV_FN(dev, vkAllocateDescriptorSets);
    DEV_FN(dev, vkUpdateDescriptorSets);
    DEV_FN(dev, vkCmdBindDescriptorSets);
    DEV_FN(dev, vkCreateSemaphore);
    DEV_FN(dev, vkCreateFence);
    DEV_FN(dev, vkWaitForFences);
    DEV_FN(dev, vkResetFences);
    DEV_FN(dev, vkResetCommandBuffer);
    DEV_FN(dev, vkAcquireNextImageKHR);
    DEV_FN(dev, vkQueueSubmit);
    DEV_FN(dev, vkQueuePresentKHR);
    DEV_FN(dev, vkDeviceWaitIdle);
    VkQueue queue;
    vkGetDeviceQueue(dev, qfam, 0, &queue);

    // ---- swapchain ----
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phys, surface, &caps);
    uint32_t nf = 1;
    VkSurfaceFormatKHR fmt;
    vkGetPhysicalDeviceSurfaceFormatsKHR(phys, surface, &nf, &fmt);
    VkExtent2D extent = caps.currentExtent;
    VkSwapchainCreateInfoKHR sc = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
    sc.surface = surface;
    sc.minImageCount = caps.minImageCount + 1;
    sc.imageFormat = fmt.format;
    sc.imageColorSpace = fmt.colorSpace;
    sc.imageExtent = extent;
    sc.imageArrayLayers = 1;
    sc.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    sc.preTransform = caps.currentTransform;
    sc.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sc.presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;   // uncapped: a benchmark
    sc.clipped = VK_TRUE;
    VkSwapchainKHR swap;
    if (vkCreateSwapchainKHR(dev, &sc, NULL, &swap)) {
        sc.presentMode = VK_PRESENT_MODE_FIFO_KHR;    // fall back to vsync
        if (vkCreateSwapchainKHR(dev, &sc, NULL, &swap)) {
            printf("vkarmy: swapchain FAILED\n");
            return 1;
        }
    }
    uint32_t ni = 0;
    vkGetSwapchainImagesKHR(dev, swap, &ni, NULL);
    VkImage images[8];
    if (ni > 8) ni = 8;
    vkGetSwapchainImagesKHR(dev, swap, &ni, images);
    VkImageView views[8];
    for (uint32_t i = 0; i < ni; ++i) {
        VkImageViewCreateInfo vci = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        vci.image = images[i];
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = fmt.format;
        vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vci.subresourceRange.levelCount = 1;
        vci.subresourceRange.layerCount = 1;
        vkCreateImageView(dev, &vci, NULL, &views[i]);
    }

    // helper: find host-visible|device memory
    #define FIND_MEM(req, flags) ({ \
        uint32_t _t = 0; \
        for (uint32_t _i = 0; _i < memp.memoryTypeCount; ++_i) \
            if (((req).memoryTypeBits & (1u << _i)) && \
                (memp.memoryTypes[_i].propertyFlags & (flags)) == (flags)) { _t = _i; break; } \
        _t; })

    // ---- depth buffer ----
    VkImageCreateInfo dimg = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    dimg.imageType = VK_IMAGE_TYPE_2D;
    dimg.format = VK_FORMAT_D32_SFLOAT;
    dimg.extent.width = extent.width;
    dimg.extent.height = extent.height;
    dimg.extent.depth = 1;
    dimg.mipLevels = 1;
    dimg.arrayLayers = 1;
    dimg.samples = VK_SAMPLE_COUNT_1_BIT;
    dimg.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    VkImage depthImg;
    vkCreateImage(dev, &dimg, NULL, &depthImg);
    VkMemoryRequirements dreq;
    vkGetImageMemoryRequirements(dev, depthImg, &dreq);
    VkMemoryAllocateInfo dai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    dai.allocationSize = dreq.size;
    dai.memoryTypeIndex = FIND_MEM(dreq, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VkDeviceMemory depthMem;
    vkAllocateMemory(dev, &dai, NULL, &depthMem);
    vkBindImageMemory(dev, depthImg, depthMem, 0);
    VkImageViewCreateInfo dvci = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    dvci.image = depthImg;
    dvci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    dvci.format = VK_FORMAT_D32_SFLOAT;
    dvci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    dvci.subresourceRange.levelCount = 1;
    dvci.subresourceRange.layerCount = 1;
    VkImageView depthView;
    vkCreateImageView(dev, &dvci, NULL, &depthView);

    // ---- render pass (color clear->present, depth clear) ----
    VkAttachmentDescription at[2] = { 0 };
    at[0].format = fmt.format;
    at[0].samples = VK_SAMPLE_COUNT_1_BIT;
    at[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    at[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    at[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    at[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    at[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    at[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    at[1].format = VK_FORMAT_D32_SFLOAT;
    at[1].samples = VK_SAMPLE_COUNT_1_BIT;
    at[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    at[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    at[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    at[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    at[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    at[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    VkAttachmentReference colRef = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkAttachmentReference depRef = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
    VkSubpassDescription sub = { 0 };
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &colRef;
    sub.pDepthStencilAttachment = &depRef;
    VkRenderPassCreateInfo rpci = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    rpci.attachmentCount = 2;
    rpci.pAttachments = at;
    rpci.subpassCount = 1;
    rpci.pSubpasses = &sub;
    VkRenderPass rp;
    vkCreateRenderPass(dev, &rpci, NULL, &rp);

    VkFramebuffer fbs[8];
    for (uint32_t i = 0; i < ni; ++i) {
        VkImageView atts[2] = { views[i], depthView };
        VkFramebufferCreateInfo fci = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
        fci.renderPass = rp;
        fci.attachmentCount = 2;
        fci.pAttachments = atts;
        fci.width = extent.width;
        fci.height = extent.height;
        fci.layers = 1;
        vkCreateFramebuffer(dev, &fci, NULL, &fbs[i]);
    }

    // ---- shaders + pipeline ----
    size_t vsSize, fsSize;
    void* vsCode = readFile("assets/spv/box.vert.spv", &vsSize);
    void* fsCode = readFile("assets/spv/box.frag.spv", &fsSize);
    if (!vsCode || !fsCode) { printf("vkarmy: missing assets/spv/*.spv\n"); return 1; }
    VkShaderModuleCreateInfo smci = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    smci.codeSize = vsSize;
    smci.pCode = (const uint32_t*)vsCode;
    VkShaderModule vs;
    vkCreateShaderModule(dev, &smci, NULL, &vs);
    smci.codeSize = fsSize;
    smci.pCode = (const uint32_t*)fsCode;
    VkShaderModule fs;
    vkCreateShaderModule(dev, &smci, NULL, &fs);

    VkPushConstantRange pcr = { VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 80 };
    VkPipelineLayoutCreateInfo plci = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &pcr;
    VkPipelineLayout layout;
    vkCreatePipelineLayout(dev, &plci, NULL, &layout);

    VkPipelineShaderStageCreateInfo stages[2] = { { 0 }, { 0 } };
    stages[0].sType = stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs;
    stages[0].pName = "main";
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fs;
    stages[1].pName = "main";

    VkVertexInputBindingDescription binds[2] = {
        { 0, 6 * sizeof(float), VK_VERTEX_INPUT_RATE_VERTEX },
        { 1, sizeof(Inst),      VK_VERTEX_INPUT_RATE_INSTANCE },
    };
    VkVertexInputAttributeDescription attrs[7] = {
        { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 },
        { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12 },
        { 2, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 0 },
        { 3, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 16 },
        { 4, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 32 },
        { 5, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 48 },
        { 6, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 64 },
    };
    VkPipelineVertexInputStateCreateInfo vin = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    vin.vertexBindingDescriptionCount = 2;
    vin.pVertexBindingDescriptions = binds;
    vin.vertexAttributeDescriptionCount = 7;
    vin.pVertexAttributeDescriptions = attrs;
    VkPipelineInputAssemblyStateCreateInfo ia = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkViewport vp = { 0, 0, (float)extent.width, (float)extent.height, 0, 1 };
    VkRect2D scis = { { 0, 0 }, extent };
    VkPipelineViewportStateCreateInfo vps = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    vps.viewportCount = 1;
    vps.pViewports = &vp;
    vps.scissorCount = 1;
    vps.pScissors = &scis;
    VkPipelineRasterizationStateCreateInfo rs = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_BACK_BIT;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    ds.depthTestEnable = VK_TRUE;
    ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS;
    VkPipelineColorBlendAttachmentState cba = { 0 };
    cba.colorWriteMask = 0xF;
    VkPipelineColorBlendStateCreateInfo cb = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    cb.attachmentCount = 1;
    cb.pAttachments = &cba;
    VkGraphicsPipelineCreateInfo gpci = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    gpci.stageCount = 2;
    gpci.pStages = stages;
    gpci.pVertexInputState = &vin;
    gpci.pInputAssemblyState = &ia;
    gpci.pViewportState = &vps;
    gpci.pRasterizationState = &rs;
    gpci.pMultisampleState = &ms;
    gpci.pDepthStencilState = &ds;
    gpci.pColorBlendState = &cb;
    gpci.layout = layout;
    gpci.renderPass = rp;
    VkPipeline pipe;
    if (vkCreateGraphicsPipelines(dev, NULL, 1, &gpci, NULL, &pipe)) {
        printf("vkarmy: pipeline FAILED\n");
        return 1;
    }

    // ---- second pipeline: static mesh (terrain) ----
    size_t mvsSize;
    void* mvsCode = readFile("assets/spv/mesh.vert.spv", &mvsSize);
    if (!mvsCode) { printf("vkarmy: missing mesh.vert.spv\n"); return 1; }
    VkShaderModule mvs;
    {
        VkShaderModuleCreateInfo mci = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        mci.codeSize = mvsSize;
        mci.pCode = (const uint32_t*)mvsCode;
        vkCreateShaderModule(dev, &mci, NULL, &mvs);
    }
    VkPipeline meshPipe;
    {
        VkPipelineShaderStageCreateInfo mst[2] = { stages[0], stages[1] };
        mst[0].module = mvs;
        VkVertexInputBindingDescription mb = { 0, sizeof(MeshVert),
                                               VK_VERTEX_INPUT_RATE_VERTEX };
        VkVertexInputAttributeDescription ma[3] = {
            { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 },
            { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12 },
            { 2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 24 },
        };
        VkPipelineVertexInputStateCreateInfo mvin = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
        mvin.vertexBindingDescriptionCount = 1;
        mvin.pVertexBindingDescriptions = &mb;
        mvin.vertexAttributeDescriptionCount = 3;
        mvin.pVertexAttributeDescriptions = ma;
        VkGraphicsPipelineCreateInfo mpci = gpci;
        mpci.pStages = mst;
        mpci.pVertexInputState = &mvin;
        if (vkCreateGraphicsPipelines(dev, NULL, 1, &mpci, NULL, &meshPipe)) {
            printf("vkarmy: mesh pipeline FAILED\n");
            return 1;
        }
    }

    // ---- geometry: one unit cube (36 verts, pos+normal) ----
    float cube[36 * 6];
    {
        const float f[6][3] = { { 0, 0, 1 }, { 0, 0, -1 }, { 1, 0, 0 },
                                { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 } };
        int v = 0;
        for (int face = 0; face < 6; ++face) {
            float* n = (float*)f[face];
            float u[3] = { n[1], n[2], n[0] };
            float w[3] = { n[2] * u[1] - n[1] * u[2],
                           n[0] * u[2] - n[2] * u[0],
                           n[1] * u[0] - n[0] * u[1] };
            const float sgn[6][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 },
                                      { -1, -1 }, { 1, 1 }, { -1, 1 } };
            for (int k = 0; k < 6; ++k) {
                for (int c = 0; c < 3; ++c)
                    cube[v * 6 + c] = 0.5f * (n[c] + sgn[k][0] * u[c] + sgn[k][1] * w[c]);
                for (int c = 0; c < 3; ++c) cube[v * 6 + 3 + c] = n[c];
                v++;
            }
        }
    }

    // ---- the terrain mesh (V158): 96x96 cells, smooth normals,
    //      grass/dirt/rock palette by height and slope ----
    enum { TN = 96 };
    const float ARENA = 120.0f, CELL = (2.0f * ARENA) / TN;
    for (int i = 0; i < NHILLS; ++i) {
        g_hx[i] = (frand() * 2.0f - 1.0f) * ARENA * 0.7f;
        g_hz[i] = (frand() * 2.0f - 1.0f) * ARENA * 0.7f;
        g_hr[i] = ARENA * (0.2f + frand() * 0.4f);
        g_hh[i] = 3.0f + frand() * 14.0f;
    }
    const int terrVerts = TN * TN * 6;
    MeshVert* terr = (MeshVert*)malloc(sizeof(MeshVert) * terrVerts);
    {
        int v = 0;
        for (int j = 0; j < TN; ++j)
            for (int i = 0; i < TN; ++i) {
                const float x0 = -ARENA + i * CELL, x1 = x0 + CELL;
                const float z0 = -ARENA + j * CELL, z1 = z0 + CELL;
                const float corner[4][2] = { { x0, z0 }, { x1, z0 },
                                             { x0, z1 }, { x1, z1 } };
                const int tri[6] = { 0, 2, 1, 1, 2, 3 };
                for (int k = 0; k < 6; ++k) {
                    const float x = corner[tri[k]][0], z = corner[tri[k]][1];
                    const float h = heightAt(x, z);
                    const float e = CELL * 0.5f;
                    const float dx = heightAt(x + e, z) - heightAt(x - e, z);
                    const float dz = heightAt(x, z + e) - heightAt(x, z - e);
                    float nx = -dx, ny = 2.0f * e, nz = -dz;
                    const float nl = sqrtf(nx * nx + ny * ny + nz * nz);
                    nx /= nl; ny /= nl; nz /= nl;
                    const float slope = 1.0f - ny;
                    MeshVert* mv = &terr[v++];
                    mv->pos[0] = x; mv->pos[1] = h; mv->pos[2] = z;
                    mv->nrm[0] = nx; mv->nrm[1] = ny; mv->nrm[2] = nz;
                    float r = 0.24f, g = 0.38f, b = 0.20f;         // grass
                    if (slope > 0.28f) { r = 0.42f; g = 0.34f; b = 0.25f; }  // dirt
                    if (h > 10.0f)     { r = 0.52f; g = 0.51f; b = 0.53f; }  // rock
                    const float jig = 0.92f + 0.08f * frand();
                    mv->col[0] = r * jig; mv->col[1] = g * jig;
                    mv->col[2] = b * jig; mv->col[3] = 1.0f;
                }
            }
    }

    // ---- buffers (host-visible; instance buffer rewritten per frame) ----
    VkBuffer vbuf, ibuf;
    VkDeviceMemory vmem, imem;
    void *vmap, *imap;
    {
        VkBufferCreateInfo bci = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        bci.size = sizeof(cube);
        bci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        vkCreateBuffer(dev, &bci, NULL, &vbuf);
        VkMemoryRequirements req;
        vkGetBufferMemoryRequirements(dev, vbuf, &req);
        VkMemoryAllocateInfo mai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        mai.allocationSize = req.size;
        mai.memoryTypeIndex = FIND_MEM(req, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        vkAllocateMemory(dev, &mai, NULL, &vmem);
        vkBindBufferMemory(dev, vbuf, vmem, 0);
        vkMapMemory(dev, vmem, 0, sizeof(cube), 0, &vmap);
        memcpy(vmap, cube, sizeof(cube));

        bci.size = sizeof(Inst) * INSTANCES;
        vkCreateBuffer(dev, &bci, NULL, &ibuf);
        vkGetBufferMemoryRequirements(dev, ibuf, &req);
        mai.allocationSize = req.size;
        mai.memoryTypeIndex = FIND_MEM(req, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        vkAllocateMemory(dev, &mai, NULL, &imem);
        vkBindBufferMemory(dev, ibuf, imem, 0);
        vkMapMemory(dev, imem, 0, sizeof(Inst) * INSTANCES, 0, &imap);
    }
    VkBuffer tbuf;
    VkDeviceMemory tmem;
    {
        VkBufferCreateInfo bci = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        bci.size = sizeof(MeshVert) * terrVerts;
        bci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        vkCreateBuffer(dev, &bci, NULL, &tbuf);
        VkMemoryRequirements req;
        vkGetBufferMemoryRequirements(dev, tbuf, &req);
        VkMemoryAllocateInfo mai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        mai.allocationSize = req.size;
        mai.memoryTypeIndex = FIND_MEM(req, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        vkAllocateMemory(dev, &mai, NULL, &tmem);
        vkBindBufferMemory(dev, tbuf, tmem, 0);
        void* tmap;
        vkMapMemory(dev, tmem, 0, bci.size, 0, &tmap);
        memcpy(tmap, terr, sizeof(MeshVert) * terrVerts);
    }

    // ---- the 2D overlay (V159): atlas texture, descriptor, pipeline ----
    BuildAtlas();
    VkImage atlasImg;
    VkImageView atlasView;
    VkSampler atlasSamp;
    {
        VkImageCreateInfo aci = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        aci.imageType = VK_IMAGE_TYPE_2D;
        aci.format = VK_FORMAT_R8_UNORM;
        aci.extent.width = ATLAS_W;
        aci.extent.height = ATLAS_H;
        aci.extent.depth = 1;
        aci.mipLevels = 1;
        aci.arrayLayers = 1;
        aci.samples = VK_SAMPLE_COUNT_1_BIT;
        aci.tiling = VK_IMAGE_TILING_LINEAR;   // host-writable, demo-simple
        aci.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
        aci.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;
        vkCreateImage(dev, &aci, NULL, &atlasImg);
        VkMemoryRequirements areq;
        vkGetImageMemoryRequirements(dev, atlasImg, &areq);
        VkMemoryAllocateInfo aai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        aai.allocationSize = areq.size;
        aai.memoryTypeIndex = FIND_MEM(areq, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        VkDeviceMemory amem;
        vkAllocateMemory(dev, &aai, NULL, &amem);
        vkBindImageMemory(dev, atlasImg, amem, 0);
        void* amap;
        vkMapMemory(dev, amem, 0, areq.size, 0, &amap);
        memcpy(amap, g_atlas, sizeof(g_atlas));   // linear R8, tightly packed
        VkImageViewCreateInfo avci = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        avci.image = atlasImg;
        avci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        avci.format = VK_FORMAT_R8_UNORM;
        avci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        avci.subresourceRange.levelCount = 1;
        avci.subresourceRange.layerCount = 1;
        vkCreateImageView(dev, &avci, NULL, &atlasView);
        VkSamplerCreateInfo smpci = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
        smpci.magFilter = VK_FILTER_LINEAR;
        smpci.minFilter = VK_FILTER_LINEAR;
        smpci.addressModeU = smpci.addressModeV = smpci.addressModeW =
            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        vkCreateSampler(dev, &smpci, NULL, &atlasSamp);
    }
    VkDescriptorSetLayout dsl;
    VkDescriptorSet dset;
    {
        VkDescriptorSetLayoutBinding b = { 0 };
        b.binding = 0;
        b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b.descriptorCount = 1;
        b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo dlci = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        dlci.bindingCount = 1;
        dlci.pBindings = &b;
        vkCreateDescriptorSetLayout(dev, &dlci, NULL, &dsl);
        VkDescriptorPoolSize ps = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 };
        VkDescriptorPoolCreateInfo dpci = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        dpci.maxSets = 1;
        dpci.poolSizeCount = 1;
        dpci.pPoolSizes = &ps;
        VkDescriptorPool dpool;
        vkCreateDescriptorPool(dev, &dpci, NULL, &dpool);
        VkDescriptorSetAllocateInfo dsai = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        dsai.descriptorPool = dpool;
        dsai.descriptorSetCount = 1;
        dsai.pSetLayouts = &dsl;
        vkAllocateDescriptorSets(dev, &dsai, &dset);
        VkDescriptorImageInfo dii = { atlasSamp, atlasView,
                                      VK_IMAGE_LAYOUT_GENERAL };
        VkWriteDescriptorSet w = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        w.dstSet = dset;
        w.dstBinding = 0;
        w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w.pImageInfo = &dii;
        vkUpdateDescriptorSets(dev, 1, &w, 0, NULL);
    }
    VkPipelineLayout uiLayout;
    VkPipeline uiPipe;
    VkBuffer uibuf;
    void* uimap;
    enum { UI_MAX_VERTS = 8192 };
    {
        size_t tvs, tfs;
        void* tvsc = readFile("assets/spv/text.vert.spv", &tvs);
        void* tfsc = readFile("assets/spv/text.frag.spv", &tfs);
        if (!tvsc || !tfsc) { printf("vkarmy: missing text shaders\n"); return 1; }
        VkShaderModuleCreateInfo mci = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        mci.codeSize = tvs;
        mci.pCode = (const uint32_t*)tvsc;
        VkShaderModule tv;
        vkCreateShaderModule(dev, &mci, NULL, &tv);
        mci.codeSize = tfs;
        mci.pCode = (const uint32_t*)tfsc;
        VkShaderModule tf;
        vkCreateShaderModule(dev, &mci, NULL, &tf);
        VkPushConstantRange upcr = { VK_SHADER_STAGE_VERTEX_BIT, 0, 16 };
        VkPipelineLayoutCreateInfo uplci = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        uplci.setLayoutCount = 1;
        uplci.pSetLayouts = &dsl;
        uplci.pushConstantRangeCount = 1;
        uplci.pPushConstantRanges = &upcr;
        vkCreatePipelineLayout(dev, &uplci, NULL, &uiLayout);
        VkPipelineShaderStageCreateInfo ust[2] = { stages[0], stages[1] };
        ust[0].module = tv;
        ust[1].module = tf;
        VkVertexInputBindingDescription ub = { 0, sizeof(UiVert),
                                               VK_VERTEX_INPUT_RATE_VERTEX };
        VkVertexInputAttributeDescription ua[3] = {
            { 0, 0, VK_FORMAT_R32G32_SFLOAT, 0 },
            { 1, 0, VK_FORMAT_R32G32_SFLOAT, 8 },
            { 2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 16 },
        };
        VkPipelineVertexInputStateCreateInfo uvin = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
        uvin.vertexBindingDescriptionCount = 1;
        uvin.pVertexBindingDescriptions = &ub;
        uvin.vertexAttributeDescriptionCount = 3;
        uvin.pVertexAttributeDescriptions = ua;
        VkPipelineColorBlendAttachmentState ucba = { 0 };
        ucba.blendEnable = VK_TRUE;
        ucba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        ucba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        ucba.colorBlendOp = VK_BLEND_OP_ADD;
        ucba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        ucba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        ucba.alphaBlendOp = VK_BLEND_OP_ADD;
        ucba.colorWriteMask = 0xF;
        VkPipelineColorBlendStateCreateInfo ucb = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
        ucb.attachmentCount = 1;
        ucb.pAttachments = &ucba;
        VkPipelineDepthStencilStateCreateInfo uds = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
        VkGraphicsPipelineCreateInfo upci = gpci;
        upci.pStages = ust;
        upci.pVertexInputState = &uvin;
        upci.pColorBlendState = &ucb;
        upci.pDepthStencilState = &uds;   // no depth for the overlay
        upci.layout = uiLayout;
        if (vkCreateGraphicsPipelines(dev, NULL, 1, &upci, NULL, &uiPipe)) {
            printf("vkarmy: ui pipeline FAILED\n");
            return 1;
        }
        VkBufferCreateInfo bci = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        bci.size = sizeof(UiVert) * UI_MAX_VERTS;
        bci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        vkCreateBuffer(dev, &bci, NULL, &uibuf);
        VkMemoryRequirements req;
        vkGetBufferMemoryRequirements(dev, uibuf, &req);
        VkMemoryAllocateInfo mai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        mai.allocationSize = req.size;
        mai.memoryTypeIndex = FIND_MEM(req, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        VkDeviceMemory umem;
        vkAllocateMemory(dev, &mai, NULL, &umem);
        vkBindBufferMemory(dev, uibuf, umem, 0);
        vkMapMemory(dev, umem, 0, bci.size, 0, &uimap);
    }

    // ---- commands + sync ----
    VkCommandPoolCreateInfo cpci = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpci.queueFamilyIndex = qfam;
    VkCommandPool pool;
    vkCreateCommandPool(dev, &cpci, NULL, &pool);
    VkCommandBufferAllocateInfo cbai = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    cbai.commandPool = pool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(dev, &cbai, &cmd);
    VkSemaphoreCreateInfo semci = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkSemaphore semA, semR;
    vkCreateSemaphore(dev, &semci, NULL, &semA);
    vkCreateSemaphore(dev, &semci, NULL, &semR);
    VkFenceCreateInfo fenci = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    fenci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    VkFence fence;
    vkCreateFence(dev, &fenci, NULL, &fence);

    // ---- the army marches ----
    Inst* insts = (Inst*)imap;
    LARGE_INTEGER freq, t0, t1;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);
    int frames = 0;
    while (frames < wantFrames && !g_wantClose) {
        MSG msg;
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        const float t = frames / 60.0f;

        // ground slab
        memset(&insts[0], 0, sizeof(Inst));
        insts[0].row0[0] = 280.0f;
        insts[0].row1[1] = 0.1f;
        insts[0].row2[2] = 280.0f;
        insts[0].row3[1] = -0.6f;   // an under-plate beneath the heightfield
        insts[0].row3[3] = 1.0f;
        insts[0].color[0] = 0.24f; insts[0].color[1] = 0.36f;
        insts[0].color[2] = 0.20f; insts[0].color[3] = 1.0f;
        // two armies marching toward each other
        for (int i = 0; i < SOLDIERS; ++i) {
            Inst* it = &insts[i + 1];
            memset(it, 0, sizeof(Inst));
            const int team = i & 1;
            const int col = (i / 2) % 50, row = (i / 2) / 50;
            const float dirz = team ? -1.0f : 1.0f;
            const float x = -49.0f + col * 2.0f + (team ? 1.0f : 0.0f);
            const float z = dirz * (18.0f + row * 2.0f) - dirz * fmodf(t * 2.0f, 14.0f);
            const float bob = fabsf(sinf(t * 8.0f + i)) * 0.12f;
            it->row0[0] = 0.62f;
            it->row1[1] = 1.8f;
            it->row2[2] = 0.44f;
            it->row3[0] = x;
            it->row3[1] = heightAt(x, z) + 0.9f + bob;   // stand ON the field
            it->row3[2] = z;
            it->row3[3] = 1.0f;
            it->color[0] = team ? 0.75f : 0.16f;
            it->color[1] = 0.18f + 0.1f * ((i * 37 % 13) / 13.0f);
            it->color[2] = team ? 0.16f : 0.72f;
            it->color[3] = 1.0f;
        }

        // trees: static box canopies rooted in the field
        for (int i = 0; i < TREES; ++i) {
            Inst* it = &insts[SOLDIERS + 1 + i];
            memset(it, 0, sizeof(Inst));
            unsigned th = (unsigned)(i * 2654435761u);
            const float tx = ((th & 0xFFFF) / 65535.0f * 2.0f - 1.0f) * 110.0f;
            const float tz = (((th >> 12) & 0xFFFF) / 65535.0f * 2.0f - 1.0f) * 110.0f;
            const float hgt = 4.0f + (th % 7) * 0.6f;
            it->row0[0] = 1.6f;
            it->row1[1] = hgt;
            it->row2[2] = 1.6f;
            it->row3[0] = tx;
            it->row3[1] = heightAt(tx, tz) + hgt * 0.5f;
            it->row3[2] = tz;
            it->row3[3] = 1.0f;
            it->color[0] = 0.13f; it->color[1] = 0.34f + (th % 5) * 0.02f;
            it->color[2] = 0.15f; it->color[3] = 1.0f;
        }

        vkWaitForFences(dev, 1, &fence, VK_TRUE, UINT64_MAX);
        vkResetFences(dev, 1, &fence);
        uint32_t idx = 0;
        if (vkAcquireNextImageKHR(dev, swap, UINT64_MAX, semA, NULL, &idx))
            break;

        vkResetCommandBuffer(cmd, 0);
        VkCommandBufferBeginInfo bi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        vkBeginCommandBuffer(cmd, &bi);
        VkClearValue clears[2];
        clears[0].color.float32[0] = 0.53f;
        clears[0].color.float32[1] = 0.68f;
        clears[0].color.float32[2] = 0.86f;   // day sky
        clears[0].color.float32[3] = 1.0f;
        clears[1].depthStencil.depth = 1.0f;
        VkRenderPassBeginInfo rpbi = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
        rpbi.renderPass = rp;
        rpbi.framebuffer = fbs[idx];
        rpbi.renderArea.extent = extent;
        rpbi.clearValueCount = 2;
        rpbi.pClearValues = clears;
        vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);

        struct { M4 vp; float sun[4]; } pc;
        const float ca = t * 0.15f;
        M4 view = m4look(sinf(ca) * 95.0f, 46.0f, cosf(ca) * 95.0f, 0.0f, 3.0f, 0.0f);
        M4 proj = m4persp(1.05f, (float)extent.width / extent.height, 0.5f, 600.0f);
        pc.vp = m4mul(proj, view);
        pc.sun[0] = -0.45f; pc.sun[1] = -0.75f; pc.sun[2] = -0.35f; pc.sun[3] = 0;
        vkCmdPushConstants(cmd, layout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(pc), &pc);
        // the battlefield: one mesh draw
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, meshPipe);
        VkDeviceSize toff = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &tbuf, &toff);
        vkCmdDraw(cmd, (uint32_t)terrVerts, 1, 0, 0);
        // the army + trees: one instanced draw
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
        VkBuffer bufs[2] = { vbuf, ibuf };
        VkDeviceSize offs[2] = { 0, 0 };
        vkCmdBindVertexBuffers(cmd, 0, 2, bufs, offs);
        vkCmdDraw(cmd, 36, INSTANCES, 0, 0);

        // ---- the HUD, on Vulkan (V159) ----
        g_uiCur = (UiVert*)uimap;
        g_uiCount = 0;
        UiQuad(16, 14, 560, 74, -1, -1, -1, -1, 0.03f, 0.03f, 0.05f, 0.62f);
        UiText(28, 20, 1.0f, "OPENWARBAND - THE VULKAN FRAME", 0.98f, 0.82f, 0.35f);
        char line[96];
        snprintf(line, sizeof(line), "%d soldiers  frame %d  text+3d, 3 draws",
                 SOLDIERS, frames);
        UiText(28, 50, 0.8f, line, 0.92f, 0.92f, 0.92f);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, uiPipe);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, uiLayout,
                                0, 1, &dset, 0, NULL);
        float scr[4] = { (float)extent.width, (float)extent.height, 0, 0 };
        vkCmdPushConstants(cmd, uiLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, 16, scr);
        VkDeviceSize uoff = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &uibuf, &uoff);
        vkCmdDraw(cmd, (uint32_t)g_uiCount, 1, 0, 0);

        vkCmdEndRenderPass(cmd);
        vkEndCommandBuffer(cmd);

        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
        si.waitSemaphoreCount = 1;
        si.pWaitSemaphores = &semA;
        si.pWaitDstStageMask = &waitStage;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmd;
        si.signalSemaphoreCount = 1;
        si.pSignalSemaphores = &semR;
        vkQueueSubmit(queue, 1, &si, fence);
        VkPresentInfoKHR pi = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
        pi.waitSemaphoreCount = 1;
        pi.pWaitSemaphores = &semR;
        pi.swapchainCount = 1;
        pi.pSwapchains = &swap;
        pi.pImageIndices = &idx;
        if (vkQueuePresentKHR(queue, &pi)) break;
        frames++;
    }
    vkDeviceWaitIdle(dev);
    QueryPerformanceCounter(&t1);
    const double sec = (double)(t1.QuadPart - t0.QuadPart) / freq.QuadPart;
    printf("vkarmy: %d frames, %d instanced soldiers, avg %.2f ms (%.0f fps) on %s\n",
           frames, SOLDIERS, sec / (frames > 0 ? frames : 1) * 1000.0,
           frames / (sec > 0 ? sec : 1), props.deviceName);
    return frames > 0 ? 0 : 1;
}
