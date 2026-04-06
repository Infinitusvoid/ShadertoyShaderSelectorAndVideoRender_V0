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
        return 1;
    }

    return app.Run();
}
