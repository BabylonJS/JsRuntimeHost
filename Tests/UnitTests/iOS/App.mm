#include "../Shared/Shared.h"

int main()
{
    int exitCode = RunTests([](const char* message, Babylon::Polyfills::Console::LogLevel logLevel)
    {
        fprintf(stdout, "[%s] %s", EnumToString(logLevel), message);
        fflush(stdout);
    });
    
    fprintf(stderr, "%i\n", exitCode);
    return exitCode;
}
