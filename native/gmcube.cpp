/* gmcube.cpp — Govnod Cube.
 *
 * What's left of a game engine when you remove everything: a spinning 3D cube.
 * Android + Vulkan only. No engine, no framework, no dependencies.
 *
 * SPDX-License-Identifier: MIT
 */

#include <android/log.h>
#include <android_native_app_glue.h>
#include <vulkan/vulkan.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "shaders.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "GovnodCube", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "GovnodCube", __VA_ARGS__)

// ---------------------------------------------------------------------------
// Small matrix math (column-major, Vulkan clip space: Y down, Z in [0, 1]).
// ---------------------------------------------------------------------------

typedef float Mat4[16];

static void mat4_identity(Mat4 m) {
	memset(m, 0, sizeof(Mat4));
	m[0] = 1.0f;
	m[5] = 1.0f;
	m[10] = 1.0f;
	m[15] = 1.0f;
}

// (A * B)[c][r] = sum_k A[k][r] * B[c][k]   (column-major)
static void mat4_mul(Mat4 out, const Mat4 a, const Mat4 b) {
	Mat4 res;
	for (int c = 0; c < 4; c++) {
		for (int r = 0; r < 4; r++) {
			float s = 0.0f;
			for (int k = 0; k < 4; k++) {
				s += a[k * 4 + r] * b[c * 4 + k];
			}
			res[c * 4 + r] = s;
		}
	}
	memcpy(out, res, sizeof(Mat4));
}

static void mat4_rotate_x(Mat4 m, float radians) {
	mat4_identity(m);
	float c = cosf(radians), s = sinf(radians);
	m[5] = c;
	m[6] = s;
	m[9] = -s;
	m[10] = c;
}

static void mat4_rotate_y(Mat4 m, float radians) {
	mat4_identity(m);
	float c = cosf(radians), s = sinf(radians);
	m[0] = c;
	m[2] = -s;
	m[8] = s;
	m[10] = c;
}

static void mat4_rotate_z(Mat4 m, float radians) {
	mat4_identity(m);
	float c = cosf(radians), s = sinf(radians);
	m[0] = c;
	m[1] = s;
	m[4] = -s;
	m[5] = c;
}

static void mat4_translate_z(Mat4 m, float z) {
	mat4_identity(m);
	m[14] = z;
}

// Vulkan-style perspective: Y flipped for the y-down NDC, Z mapped to [0, 1].
static void mat4_perspective(Mat4 m, float fovy, float aspect, float znear, float zfar) {
	memset(m, 0, sizeof(Mat4));
	const float ys = 1.0f / tanf(fovy * 0.5f);
	const float xs = ys / aspect;
	m[0] = xs;
	m[5] = -ys;
	m[10] = zfar / (znear - zfar);
	m[11] = -1.0f;
	m[14] = (znear * zfar) / (znear - zfar);
}

