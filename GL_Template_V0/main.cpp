#include <iostream>
#include <vector>
#include <string>
#include <random>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <algorithm>
#include <cstdlib> // std::atoi

// -----------------------------
// OpenGL / GLFW / GLEW
// -----------------------------
#include <GL/glew.h>
#include <GLFW/glfw3.h>

// -----------------------------
// stb_image & stb_image_write
// -----------------------------
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../External_libs/stb/image/stb_image.h"
#include "../External_libs/stb/image/stb_image_write.h"

// -----------------------------
// tinyply (header-only style)
// -----------------------------
// #define TINYPLY_IMPLEMENTATION
// #include "../External_libs/tinyply/tinyply/source/tinyply.h"

#include "../cpp_httplib/cpp-httplib-master/httplib.h"

#include "../External_libs/glm_0_9_9_7/glm/glm/glm.hpp"

int main()
{
	const std::string folder_root = "C:/Users/Cosmos/Desktop/output/ShadertoyShaderSelectorAndVideoRender_V0";
	std::string shader_folder_path = folder_root + "/shaders_input";
	
	std::string shader_folder_path_valid = folder_root + "/shaders_valid"; // The shaders are valid shadertoy shader they compile and run nicely

	std::string shader_selected_for_rendering_folder_path = folder_root + "/shaders_selected_for_rendering";

	std::string shader_rendered = folder_root + "/shaders_rendered";
	std::string video_rendered = folder_root + "/video_rendered";

	

	std::cout << "GL_Template_V0\n";

	return 0;
}