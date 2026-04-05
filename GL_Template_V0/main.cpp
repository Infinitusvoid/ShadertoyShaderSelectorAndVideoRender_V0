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

	/*
	* What to build
	 - we first go thru all the shaders in the shader_folder_path and test each shader if it's valid shadertoy shaders 
	   if it's not valid shadertoy shader we try to fix it usually this means removing for example the uniform, basically trying out besed to keep the essence of the shader
	   the thing sometimes there are uniform float time; etc thing in which we can remove and fix the thing and similar thing if we can't fix it we skip the shader
       and basically log out that the shader does not work ( btw the file extension can be all kind of thing so like you will have to test but usual are thing like .glsl, .toy, .txt etc but
	   well if it's image or some usual know type in the folder like .avi, .mpeg, .png just ignore the file 
	   well for the shaders the are valid or we can generate correct shader version we write a copy into shader_folder_path_valid
	 - We start in window mode but with F we get in immerise full screen ( be sure you render into buffers so we don't get glitches for that come from window vs full screen mode ) and play the first shader in the folder valid shader with arrow keys we can move to next shader or previus shader, 
	   while with spacebar we copy the shader into shader_selected_for_rendering_folder_path, than when we press R we get into rendering mode 
     - We can select for all the shader that we wish to render into a video a resulution and framerate the default is 4k 60fps 
	   we skip use ffmpeg basically to render directly into video files, we try use a fast enocoding while try to balance the file size as well probably should be a settting 
     - when the thing is rendered we move the shader from shader_selected_for_rendering_folder_path -> shader_rendered
	   while the video is writen into the video_rendered basically make sure that if a program crashes the work is not lot and we we can just start the program and go into rendering mode and just render
     - Feel free to use imgut if you think you can make nicer interface but using only OpenGL is fine as well and make sure we are not freezing the computer while rendering so basically 
	   the user can for example watch youtube while the whole thing is working in background, well if you think making a nicer web interface feel free to do that instead nicely 
	*/

	std::cout << "GL_Template_V0\n";

	return 0;
}