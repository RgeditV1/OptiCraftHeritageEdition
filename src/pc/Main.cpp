#define SDL_MAIN_HANDLED
#include "SDL.h"

#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#include "client/Minecraft.h"
#include "net/minecraft/src/GameResources.h"
#include "net/minecraft/src/Tessellator.h"
#include "platform/Log.h"

#include "external/SDLException.h"

#include "lwjgl/GLContext.h"
#include "pc/render/PcRenderBackend.h"

namespace
{
std::string trimUsername(std::string value)
{
	if (value.size() >= 3 &&
		static_cast<unsigned char>(value[0]) == 0xef &&
		static_cast<unsigned char>(value[1]) == 0xbb &&
		static_cast<unsigned char>(value[2]) == 0xbf)
	{
		value.erase(0, 3);
	}

	const auto first = value.find_first_not_of(" \t\r\n");
	if (first == std::string::npos)
		return "";
	const auto last = value.find_last_not_of(" \t\r\n");
	return value.substr(first, last - first + 1);
}

PcRenderBackendType loadRenderBackendPreference()
{
	const std::filesystem::path path =
		std::filesystem::path(GameResources::getExeDir()) / ".minecraft" / "options.txt";
	std::ifstream input(path);
	std::string line;
	while (input && std::getline(input, line))
	{
		if (line.rfind("renderBackend:", 0) != 0)
			continue;
		return pcRenderBackendFromString(line.substr(14));
	}
	return PcRenderBackendType::OpenGL;
}

int loadOptiFineAaLevel()
{
	const std::filesystem::path path =
		std::filesystem::path(GameResources::getExeDir()) / ".minecraft" / "options.txt";
	std::ifstream input(path);
	std::string line;
	while (input && std::getline(input, line))
	{
		if (line.rfind("ofAaLevel:", 0) != 0)
			continue;
		try
		{
			const int value = std::stoi(line.substr(10));
			switch (value)
			{
			case 2: case 4: case 8: case 16: return value;
			default: return 0;
			}
		}
		catch (...)
		{
			return 0;
		}
	}
	return 0;
}

std::string loadUsername()
{
	const std::filesystem::path path =
		std::filesystem::path(GameResources::getExeDir()) / "username.txt";
	std::ifstream input(path);
	std::string username;
	if (input && std::getline(input, username))
		username = trimUsername(username);

	if (!username.empty())
		return username;

	username = "Player";
	std::ofstream output(path, std::ios::trunc);
	if (output)
		output << username << '\n';
	return username;
}
}

int main(int argc, char *argv[])
{
	McLog::openSessionFile("log");
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER | SDL_INIT_AUDIO) < 0)
	{	MC_LOG_ERROR("SDL", "SDL_Init failed: %s", SDL_GetError());
		McLog::flush();
		throw SDLException();
	}
	pcRenderBackendSetRequested(loadRenderBackendPreference());
	lwjgl::GLContext::setRequestedSamples(loadOptiFineAaLevel());
	lwjgl::GLContext::instantiate();
	MC_LOG_INFO("Main", "OpenGL version: %s", reinterpret_cast<const char*>(glGetString(GL_VERSION)));
#if PLATFORM_PC_LEGACY
	Tessellator::convertQuadsToTriangles = !pcRenderBackendIsDirect3D9();
#endif

	jstring username = loadUsername();
	if (argc >= 2 && std::strlen(argv[1]) > 0)
		username = argv[1];

	jstring auth = "-";
	if (argc >= 3 && std::strlen(argv[2]) > 1)
		auth = argv[2];

	MC_LOG_INFO("Main", "Starting Minecraft with username: %s, auth: %s", username.c_str(), auth.c_str());
	Minecraft::start(&username, &auth);
	McLog::flush();

	return 0;
}
