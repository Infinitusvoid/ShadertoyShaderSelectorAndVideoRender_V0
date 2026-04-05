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

#include "../External_libs/glm_0_9_9_7/glm/glm/glm.hpp"

int main()
{
	std::cout << "GL_Template_V0\n";

	return 0;
}