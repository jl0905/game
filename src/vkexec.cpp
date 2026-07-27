#include <string.h>
#include <stdlib.h>

// raylib.h cannot coexist with windows.h (CloseWindow/ShowCursor/Rectangle
// clash), so the few raylib calls used here are declared by hand.
extern "C" bool IsWindowReady(void);
extern "C" void TraceLog(int logLevel, const char* text, ...);
enum { RL_LOG_INFO = 3, RL_LOG_WARNING = 4 };

// ---------------------------------------------------------------------------
// The in-game Vulkan executor (V162 boot, V163 frame executor). When
// settings.cfg says `renderer vulkan`, the GAME PROCESS brings a Vulkan
// device up at first flush, then renders the frame's recorded instance
// buckets on it: offscreen R8G8B8A8+D32 target, the proven vkarmy box
// pipeline (assets/spv/box.*.spv), one instanced draw per frame, readback
// to host memory. rdr.cpp composites the result over the GL scene â€” the
// present-interop stage of RENDERER.md; the native window swap retires the
// composite. Headless runs and `renderer raylib` never touch any of this.
// ---------------------------------------------------------------------------
#if defined(_WIN32)
#define VK_NO_PROTOTYPES
#include <windows.h>
#include <vulkan/vulkan.h>
#include <stdio.h>

