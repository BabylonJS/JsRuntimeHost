#include "../Shared/Shared.h"

int main()
{
    for (int i = 0; i < 1000; i++)
    {
        RunTests([](const char* message, Babylon::Polyfills::Console::LogLevel logLevel) {
            fprintf(stdout, "[%s] %s", EnumToString(logLevel), message);
            fflush(stdout);
        });
    }
    return 0;
}
