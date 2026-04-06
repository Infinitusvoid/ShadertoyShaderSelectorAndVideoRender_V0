#define NOMINMAX
#include <Windows.h>

#include "src/ShaderToolApp.h"

int main(int argc, char** argv)
{
    ShaderToolApp::Options options;
    if (argc > 1)
    {
        options.rootOverride = std::filesystem::path(argv[1]);
    }

    ShaderToolApp app;
    if (!app.Initialize(options))
    {
        std::string message = app.LastError();
        if (message.empty())
        {
            message = "The application failed to initialize. Check the logs folder next to the executable if it was created.";
        }

        MessageBoxA(nullptr, message.c_str(), "Shader Selector Startup Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    return app.Run();
}