namespace rdr {

namespace {

#define IFN(name) PFN_##name name = (PFN_##name)g_gipa(g_vk.inst, #name)
#define DFN(name) PFN_##name name = (PFN_##name)g_gdpa(g_vk.dev, #name)

PFN_vkGetInstanceProcAddr g_gipa = nullptr;
PFN_vkGetDeviceProcAddr g_gdpa = nullptr;

struct VkExec {
    bool tried = false, live = false, frameReady = false, frameFailed = false;
    char gpuName[256] = { 0 };
    VkInstance inst = VK_NULL_HANDLE;
    VkPhysicalDevice phys = VK_NULL_HANDLE;
    VkDevice dev = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t qfam = 0;
    VkPhysicalDeviceMemoryProperties memProps{};
    // frame resources
    int width = 0, height = 0;
    VkImage color = VK_NULL_HANDLE, depth = VK_NULL_HANDLE;
    VkDeviceMemory colorMem = VK_NULL_HANDLE, depthMem = VK_NULL_HANDLE;
    VkImageView colorView = VK_NULL_HANDLE, depthView = VK_NULL_HANDLE;
    VkRenderPass rp = VK_NULL_HANDLE;
    VkFramebuffer fb = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline pipe = VK_NULL_HANDLE;
    VkBuffer cubeBuf = VK_NULL_HANDLE;
    VkDeviceMemory cubeMem = VK_NULL_HANDLE;
    // Pipelined executor (V166): two in-flight slots. The CPU fills slot N
    // and presents slot N-1, so the fence wait lands on work the GPU
    // finished a frame ago instead of stalling on the frame just submitted.
    VkBuffer instBuf[2] = {}, readBuf[2] = {};
    VkDeviceMemory instMem[2] = {}, readMem[2] = {};
    void* instMap[2] = {};
    void* readMap[2] = {};
    int instCap[2] = {};
    VkCommandBuffer cmd[2] = {};
    VkFence fence[2] = {};
    bool submitted[2] = {};
    int frame = 0;
    VkCommandPool pool = VK_NULL_HANDLE;
    // terrain occluder (V164): depth-only static mesh
    VkPipeline meshPipe = VK_NULL_HANDLE;
    VkBuffer terrBuf = VK_NULL_HANDLE;
    VkDeviceMemory terrMem = VK_NULL_HANDLE;
    int terrCount = 0;
    float* terrPending = nullptr;      // staged before the device is up
    int terrPendingCount = 0;
};
VkExec g_vk;

void BootDevice() {
    g_vk.tried = true;
    HMODULE lib = LoadLibraryA("vulkan-1.dll");
    if (!lib) return;
    g_gipa = (PFN_vkGetInstanceProcAddr)GetProcAddress(lib, "vkGetInstanceProcAddr");
    auto createInstance = (PFN_vkCreateInstance)g_gipa(NULL, "vkCreateInstance");
    VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    app.pApplicationName = "openwarband";
    app.apiVersion = VK_API_VERSION_1_2;
    VkInstanceCreateInfo ici = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    ici.pApplicationInfo = &app;
    if (createInstance(&ici, NULL, &g_vk.inst) != VK_SUCCESS) return;

    IFN(vkEnumeratePhysicalDevices);
    IFN(vkGetPhysicalDeviceProperties);
    IFN(vkGetPhysicalDeviceQueueFamilyProperties);
    IFN(vkGetPhysicalDeviceMemoryProperties);
    IFN(vkCreateDevice);
    IFN(vkGetDeviceProcAddr);
    g_gdpa = vkGetDeviceProcAddr;
    uint32_t nd = 0;
    vkEnumeratePhysicalDevices(g_vk.inst, &nd, NULL);
    VkPhysicalDevice devs[8];
    if (nd > 8) nd = 8;
    vkEnumeratePhysicalDevices(g_vk.inst, &nd, devs);
    VkPhysicalDeviceProperties props{};
    for (int pass = 0; pass < 2 && !g_vk.phys; ++pass)
        for (uint32_t i = 0; i < nd && !g_vk.phys; ++i) {
            VkPhysicalDeviceProperties p;
            vkGetPhysicalDeviceProperties(devs[i], &p);
            if (pass == 0 && p.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
                continue;
            uint32_t nq = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(devs[i], &nq, NULL);
            VkQueueFamilyProperties qf[16];
            if (nq > 16) nq = 16;
            vkGetPhysicalDeviceQueueFamilyProperties(devs[i], &nq, qf);
            for (uint32_t q = 0; q < nq; ++q)
                if (qf[q].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                    g_vk.phys = devs[i];
                    props = p;
                    g_vk.qfam = q;
                    break;
                }
        }
    if (!g_vk.phys) return;
    vkGetPhysicalDeviceMemoryProperties(g_vk.phys, &g_vk.memProps);
    const float prio = 1.0f;
    VkDeviceQueueCreateInfo qci = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    qci.queueFamilyIndex = g_vk.qfam;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;
    VkDeviceCreateInfo dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    if (vkCreateDevice(g_vk.phys, &dci, NULL, &g_vk.dev) != VK_SUCCESS) return;
    DFN(vkGetDeviceQueue);
    vkGetDeviceQueue(g_vk.dev, g_vk.qfam, 0, &g_vk.queue);
    strncpy(g_vk.gpuName, props.deviceName, sizeof(g_vk.gpuName) - 1);
    g_vk.live = true;
}

uint32_t MemType(uint32_t bits, VkMemoryPropertyFlags want) {
    for (uint32_t i = 0; i < g_vk.memProps.memoryTypeCount; ++i)
        if ((bits & (1u << i)) &&
            (g_vk.memProps.memoryTypes[i].propertyFlags & want) == want)
            return i;
    return 0;
}

bool MakeBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                VkMemoryPropertyFlags memFlags, VkBuffer* buf,
                VkDeviceMemory* mem, void** map) {
    DFN(vkCreateBuffer); DFN(vkGetBufferMemoryRequirements);
    DFN(vkAllocateMemory); DFN(vkBindBufferMemory); DFN(vkMapMemory);
    VkBufferCreateInfo bci = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bci.size = size;
    bci.usage = usage;
    if (vkCreateBuffer(g_vk.dev, &bci, NULL, buf) != VK_SUCCESS) return false;
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(g_vk.dev, *buf, &req);
    VkMemoryAllocateInfo mai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = MemType(req.memoryTypeBits, memFlags);
    if (vkAllocateMemory(g_vk.dev, &mai, NULL, mem) != VK_SUCCESS) return false;
    vkBindBufferMemory(g_vk.dev, *buf, *mem, 0);
    if (map) vkMapMemory(g_vk.dev, *mem, 0, size, 0, map);
    return true;
}

bool MakeImage(int w, int h, VkFormat fmt, VkImageUsageFlags usage,
               VkImageAspectFlags aspect, VkImage* img, VkDeviceMemory* mem,
               VkImageView* view) {
    DFN(vkCreateImage); DFN(vkGetImageMemoryRequirements);
    DFN(vkAllocateMemory); DFN(vkBindImageMemory); DFN(vkCreateImageView);
    VkImageCreateInfo ici = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = fmt;
    ici.extent = { (uint32_t)w, (uint32_t)h, 1 };
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.usage = usage;
    if (vkCreateImage(g_vk.dev, &ici, NULL, img) != VK_SUCCESS) return false;
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(g_vk.dev, *img, &req);
    VkMemoryAllocateInfo mai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = MemType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(g_vk.dev, &mai, NULL, mem) != VK_SUCCESS) return false;
    vkBindImageMemory(g_vk.dev, *img, *mem, 0);
    VkImageViewCreateInfo vci = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    vci.image = *img;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = fmt;
    vci.subresourceRange = { aspect, 0, 1, 0, 1 };
    return vkCreateImageView(g_vk.dev, &vci, NULL, view) == VK_SUCCESS;
}

void* ReadSpv(const char* path, size_t* size) {
    FILE* f = fopen(path, "rb");
    if (!f) return nullptr;
    fseek(f, 0, SEEK_END);
    *size = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    void* data = malloc(*size);
    fread(data, 1, *size, f);
    fclose(f);
    return data;
}

// Unit cube centred on the origin, 36 verts, pos+normal interleaved.
void FillCube(float* v) {
    const float n[6][3] = { { 0, 0, 1 }, { 0, 0, -1 }, { 1, 0, 0 },
                            { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 } };
    int k = 0;
    for (int f = 0; f < 6; ++f) {
        const float* nn = n[f];
        const float u[3] = { nn[1], nn[2], nn[0] };   // any perpendicular
        const float w[3] = { nn[1] * u[2] - nn[2] * u[1],
                             nn[2] * u[0] - nn[0] * u[2],
                             nn[0] * u[1] - nn[1] * u[0] };
        const int idx[6][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 },
                                { -1, -1 }, { 1, 1 }, { -1, 1 } };
        for (int t = 0; t < 6; ++t) {
            for (int a = 0; a < 3; ++a)
                v[k++] = 0.5f * (nn[a] + idx[t][0] * u[a] + idx[t][1] * w[a]);
            v[k++] = nn[0]; v[k++] = nn[1]; v[k++] = nn[2];
        }
    }
}