static double now_sec() {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

// ---------------------------------------------------------------------------
// Cube geometry: 24 vertices (position + color), 36 indices.
// ---------------------------------------------------------------------------

struct CubeVertex {
	float x, y, z;
	float r, g, b;
};

static const CubeVertex kCubeVertices[] = {
	// +Z (front, blue)
	{ -0.5f, -0.5f, 0.5f, 0.20f, 0.60f, 0.95f },
	{ 0.5f, -0.5f, 0.5f, 0.20f, 0.60f, 0.95f },
	{ 0.5f, 0.5f, 0.5f, 0.20f, 0.60f, 0.95f },
	{ -0.5f, 0.5f, 0.5f, 0.20f, 0.60f, 0.95f },
	// -Z (back, red)
	{ -0.5f, -0.5f, -0.5f, 0.95f, 0.35f, 0.40f },
	{ -0.5f, 0.5f, -0.5f, 0.95f, 0.35f, 0.40f },
	{ 0.5f, 0.5f, -0.5f, 0.95f, 0.35f, 0.40f },
	{ 0.5f, -0.5f, -0.5f, 0.95f, 0.35f, 0.40f },
	// +Y (top, yellow)
	{ -0.5f, 0.5f, -0.5f, 0.95f, 0.80f, 0.25f },
	{ -0.5f, 0.5f, 0.5f, 0.95f, 0.80f, 0.25f },
	{ 0.5f, 0.5f, 0.5f, 0.95f, 0.80f, 0.25f },
	{ 0.5f, 0.5f, -0.5f, 0.95f, 0.80f, 0.25f },
	// -Y (bottom, green)
	{ -0.5f, -0.5f, -0.5f, 0.30f, 0.75f, 0.35f },
	{ 0.5f, -0.5f, -0.5f, 0.30f, 0.75f, 0.35f },
	{ 0.5f, -0.5f, 0.5f, 0.30f, 0.75f, 0.35f },
	{ -0.5f, -0.5f, 0.5f, 0.30f, 0.75f, 0.35f },
	// +X (right, purple)
	{ 0.5f, -0.5f, -0.5f, 0.75f, 0.50f, 0.95f },
	{ 0.5f, 0.5f, -0.5f, 0.75f, 0.50f, 0.95f },
	{ 0.5f, 0.5f, 0.5f, 0.75f, 0.50f, 0.95f },
	{ 0.5f, -0.5f, 0.5f, 0.75f, 0.50f, 0.95f },
	// -X (left, teal)
	{ -0.5f, -0.5f, -0.5f, 0.25f, 0.80f, 0.75f },
	{ -0.5f, -0.5f, 0.5f, 0.25f, 0.80f, 0.75f },
	{ -0.5f, 0.5f, 0.5f, 0.25f, 0.80f, 0.75f },
	{ -0.5f, 0.5f, -0.5f, 0.25f, 0.80f, 0.75f },
};

static const uint16_t kCubeIndices[] = {
	0, 1, 2, 0, 2, 3, // +Z
	4, 5, 6, 4, 6, 7, // -Z
	8, 9, 10, 8, 10, 11, // +Y
	12, 13, 14, 12, 14, 15, // -Y
	16, 17, 18, 16, 18, 19, // +X
	20, 21, 22, 20, 22, 23, // -X
};

static const uint32_t kIndexCount = sizeof(kCubeIndices) / sizeof(kCubeIndices[0]);

// ---------------------------------------------------------------------------
// Vulkan app state.
// ---------------------------------------------------------------------------

struct CubeApp {
	struct android_app *app = nullptr;
	bool vulkan_up = false; // instance + device + surface + pipeline
	bool swapchain_up = false; // swapchain + framebuffers + command buffers
	bool quit_after_cleanup = false;

	VkInstance instance = VK_NULL_HANDLE;
	VkSurfaceKHR surface = VK_NULL_HANDLE;
	VkPhysicalDevice physical_device = VK_NULL_HANDLE;
	uint32_t queue_family = 0;
	VkDevice device = VK_NULL_HANDLE;
	VkQueue queue = VK_NULL_HANDLE;

	VkRenderPass render_pass = VK_NULL_HANDLE;
	VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
	VkPipeline pipeline = VK_NULL_HANDLE;

	VkBuffer vertex_buffer = VK_NULL_HANDLE;
	VkBuffer index_buffer = VK_NULL_HANDLE;
	VkDeviceMemory vertex_memory = VK_NULL_HANDLE;
	VkDeviceMemory index_memory = VK_NULL_HANDLE;

	VkSwapchainKHR swapchain = VK_NULL_HANDLE;
	VkFormat surface_format = VK_FORMAT_UNDEFINED;
	VkColorSpaceKHR color_space = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
	VkFormat depth_format = VK_FORMAT_UNDEFINED;
	VkExtent2D extent = {};
	VkImage depth_image = VK_NULL_HANDLE;
	VkDeviceMemory depth_memory = VK_NULL_HANDLE;
	VkImageView depth_view = VK_NULL_HANDLE;

	VkImageView *image_views = nullptr;
	VkFramebuffer *framebuffers = nullptr;
	VkCommandBuffer *command_buffers = nullptr;
	uint32_t image_count = 0;

	VkSemaphore acquire_semaphore = VK_NULL_HANDLE;
	VkSemaphore render_semaphore = VK_NULL_HANDLE;
	VkFence frame_fence = VK_NULL_HANDLE;

	VkCommandPool command_pool = VK_NULL_HANDLE;

	double start_time = 0.0;

	// ---- helpers ----

	uint32_t find_memory_type(uint32_t type_bits, VkMemoryPropertyFlags props) {
		VkPhysicalDeviceMemoryProperties mem;
		vkGetPhysicalDeviceMemoryProperties(physical_device, &mem);
		for (uint32_t i = 0; i < mem.memoryTypeCount; i++) {
			if ((type_bits & (1u << i)) && (mem.memoryTypes[i].propertyFlags & props) == props) {
				return i;
			}
		}
		return 0xFFFFFFFFu;
	}

	bool create_buffer(VkBuffer *p_buffer, VkDeviceMemory *p_memory, VkDeviceSize size, const void *data) {
		VkBufferCreateInfo buf_info = {};
		buf_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		buf_info.size = size;
		buf_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
		buf_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		if (vkCreateBuffer(device, &buf_info, nullptr, p_buffer) != VK_SUCCESS) {
			LOGE("vkCreateBuffer failed");
			return false;
		}

		VkMemoryRequirements reqs = {};
		vkGetBufferMemoryRequirements(device, *p_buffer, &reqs);

		VkMemoryAllocateInfo alloc_info = {};
		alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		alloc_info.allocationSize = reqs.size;
		alloc_info.memoryTypeIndex = find_memory_type(reqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		if (alloc_info.memoryTypeIndex == 0xFFFFFFFFu) {
			LOGE("no host-visible memory type");
			return false;
		}

		if (vkAllocateMemory(device, &alloc_info, nullptr, p_memory) != VK_SUCCESS) {
			LOGE("vkAllocateMemory failed");
			return false;
		}
		if (vkBindBufferMemory(device, *p_buffer, *p_memory, 0) != VK_SUCCESS) {
			LOGE("vkBindBufferMemory failed");
			return false;
		}

		void *mapped = nullptr;
		if (vkMapMemory(device, *p_memory, 0, size, 0, &mapped) != VK_SUCCESS) {
			LOGE("vkMapMemory failed");
			return false;
		}
		memcpy(mapped, data, (size_t)size);
		vkUnmapMemory(device, *p_memory);
		return true;
	}

	// ---- base init: instance, surface, device, pipeline, vertex data ----

	bool init_vulkan() {
		VkApplicationInfo app_info = {};
		app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		app_info.pApplicationName = "Govnod Cube";
		app_info.applicationVersion = 1;
		app_info.pEngineName = "none";
		app_info.engineVersion = 1;
		app_info.apiVersion = VK_API_VERSION_1_0;

		const char *instance_extensions[] = { VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_ANDROID_SURFACE_EXTENSION_NAME };

		VkInstanceCreateInfo create_info = {};
		create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		create_info.pApplicationInfo = &app_info;
		create_info.enabledExtensionCount = 2;
		create_info.ppEnabledExtensionNames = instance_extensions;

		if (vkCreateInstance(&create_info, nullptr, &instance) != VK_SUCCESS) {
			LOGE("vkCreateInstance failed");
			return false;
		}

		PFN_vkCreateAndroidSurfaceKHR vkCreateAndroidSurfaceKHR =
				(PFN_vkCreateAndroidSurfaceKHR)vkGetInstanceProcAddr(instance, "vkCreateAndroidSurfaceKHR");
		if (vkCreateAndroidSurfaceKHR == nullptr) {
			LOGE("vkCreateAndroidSurfaceKHR not available");
			return false;
		}

		VkAndroidSurfaceCreateInfoKHR surface_info = {};
		surface_info.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
		surface_info.window = app->window;
		if (vkCreateAndroidSurfaceKHR(instance, &surface_info, nullptr, &surface) != VK_SUCCESS) {
			LOGE("vkCreateAndroidSurfaceKHR failed");
			return false;
		}

		uint32_t device_count = 0;
		vkEnumeratePhysicalDevices(instance, &device_count, nullptr);
		if (device_count == 0) {
			LOGE("no Vulkan devices");
			return false;
		}
		VkPhysicalDevice *devices = (VkPhysicalDevice *)malloc(sizeof(VkPhysicalDevice) * device_count);
		vkEnumeratePhysicalDevices(instance, &device_count, devices);

		bool found = false;
		for (uint32_t d = 0; d < device_count && !found; d++) {
			uint32_t family_count = 0;
			vkGetPhysicalDeviceQueueFamilyProperties(devices[d], &family_count, nullptr);
			VkQueueFamilyProperties *families = (VkQueueFamilyProperties *)malloc(sizeof(VkQueueFamilyProperties) * family_count);
			vkGetPhysicalDeviceQueueFamilyProperties(devices[d], &family_count, families);
			for (uint32_t f = 0; f < family_count; f++) {
				VkBool32 present = VK_FALSE;
				vkGetPhysicalDeviceSurfaceSupportKHR(devices[d], f, surface, &present);
				if ((families[f].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) {
					physical_device = devices[d];
					queue_family = f;
					found = true;
					break;
				}
			}
			free(families);
		}
		free(devices);
		if (!found) {
			LOGE("no queue family with graphics + present");
			return false;
		}

		const float priority = 1.0f;
		VkDeviceQueueCreateInfo queue_info = {};
		queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queue_info.queueFamilyIndex = queue_family;
		queue_info.queueCount = 1;
		queue_info.pQueuePriorities = &priority;

		const char *device_extensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

		VkDeviceCreateInfo device_info = {};
		device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		device_info.queueCreateInfoCount = 1;
		device_info.pQueueCreateInfos = &queue_info;
		device_info.enabledExtensionCount = 1;
		device_info.ppEnabledExtensionNames = device_extensions;

		if (vkCreateDevice(physical_device, &device_info, nullptr, &device) != VK_SUCCESS) {
			LOGE("vkCreateDevice failed");
			return false;
		}
		vkGetDeviceQueue(device, queue_family, 0, &queue);

		if (!create_render_pass() || !create_pipeline() || !create_geometry() || !create_sync_objects()) {
			return false;
		}

		LOGI("Vulkan initialized");
		return true;
	}

	bool create_render_pass() {
		VkSurfaceFormatKHR chosen = {};
		uint32_t format_count = 0;
		vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &format_count, nullptr);
		VkSurfaceFormatKHR *formats = (VkSurfaceFormatKHR *)malloc(sizeof(VkSurfaceFormatKHR) * format_count);
		vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &format_count, formats);
		chosen = formats[0];
		for (uint32_t i = 0; i < format_count; i++) {
			if (formats[i].format == VK_FORMAT_R8G8B8A8_UNORM || formats[i].format == VK_FORMAT_B8G8R8A8_UNORM) {
				chosen = formats[i];
				break;
			}
		}
		free(formats);
		surface_format = chosen.format;
		color_space = chosen.colorSpace;

		// Depth format candidates.
		const VkFormat depth_candidates[] = { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D16_UNORM };
		depth_format = VK_FORMAT_UNDEFINED;
		for (size_t i = 0; i < sizeof(depth_candidates) / sizeof(depth_candidates[0]); i++) {
			VkFormatProperties props = {};
			vkGetPhysicalDeviceFormatProperties(physical_device, depth_candidates[i], &props);
			if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
				depth_format = depth_candidates[i];
				break;
			}
		}

		VkAttachmentDescription attachments[2] = {};
		attachments[0].format = surface_format;
		attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
		attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

		attachments[1].format = depth_format;
		attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
		attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkAttachmentReference color_ref = {};
		color_ref.attachment = 0;
		color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		VkAttachmentReference depth_ref = {};
		depth_ref.attachment = 1;
		depth_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkSubpassDescription subpass = {};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &color_ref;
		subpass.pDepthStencilAttachment = &depth_ref;

		VkSubpassDependency dependency = {};
		dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
		dependency.dstSubpass = 0;
		dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependency.srcAccessMask = 0;
		dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

		VkRenderPassCreateInfo rp_info = {};
		rp_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		rp_info.attachmentCount = 2;
		rp_info.pAttachments = attachments;
		rp_info.subpassCount = 1;
		rp_info.pSubpasses = &subpass;
		rp_info.dependencyCount = 1;
		rp_info.pDependencies = &dependency;

		if (vkCreateRenderPass(device, &rp_info, nullptr, &render_pass) != VK_SUCCESS) {
			LOGE("vkCreateRenderPass failed");
			return false;
		}
		return true;
	}

	bool create_pipeline() {
		VkPushConstantRange push_range = {};
		push_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
		push_range.offset = 0;
		push_range.size = 64;

		VkPipelineLayoutCreateInfo layout_info = {};
		layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		layout_info.pushConstantRangeCount = 1;
		layout_info.pPushConstantRanges = &push_range;

		if (vkCreatePipelineLayout(device, &layout_info, nullptr, &pipeline_layout) != VK_SUCCESS) {
			LOGE("vkCreatePipelineLayout failed");
			return false;
		}

		VkPipelineShaderStageCreateInfo stages[2] = {};
		stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
		stages[0].module = VK_NULL_HANDLE;
		stages[0].pName = "main";
		stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		stages[1].module = VK_NULL_HANDLE;
		stages[1].pName = "main";

		VkShaderModuleCreateInfo module_info = {};
		module_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;

		VkShaderModule vert_module = VK_NULL_HANDLE;
		VkShaderModule frag_module = VK_NULL_HANDLE;

		module_info.codeSize = sizeof(cube_vert_spv);
		module_info.pCode = cube_vert_spv;
		if (vkCreateShaderModule(device, &module_info, nullptr, &vert_module) != VK_SUCCESS) {
			LOGE("vkCreateShaderModule (vertex) failed");
			return false;
		}
		module_info.codeSize = sizeof(cube_frag_spv);
		module_info.pCode = cube_frag_spv;
		if (vkCreateShaderModule(device, &module_info, nullptr, &frag_module) != VK_SUCCESS) {
			LOGE("vkCreateShaderModule (fragment) failed");
			vkDestroyShaderModule(device, vert_module, nullptr);
			return false;
		}
		stages[0].module = vert_module;
		stages[1].module = frag_module;

		VkVertexInputBindingDescription binding = {};
		binding.binding = 0;
		binding.stride = sizeof(CubeVertex);
		binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

		VkVertexInputAttributeDescription attributes[2] = {};
		attributes[0].location = 0;
		attributes[0].binding = 0;
		attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
		attributes[0].offset = 0;
		attributes[1].location = 1;
		attributes[1].binding = 0;
		attributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
		attributes[1].offset = 12;

		VkPipelineVertexInputStateCreateInfo vertex_input = {};
		vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		vertex_input.vertexBindingDescriptionCount = 1;
		vertex_input.pVertexBindingDescriptions = &binding;
		vertex_input.vertexAttributeDescriptionCount = 2;
		vertex_input.pVertexAttributeDescriptions = attributes;

		VkPipelineInputAssemblyStateCreateInfo input_assembly = {};
		input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

		VkPipelineViewportStateCreateInfo viewport_state = {};
		viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		viewport_state.viewportCount = 1;
		viewport_state.scissorCount = 1;

		VkPipelineRasterizationStateCreateInfo rasterizer = {};
		rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
		rasterizer.cullMode = VK_CULL_MODE_NONE;
		rasterizer.lineWidth = 1.0f;

		VkPipelineMultisampleStateCreateInfo multisampling = {};
		multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

		VkPipelineDepthStencilStateCreateInfo depth_stencil = {};
		depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		depth_stencil.depthTestEnable = VK_TRUE;
		depth_stencil.depthWriteEnable = VK_TRUE;
		depth_stencil.depthCompareOp = VK_COMPARE_OP_LESS;

		VkPipelineColorBlendAttachmentState blend_attachment = {};
		blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

		VkPipelineColorBlendStateCreateInfo color_blend = {};
		color_blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		color_blend.attachmentCount = 1;
		color_blend.pAttachments = &blend_attachment;

		VkDynamicState dynamic_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

		VkPipelineDynamicStateCreateInfo dynamic_state = {};
		dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		dynamic_state.dynamicStateCount = 2;
		dynamic_state.pDynamicStates = dynamic_states;

		VkGraphicsPipelineCreateInfo pipeline_info = {};
		pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipeline_info.stageCount = 2;
		pipeline_info.pStages = stages;
		pipeline_info.pVertexInputState = &vertex_input;
		pipeline_info.pInputAssemblyState = &input_assembly;
		pipeline_info.pViewportState = &viewport_state;
		pipeline_info.pRasterizationState = &rasterizer;
		pipeline_info.pMultisampleState = &multisampling;
		pipeline_info.pDepthStencilState = &depth_stencil;
		pipeline_info.pColorBlendState = &color_blend;
		pipeline_info.pDynamicState = &dynamic_state;
		pipeline_info.layout = pipeline_layout;
		pipeline_info.renderPass = render_pass;
		pipeline_info.subpass = 0;

		VkResult res = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline);
		vkDestroyShaderModule(device, vert_module, nullptr);
		vkDestroyShaderModule(device, frag_module, nullptr);
		if (res != VK_SUCCESS) {
			LOGE("vkCreateGraphicsPipelines failed");
			return false;
		}
		return true;
	}

	bool create_geometry() {
		if (!create_buffer(&vertex_buffer, &vertex_memory, sizeof(kCubeVertices), kCubeVertices)) {
			return false;
		}
		if (!create_buffer(&index_buffer, &index_memory, sizeof(kCubeIndices), kCubeIndices)) {
			return false;
		}

		VkCommandPoolCreateInfo pool_info = {};
		pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		pool_info.queueFamilyIndex = queue_family;
		if (vkCreateCommandPool(device, &pool_info, nullptr, &command_pool) != VK_SUCCESS) {
			LOGE("vkCreateCommandPool failed");
			return false;
		}
		return true;
	}

	bool create_sync_objects() {
		VkSemaphoreCreateInfo sem_info = {};
		sem_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		VkFenceCreateInfo fence_info = {};
		fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

		if (vkCreateSemaphore(device, &sem_info, nullptr, &acquire_semaphore) != VK_SUCCESS ||
				vkCreateSemaphore(device, &sem_info, nullptr, &render_semaphore) != VK_SUCCESS ||
				vkCreateFence(device, &fence_info, nullptr, &frame_fence) != VK_SUCCESS) {
			LOGE("failed to create sync objects");
			return false;
		}
		return true;
	}

	// ---- swapchain-dependent objects ----

	bool init_swapchain() {
		VkSurfaceCapabilitiesKHR caps = {};
		if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface, &caps) != VK_SUCCESS) {
			LOGE("vkGetPhysicalDeviceSurfaceCapabilitiesKHR failed");
			return false;
		}

		VkExtent2D swap_extent = caps.currentExtent;
		if (swap_extent.width == 0xFFFFFFFFu || swap_extent.height == 0xFFFFFFFFu || swap_extent.width == 0 || swap_extent.height == 0) {
			swap_extent.width = (uint32_t)ANativeWindow_getWidth(app->window);
			swap_extent.height = (uint32_t)ANativeWindow_getHeight(app->window);
		}
		if (swap_extent.width == 0 || swap_extent.height == 0) {
			return false; // no drawable surface yet
		}

		uint32_t image_count = caps.minImageCount;
		if (caps.maxImageCount > 0 && image_count > caps.maxImageCount) {
			image_count = caps.maxImageCount;
		}

		VkCompositeAlphaFlagBitsKHR composite_alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		if (!(caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)) {
			composite_alpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
		}

		VkSwapchainCreateInfoKHR swap_info = {};
		swap_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
		swap_info.surface = surface;
		swap_info.minImageCount = image_count;
		swap_info.imageFormat = surface_format;
		swap_info.imageColorSpace = color_space;
		swap_info.imageExtent = swap_extent;
		swap_info.imageArrayLayers = 1;
		swap_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		swap_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		swap_info.preTransform = caps.currentTransform;
		swap_info.compositeAlpha = composite_alpha;
		swap_info.clipped = VK_TRUE;
		swap_info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
		swap_info.oldSwapchain = VK_NULL_HANDLE;

		if (vkCreateSwapchainKHR(device, &swap_info, nullptr, &swapchain) != VK_SUCCESS) {
			LOGE("vkCreateSwapchainKHR failed");
			return false;
		}
		extent = swap_extent;

		vkGetSwapchainImagesKHR(device, swapchain, &this->image_count, nullptr);
		VkImage *images = (VkImage *)malloc(sizeof(VkImage) * this->image_count);
		vkGetSwapchainImagesKHR(device, swapchain, &this->image_count, images);

		image_views = (VkImageView *)calloc(this->image_count, sizeof(VkImageView));
		framebuffers = (VkFramebuffer *)calloc(this->image_count, sizeof(VkFramebuffer));
		command_buffers = (VkCommandBuffer *)calloc(this->image_count, sizeof(VkCommandBuffer));

		VkImageViewCreateInfo view_info = {};
		view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
		view_info.format = surface_format;
		view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		view_info.subresourceRange.levelCount = 1;
		view_info.subresourceRange.layerCount = 1;

		for (uint32_t i = 0; i < this->image_count; i++) {
			view_info.image = images[i];
			if (vkCreateImageView(device, &view_info, nullptr, &image_views[i]) != VK_SUCCESS) {
				LOGE("vkCreateImageView failed");
				free(images);
				return false;
			}
		}
		free(images);

		// Depth image.
		VkImageCreateInfo depth_info = {};
		depth_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		depth_info.imageType = VK_IMAGE_TYPE_2D;
		depth_info.format = depth_format;
		depth_info.extent = { swap_extent.width, swap_extent.height, 1 };
		depth_info.mipLevels = 1;
		depth_info.arrayLayers = 1;
		depth_info.samples = VK_SAMPLE_COUNT_1_BIT;
		depth_info.tiling = VK_IMAGE_TILING_OPTIMAL;
		depth_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

		if (vkCreateImage(device, &depth_info, nullptr, &depth_image) != VK_SUCCESS) {
			LOGE("vkCreateImage (depth) failed");
			return false;
		}

		VkMemoryRequirements reqs = {};
		vkGetImageMemoryRequirements(device, depth_image, &reqs);
		VkMemoryAllocateInfo depth_alloc = {};
		depth_alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		depth_alloc.allocationSize = reqs.size;
		depth_alloc.memoryTypeIndex = find_memory_type(reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		if (depth_alloc.memoryTypeIndex == 0xFFFFFFFFu ||
				vkAllocateMemory(device, &depth_alloc, nullptr, &depth_memory) != VK_SUCCESS ||
				vkBindImageMemory(device, depth_image, depth_memory, 0) != VK_SUCCESS) {
			LOGE("depth image allocation failed");
			return false;
		}

		VkImageViewCreateInfo depth_view_info = {};
		depth_view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		depth_view_info.image = depth_image;
		depth_view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
		depth_view_info.format = depth_info.format;
		VkImageAspectFlags depth_aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
		if (depth_format == VK_FORMAT_D24_UNORM_S8_UINT) {
			depth_aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
		}
		depth_view_info.subresourceRange.aspectMask = depth_aspect;
		depth_view_info.subresourceRange.levelCount = 1;
		depth_view_info.subresourceRange.layerCount = 1;
		if (vkCreateImageView(device, &depth_view_info, nullptr, &depth_view) != VK_SUCCESS) {
			LOGE("vkCreateImageView (depth) failed");
			return false;
		}

		VkImageView fb_attachments[2] = {};
		VkFramebufferCreateInfo fb_info = {};
		fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		fb_info.renderPass = render_pass;
		fb_info.attachmentCount = 2;
		fb_info.pAttachments = fb_attachments;
		fb_info.width = swap_extent.width;
		fb_info.height = swap_extent.height;
		fb_info.layers = 1;

		for (uint32_t i = 0; i < this->image_count; i++) {
			fb_attachments[0] = image_views[i];
			fb_attachments[1] = depth_view;
			if (vkCreateFramebuffer(device, &fb_info, nullptr, &framebuffers[i]) != VK_SUCCESS) {
				LOGE("vkCreateFramebuffer failed");
				return false;
			}
		}

		VkCommandBufferAllocateInfo cmd_info = {};
		cmd_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		cmd_info.commandPool = command_pool;
		cmd_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		cmd_info.commandBufferCount = this->image_count;
		if (vkAllocateCommandBuffers(device, &cmd_info, command_buffers) != VK_SUCCESS) {
			LOGE("vkAllocateCommandBuffers failed");
			return false;
		}

		start_time = now_sec();
		LOGI("swapchain ready: %ux%u, %u images", extent.width, extent.height, this->image_count);
		return true;
	}

	void destroy_swapchain() {
		if (device == VK_NULL_HANDLE) {
			return;
		}
		vkDeviceWaitIdle(device);
		if (command_buffers) {
			vkFreeCommandBuffers(device, command_pool, image_count, command_buffers);
		}
		for (uint32_t i = 0; i < image_count; i++) {
			if (framebuffers && framebuffers[i] != VK_NULL_HANDLE) {
				vkDestroyFramebuffer(device, framebuffers[i], nullptr);
			}
			if (image_views && image_views[i] != VK_NULL_HANDLE) {
				vkDestroyImageView(device, image_views[i], nullptr);
			}
		}
		free(command_buffers);
		free(framebuffers);
		free(image_views);
		command_buffers = nullptr;
		framebuffers = nullptr;
		image_views = nullptr;
		image_count = 0;
		if (depth_view != VK_NULL_HANDLE) {
			vkDestroyImageView(device, depth_view, nullptr);
			depth_view = VK_NULL_HANDLE;
		}
		if (depth_image != VK_NULL_HANDLE) {
			vkDestroyImage(device, depth_image, nullptr);
			depth_image = VK_NULL_HANDLE;
		}
		if (depth_memory != VK_NULL_HANDLE) {
			vkFreeMemory(device, depth_memory, nullptr);
			depth_memory = VK_NULL_HANDLE;
		}
		if (swapchain != VK_NULL_HANDLE) {
			vkDestroySwapchainKHR(device, swapchain, nullptr);
			swapchain = VK_NULL_HANDLE;
		}
		swapchain_up = false;
	}

	void destroy_vulkan() {
		if (device != VK_NULL_HANDLE) {
			destroy_swapchain();
			vkDeviceWaitIdle(device);
			if (acquire_semaphore != VK_NULL_HANDLE) {
				vkDestroySemaphore(device, acquire_semaphore, nullptr);
			}
			if (render_semaphore != VK_NULL_HANDLE) {
				vkDestroySemaphore(device, render_semaphore, nullptr);
			}
			if (frame_fence != VK_NULL_HANDLE) {
				vkDestroyFence(device, frame_fence, nullptr);
			}
			if (command_pool != VK_NULL_HANDLE) {
				vkDestroyCommandPool(device, command_pool, nullptr);
			}
			if (pipeline != VK_NULL_HANDLE) {
				vkDestroyPipeline(device, pipeline, nullptr);
			}
			if (pipeline_layout != VK_NULL_HANDLE) {
				vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
			}
			if (vertex_buffer != VK_NULL_HANDLE) {
				vkDestroyBuffer(device, vertex_buffer, nullptr);
			}
			if (index_buffer != VK_NULL_HANDLE) {
				vkDestroyBuffer(device, index_buffer, nullptr);
			}
			if (vertex_memory != VK_NULL_HANDLE) {
				vkFreeMemory(device, vertex_memory, nullptr);
			}
			if (index_memory != VK_NULL_HANDLE) {
				vkFreeMemory(device, index_memory, nullptr);
			}
			if (render_pass != VK_NULL_HANDLE) {
				vkDestroyRenderPass(device, render_pass, nullptr);
			}
			vkDestroyDevice(device, nullptr);
			device = VK_NULL_HANDLE;
		}
		if (surface != VK_NULL_HANDLE) {
			vkDestroySurfaceKHR(instance, surface, nullptr);
			surface = VK_NULL_HANDLE;
		}
		if (instance != VK_NULL_HANDLE) {
			vkDestroyInstance(instance, nullptr);
			instance = VK_NULL_HANDLE;
		}
		vulkan_up = false;
	}

	// ---- frame ----

	void draw_frame() {
		VkResult res = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, acquire_semaphore, frame_fence, &frame_index);
		if (res == VK_ERROR_OUT_OF_DATE_KHR) {
			recreate_swapchain();
			return;
		}
		if (res != VK_SUCCESS && res != VK_SUBOPTIMAL_KHR) {
			return;
		}

		vkWaitForFences(device, 1, &frame_fence, VK_TRUE, UINT64_MAX);
		vkResetFences(device, 1, &frame_fence);
		vkResetCommandBuffer(command_buffers[frame_index], 0);

		VkCommandBuffer cmd = command_buffers[frame_index];
		VkCommandBufferBeginInfo begin_info = {};
		begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		vkBeginCommandBuffer(cmd, &begin_info);

		VkClearValue clear_values[2] = {};
		clear_values[0].color = { { 0.043f, 0.055f, 0.075f, 1.0f } };
		clear_values[1].depthStencil = { 1.0f, 0 };

		VkRenderPassBeginInfo rp_begin = {};
		rp_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		rp_begin.renderPass = render_pass;
		rp_begin.framebuffer = framebuffers[frame_index];
		rp_begin.renderArea.extent = extent;
		rp_begin.clearValueCount = 2;
		rp_begin.pClearValues = clear_values;

		vkCmdBeginRenderPass(cmd, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

		VkViewport viewport = {};
		viewport.width = (float)extent.width;
		viewport.height = (float)extent.height;
		viewport.maxDepth = 1.0f;
		vkCmdSetViewport(cmd, 0, 1, &viewport);

		VkRect2D scissor = {};
		scissor.extent = extent;
		vkCmdSetScissor(cmd, 0, 1, &scissor);

		VkDeviceSize offset = 0;
		vkCmdBindVertexBuffers(cmd, 0, 1, &vertex_buffer, &offset);
		vkCmdBindIndexBuffer(cmd, index_buffer, 0, VK_INDEX_TYPE_UINT16);

		Mat4 mvp = {};
		build_mvp(mvp);
		vkCmdPushConstants(cmd, pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Mat4), mvp);

		vkCmdDrawIndexed(cmd, kIndexCount, 1, 0, 0, 0);
		vkCmdEndRenderPass(cmd);
		vkEndCommandBuffer(cmd);

		VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		VkSubmitInfo submit_info = {};
		submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submit_info.waitSemaphoreCount = 1;
		submit_info.pWaitSemaphores = &acquire_semaphore;
		submit_info.pWaitDstStageMask = &wait_stage;
		submit_info.commandBufferCount = 1;
		submit_info.pCommandBuffers = &cmd;
		submit_info.signalSemaphoreCount = 1;
		submit_info.pSignalSemaphores = &render_semaphore;

		if (vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE) != VK_SUCCESS) {
			LOGE("vkQueueSubmit failed");
			return;
		}

		VkPresentInfoKHR present_info = {};
		present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		present_info.waitSemaphoreCount = 1;
		present_info.pWaitSemaphores = &render_semaphore;
		present_info.swapchainCount = 1;
		present_info.pSwapchains = &swapchain;
		present_info.pImageIndices = &frame_index;

		res = vkQueuePresentKHR(queue, &present_info);
		if (res == VK_ERROR_OUT_OF_DATE_KHR) {
			recreate_swapchain();
		}
	}

	uint32_t frame_index = 0;

	void recreate_swapchain() {
		destroy_swapchain();
		swapchain_up = init_swapchain();
	}

	void build_mvp(Mat4 out) {
		const double t = now_sec() - start_time;
		const float ry = (float)(t * 0.8);
		const float rx = (float)(t * 0.35);

		Mat4 model, rot_y, rot_x, view, proj, pre, tmp;
		mat4_rotate_y(rot_y, ry);
		mat4_rotate_x(rot_x, rx);
		mat4_mul(model, rot_y, rot_x);
		mat4_translate_z(view, -3.5f);

		VkSurfaceCapabilitiesKHR caps = {};
		vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface, &caps);
		float aspect = (float)extent.width / (float)(extent.height ? extent.height : 1);
		mat4_perspective(proj, 50.0f * (float)M_PI / 180.0f, aspect, 0.1f, 100.0f);

		// Compensate the surface pre-transform so the cube is upright.
		mat4_identity(pre);
		switch (caps.currentTransform) {
			case VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR:
				mat4_rotate_z(pre, (float)(-M_PI / 2.0));
				break;
			case VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR:
				mat4_rotate_z(pre, (float)M_PI);
				break;
			case VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR:
				mat4_rotate_z(pre, (float)(M_PI / 2.0));
				break;
			default:
				break;
		}

		mat4_mul(tmp, proj, pre);
		Mat4 view_model;
		mat4_mul(view_model, view, model);
		mat4_mul(out, tmp, view_model);
	}
};