bool BuildFrameResources(int w, int h) {
    DFN(vkCreateRenderPass); DFN(vkCreateFramebuffer); DFN(vkCreateShaderModule);
    DFN(vkCreatePipelineLayout); DFN(vkCreateGraphicsPipelines);
    DFN(vkCreateCommandPool); DFN(vkAllocateCommandBuffers); DFN(vkCreateFence);
    DFN(vkDestroyImage); DFN(vkDestroyImageView); DFN(vkFreeMemory);
    DFN(vkDestroyFramebuffer); DFN(vkDestroyBuffer); DFN(vkUnmapMemory);

    if (g_vk.fb) {                       // resize: drop the old target
        vkDestroyFramebuffer(g_vk.dev, g_vk.fb, NULL);
        vkDestroyImageView(g_vk.dev, g_vk.colorView, NULL);
        vkDestroyImageView(g_vk.dev, g_vk.depthView, NULL);
        vkDestroyImage(g_vk.dev, g_vk.color, NULL);
        vkDestroyImage(g_vk.dev, g_vk.depth, NULL);
        vkFreeMemory(g_vk.dev, g_vk.colorMem, NULL);
        vkFreeMemory(g_vk.dev, g_vk.depthMem, NULL);
        for (int i = 0; i < 2; ++i) {
            vkUnmapMemory(g_vk.dev, g_vk.readMem[i]);
            vkDestroyBuffer(g_vk.dev, g_vk.readBuf[i], NULL);
            vkFreeMemory(g_vk.dev, g_vk.readMem[i], NULL);
            g_vk.submitted[i] = false;
        }
        g_vk.fb = VK_NULL_HANDLE;
    }

    if (!MakeImage(w, h, VK_FORMAT_R8G8B8A8_UNORM,
                   VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                   VK_IMAGE_ASPECT_COLOR_BIT, &g_vk.color, &g_vk.colorMem, &g_vk.colorView))
        return false;
    if (!MakeImage(w, h, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                   VK_IMAGE_ASPECT_DEPTH_BIT, &g_vk.depth, &g_vk.depthMem, &g_vk.depthView))
        return false;
    // Readback memory MUST be host-cached: the GL upload reads every byte
    // back on the CPU, and uncached (write-combined) reads of a 3.6MB frame
    // cost tens of ms. Fall back to plain coherent if no cached type exists.
    VkMemoryPropertyFlags readFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                                      VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
    bool haveCached = false;
    for (uint32_t i = 0; i < g_vk.memProps.memoryTypeCount; ++i)
        if ((g_vk.memProps.memoryTypes[i].propertyFlags & readFlags) == readFlags)
            haveCached = true;
    if (!haveCached)
        readFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    for (int i = 0; i < 2; ++i)
        if (!MakeBuffer((VkDeviceSize)w * h * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, readFlags,
                        &g_vk.readBuf[i], &g_vk.readMem[i], &g_vk.readMap[i]))
            return false;

    if (!g_vk.rp) {
        VkAttachmentDescription at[2] = {};
        at[0].format = VK_FORMAT_R8G8B8A8_UNORM;
        at[0].samples = VK_SAMPLE_COUNT_1_BIT;
        at[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        at[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        at[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        at[0].finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        at[1].format = VK_FORMAT_D32_SFLOAT;
        at[1].samples = VK_SAMPLE_COUNT_1_BIT;
        at[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        at[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        at[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        at[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        VkAttachmentReference cr = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        VkAttachmentReference dr = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
        VkSubpassDescription sp = {};
        sp.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sp.colorAttachmentCount = 1;
        sp.pColorAttachments = &cr;
        sp.pDepthStencilAttachment = &dr;
        VkRenderPassCreateInfo rpci = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
        rpci.attachmentCount = 2;
        rpci.pAttachments = at;
        rpci.subpassCount = 1;
        rpci.pSubpasses = &sp;
        if (vkCreateRenderPass(g_vk.dev, &rpci, NULL, &g_vk.rp) != VK_SUCCESS)
            return false;
    }

    VkImageView views[2] = { g_vk.colorView, g_vk.depthView };
    VkFramebufferCreateInfo fci = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
    fci.renderPass = g_vk.rp;
    fci.attachmentCount = 2;
    fci.pAttachments = views;
    fci.width = (uint32_t)w;
    fci.height = (uint32_t)h;
    fci.layers = 1;
    if (vkCreateFramebuffer(g_vk.dev, &fci, NULL, &g_vk.fb) != VK_SUCCESS)
        return false;

    if (!g_vk.pipe) {
        size_t vsSize, fsSize;
        void* vsCode = ReadSpv("assets/spv/box.vert.spv", &vsSize);
        void* fsCode = ReadSpv("assets/spv/box.frag.spv", &fsSize);
        if (!vsCode || !fsCode) {
            TraceLog(RL_LOG_WARNING, "rdr: vulkan executor missing assets/spv/box.*.spv");
            return false;
        }
        VkShaderModuleCreateInfo smci = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        smci.codeSize = vsSize;
        smci.pCode = (const uint32_t*)vsCode;
        VkShaderModule vs, fs;
        vkCreateShaderModule(g_vk.dev, &smci, NULL, &vs);
        smci.codeSize = fsSize;
        smci.pCode = (const uint32_t*)fsCode;
        vkCreateShaderModule(g_vk.dev, &smci, NULL, &fs);
        free(vsCode); free(fsCode);

        VkPushConstantRange pcr = { VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 80 };
        VkPipelineLayoutCreateInfo plci = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        plci.pushConstantRangeCount = 1;
        plci.pPushConstantRanges = &pcr;
        vkCreatePipelineLayout(g_vk.dev, &plci, NULL, &g_vk.layout);

        VkPipelineShaderStageCreateInfo stages[2] = {};
        stages[0].sType = stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vs;
        stages[0].pName = "main";
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fs;
        stages[1].pName = "main";
        VkVertexInputBindingDescription binds[2] = {
            { 0, 6 * sizeof(float), VK_VERTEX_INPUT_RATE_VERTEX },
            { 1, 80,                VK_VERTEX_INPUT_RATE_INSTANCE },
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
        VkPipelineViewportStateCreateInfo vps = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
        vps.viewportCount = 1;
        vps.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo rs = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
        rs.polygonMode = VK_POLYGON_MODE_FILL;
        rs.cullMode = VK_CULL_MODE_NONE;   // negative-viewport GL parity; cull later
        rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rs.lineWidth = 1.0f;
        VkPipelineMultisampleStateCreateInfo ms = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo dst = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
        dst.depthTestEnable = VK_TRUE;
        dst.depthWriteEnable = VK_TRUE;
        dst.depthCompareOp = VK_COMPARE_OP_LESS;
        VkPipelineColorBlendAttachmentState cba = {};
        cba.colorWriteMask = 0xF;
        VkPipelineColorBlendStateCreateInfo cb = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
        cb.attachmentCount = 1;
        cb.pAttachments = &cba;
        VkDynamicState dyn[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dsci = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
        dsci.dynamicStateCount = 2;
        dsci.pDynamicStates = dyn;
        VkGraphicsPipelineCreateInfo gpci = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
        gpci.stageCount = 2;
        gpci.pStages = stages;
        gpci.pVertexInputState = &vin;
        gpci.pInputAssemblyState = &ia;
        gpci.pViewportState = &vps;
        gpci.pRasterizationState = &rs;
        gpci.pMultisampleState = &ms;
        gpci.pDepthStencilState = &dst;
        gpci.pColorBlendState = &cb;
        gpci.pDynamicState = &dsci;
        gpci.layout = g_vk.layout;
        gpci.renderPass = g_vk.rp;
        if (vkCreateGraphicsPipelines(g_vk.dev, NULL, 1, &gpci, NULL, &g_vk.pipe) != VK_SUCCESS)
            return false;

        float cube[36 * 6];
        FillCube(cube);
        void* cubeMap = nullptr;
        if (!MakeBuffer(sizeof(cube), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                        &g_vk.cubeBuf, &g_vk.cubeMem, &cubeMap))
            return false;
        memcpy(cubeMap, cube, sizeof(cube));

        // Terrain pipeline (V164): mesh.vert + the same Lambert fragment,
        // but colour writes OFF â€” it exists to give the soldiers correct
        // hillside occlusion in the composite while GL still paints the
        // ground. Flipping colorWriteMask on is the whole-terrain switch.
        size_t mvsSize;
        void* mvsCode = ReadSpv("assets/spv/mesh.vert.spv", &mvsSize);
        if (mvsCode) {
            smci.codeSize = mvsSize;
            smci.pCode = (const uint32_t*)mvsCode;
            VkShaderModule mvsMod;
            vkCreateShaderModule(g_vk.dev, &smci, NULL, &mvsMod);
            free(mvsCode);
            VkPipelineShaderStageCreateInfo mst[2] = { stages[0], stages[1] };
            mst[0].module = mvsMod;
            VkVertexInputBindingDescription mb = { 0, 10 * sizeof(float),
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
            VkPipelineColorBlendAttachmentState mcba = {};
            mcba.colorWriteMask = 0;               // depth-only occluder
            VkPipelineColorBlendStateCreateInfo mcb = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
            mcb.attachmentCount = 1;
            mcb.pAttachments = &mcba;
            VkGraphicsPipelineCreateInfo mgp = gpci;
            mgp.pStages = mst;
            mgp.pVertexInputState = &mvin;
            mgp.pColorBlendState = &mcb;
            vkCreateGraphicsPipelines(g_vk.dev, NULL, 1, &mgp, NULL, &g_vk.meshPipe);
        }

        VkCommandPoolCreateInfo cpci = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
        cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        cpci.queueFamilyIndex = g_vk.qfam;
        vkCreateCommandPool(g_vk.dev, &cpci, NULL, &g_vk.pool);
        VkCommandBufferAllocateInfo cbai = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        cbai.commandPool = g_vk.pool;
        cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cbai.commandBufferCount = 2;
        vkAllocateCommandBuffers(g_vk.dev, &cbai, g_vk.cmd);
        VkFenceCreateInfo fenci = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        vkCreateFence(g_vk.dev, &fenci, NULL, &g_vk.fence[0]);
        vkCreateFence(g_vk.dev, &fenci, NULL, &g_vk.fence[1]);
    }

    g_vk.width = w;
    g_vk.height = h;
    return true;
}

bool EnsureInstCap(int slot, int count) {
    if (count <= g_vk.instCap[slot]) return true;
    DFN(vkDestroyBuffer); DFN(vkUnmapMemory); DFN(vkFreeMemory); DFN(vkDeviceWaitIdle);
    if (g_vk.instBuf[slot]) {
        vkDeviceWaitIdle(g_vk.dev);
        vkUnmapMemory(g_vk.dev, g_vk.instMem[slot]);
        vkDestroyBuffer(g_vk.dev, g_vk.instBuf[slot], NULL);
        vkFreeMemory(g_vk.dev, g_vk.instMem[slot], NULL);
        g_vk.instBuf[slot] = VK_NULL_HANDLE;
    }
    const int cap = count < 4096 ? 4096 : count * 2;
    if (!MakeBuffer((VkDeviceSize)cap * 80, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    &g_vk.instBuf[slot], &g_vk.instMem[slot], &g_vk.instMap[slot]))
        return false;
    g_vk.instCap[slot] = cap;
    return true;
}

}  // namespace

// V164: stage the battlefield mesh (10 floats/vert: pos3 nrm3 col4). Called
// at terrain bake time â€” possibly before the device exists, so the copy is
// staged and uploaded lazily on the next Vulkan frame.
void VulkanSetTerrain(const float* verts, int vertCount) {
    free(g_vk.terrPending);
    g_vk.terrPending = nullptr;
    g_vk.terrPendingCount = 0;
    if (!verts || vertCount <= 0) return;
    const size_t bytes = (size_t)vertCount * 10 * sizeof(float);
    g_vk.terrPending = (float*)malloc(bytes);
    memcpy(g_vk.terrPending, verts, bytes);
    g_vk.terrPendingCount = vertCount;
}

namespace {
void UploadPendingTerrain() {
    if (!g_vk.terrPending) return;
    DFN(vkDestroyBuffer); DFN(vkFreeMemory); DFN(vkDeviceWaitIdle); DFN(vkUnmapMemory);
    if (g_vk.terrBuf) {
        vkDeviceWaitIdle(g_vk.dev);
        vkDestroyBuffer(g_vk.dev, g_vk.terrBuf, NULL);
        vkFreeMemory(g_vk.dev, g_vk.terrMem, NULL);
        g_vk.terrBuf = VK_NULL_HANDLE;
    }
    void* map = nullptr;
    const size_t bytes = (size_t)g_vk.terrPendingCount * 10 * sizeof(float);
    if (MakeBuffer(bytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                   &g_vk.terrBuf, &g_vk.terrMem, &map)) {
        memcpy(map, g_vk.terrPending, bytes);
        vkUnmapMemory(g_vk.dev, g_vk.terrMem);
        g_vk.terrCount = g_vk.terrPendingCount;
    }
    free(g_vk.terrPending);
    g_vk.terrPending = nullptr;
    g_vk.terrPendingCount = 0;
}
}  // namespace

// Boots the device once (logged); true once the frame executor is usable.
bool VulkanExecutorReady() {
    if (!g_vk.tried && IsWindowReady()) {
        BootDevice();
        if (g_vk.live)
            TraceLog(RL_LOG_INFO,
                     "rdr: VULKAN DEVICE LIVE in-game on %s (queue family %u) - "
                     "frame executor migration continues per RENDERER.md",
                     g_vk.gpuName, g_vk.qfam);
        else
            TraceLog(RL_LOG_WARNING, "rdr: vulkan requested but device boot failed; GL fallback");
    }
    return g_vk.live && !g_vk.frameFailed;
}

// Renders `count` instances (80 bytes each: column-major mat4 + rgba floats)
// through the Vulkan box pipeline into an offscreen target and returns the
// RGBA pixels (w*h*4, row 0 = top), or null on failure or warm-up (caller falls back
// to GL). Pipelined readback (V166): the returned pixels are LAST frame's
// render - one frame of latency buys back the sync-wait stall â€” the present-interop stage; the native
// swapchain replaces it when the window itself moves to Vulkan.
const unsigned char* VulkanRenderFrame(const float* viewProj16, const float* sun4,
                                       const void* instData, int count,
                                       int w, int h) {
    if (!g_vk.live || g_vk.frameFailed || w <= 0 || h <= 0) return nullptr;
    if (w != g_vk.width || h != g_vk.height) {
        DFN(vkDeviceWaitIdle);
        if (g_vk.fb) vkDeviceWaitIdle(g_vk.dev);
        if (!BuildFrameResources(w, h)) {
            g_vk.frameFailed = true;
            TraceLog(RL_LOG_WARNING, "rdr: vulkan frame resources failed; GL fallback");
            return nullptr;
        }
        if (!g_vk.frameReady) {
            g_vk.frameReady = true;
            TraceLog(RL_LOG_INFO,
                     "rdr: VULKAN FRAME EXECUTOR LIVE - the army renders on %s",
                     g_vk.gpuName);
        }
    }
    UploadPendingTerrain();

    DFN(vkBeginCommandBuffer); DFN(vkCmdBeginRenderPass); DFN(vkCmdBindPipeline);
    DFN(vkCmdSetViewport); DFN(vkCmdSetScissor); DFN(vkCmdBindVertexBuffers);
    DFN(vkCmdPushConstants); DFN(vkCmdDraw); DFN(vkCmdEndRenderPass);
    DFN(vkCmdCopyImageToBuffer); DFN(vkEndCommandBuffer); DFN(vkQueueSubmit);
    DFN(vkWaitForFences); DFN(vkResetFences); DFN(vkResetCommandBuffer);

    // Slot rotation (V166): reclaim this slot (two frames old â€” the GPU is
    // long done), fill and submit it, then present the OTHER slot, whose
    // work was submitted last frame and has had a whole frame to finish.
    const int cur = g_vk.frame & 1;
    g_vk.frame++;
    if (g_vk.submitted[cur])
        vkWaitForFences(g_vk.dev, 1, &g_vk.fence[cur], VK_TRUE, UINT64_MAX);
    vkResetFences(g_vk.dev, 1, &g_vk.fence[cur]);
    g_vk.submitted[cur] = false;
    if (!EnsureInstCap(cur, count)) { g_vk.frameFailed = true; return nullptr; }
    if (count > 0) memcpy(g_vk.instMap[cur], instData, (size_t)count * 80);

    vkResetCommandBuffer(g_vk.cmd[cur], 0);
    VkCommandBufferBeginInfo bi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(g_vk.cmd[cur],&bi);
    VkClearValue clears[2];
    clears[0].color = { { 0, 0, 0, 0 } };            // transparent: GL composites
    clears[1].depthStencil = { 1.0f, 0 };
    VkRenderPassBeginInfo rbi = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    rbi.renderPass = g_vk.rp;
    rbi.framebuffer = g_vk.fb;
    rbi.renderArea = { { 0, 0 }, { (uint32_t)w, (uint32_t)h } };
    rbi.clearValueCount = 2;
    rbi.pClearValues = clears;
    vkCmdBeginRenderPass(g_vk.cmd[cur],&rbi, VK_SUBPASS_CONTENTS_INLINE);
    {
        // Negative-height viewport (core 1.1) keeps GL clip conventions, so
        // the game's matrices work unmodified and row 0 reads back as top.
        VkViewport vp = { 0, (float)h, (float)w, -(float)h, 0, 1 };
        VkRect2D sc = { { 0, 0 }, { (uint32_t)w, (uint32_t)h } };
        vkCmdSetViewport(g_vk.cmd[cur],0, 1, &vp);
        vkCmdSetScissor(g_vk.cmd[cur],0, 1, &sc);
        float pc[20];
        memcpy(pc, viewProj16, 64);
        memcpy(pc + 16, sun4, 16);
        vkCmdPushConstants(g_vk.cmd[cur],g_vk.layout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, 80, pc);
        if (g_vk.terrBuf && g_vk.meshPipe) {   // depth-only hillside occluder
            vkCmdBindPipeline(g_vk.cmd[cur],VK_PIPELINE_BIND_POINT_GRAPHICS, g_vk.meshPipe);
            VkDeviceSize toff = 0;
            vkCmdBindVertexBuffers(g_vk.cmd[cur],0, 1, &g_vk.terrBuf, &toff);
            vkCmdDraw(g_vk.cmd[cur],(uint32_t)g_vk.terrCount, 1, 0, 0);
        }
        if (count > 0) {
            vkCmdBindPipeline(g_vk.cmd[cur],VK_PIPELINE_BIND_POINT_GRAPHICS, g_vk.pipe);
            VkBuffer bufs[2] = { g_vk.cubeBuf, g_vk.instBuf[cur] };
            VkDeviceSize offs[2] = { 0, 0 };
            vkCmdBindVertexBuffers(g_vk.cmd[cur],0, 2, bufs, offs);
            vkCmdDraw(g_vk.cmd[cur],36, (uint32_t)count, 0, 0);
        }
    }
    vkCmdEndRenderPass(g_vk.cmd[cur]);
    VkBufferImageCopy region = {};
    region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.imageExtent = { (uint32_t)w, (uint32_t)h, 1 };
    vkCmdCopyImageToBuffer(g_vk.cmd[cur], g_vk.color, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           g_vk.readBuf[cur], 1, &region);
    vkEndCommandBuffer(g_vk.cmd[cur]);
    VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    si.commandBufferCount = 1;
    si.pCommandBuffers = &g_vk.cmd[cur];
    vkQueueSubmit(g_vk.queue, 1, &si, g_vk.fence[cur]);
    g_vk.submitted[cur] = true;
    // Present the other slot: its GPU work has had a full frame to land,
    // so this wait is normally instant â€” the pipelining win (V166).
    const int prev = cur ^ 1;
    if (!g_vk.submitted[prev]) return nullptr;   // very first frame: GL covers it
    vkWaitForFences(g_vk.dev, 1, &g_vk.fence[prev], VK_TRUE, UINT64_MAX);
    return (const unsigned char*)g_vk.readMap[prev];
}

}  // namespace rdr

#else
namespace rdr {
bool VulkanExecutorReady() { return false; }
void VulkanSetTerrain(const float*, int) {}
const unsigned char* VulkanRenderFrame(const float*, const float*, const void*,
                                       int, int, int) { return nullptr; }
}
#endif