static void cube_handle_cmd(struct android_app *app, int32_t cmd) {
	CubeApp *cube = (CubeApp *)app->userData;
	switch (cmd) {
		case APP_CMD_INIT_WINDOW: {
			if (app->window != nullptr) {
				if (!cube->vulkan_up) {
					cube->vulkan_up = cube->init_vulkan();
				}
				if (cube->vulkan_up) {
					cube->swapchain_up = cube->init_swapchain();
				}
			}
			break;
		}
		case APP_CMD_TERM_WINDOW: {
			cube->destroy_swapchain();
			break;
		}
		case APP_CMD_DESTROY: {
			cube->destroy_vulkan();
			break;
		}
		default:
			break;
	}
}

static int32_t cube_handle_input(struct android_app *app, AInputEvent *event) {
	if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_KEY &&
			AKeyEvent_getAction(event) == AKEY_EVENT_ACTION_DOWN &&
			AKeyEvent_getKeyCode(event) == AKEYCODE_BACK) {
		ANativeActivity_finish(app->activity);
		return 1;
	}
	return 0;
}

void android_main(struct android_app *app) {
	CubeApp cube;
	cube.app = app;
	app->userData = &cube;
	app->onAppCmd = cube_handle_cmd;
	app->onInputEvent = cube_handle_input;

	LOGI("Govnod Cube starting");

	while (!app->destroyRequested) {
		int events = 0;
		struct android_poll_source *source = nullptr;
		while (ALooper_pollOnce(0, nullptr, &events, (void **)&source) >= 0) {
			if (source != nullptr) {
				source->process(app, source);
			}
			if (app->destroyRequested) {
				break;
			}
		}

		if (cube.vulkan_up && cube.swapchain_up) {
			cube.draw_frame();
		}
	}

	cube.destroy_vulkan();
	LOGI("Govnod Cube bye");
}
