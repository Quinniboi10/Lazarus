#ifdef _WIN32
#include <windows.h>
#include <fcntl.h>
#endif

struct UnicodeTerminalInitializer {
    UnicodeTerminalInitializer() {
        #ifdef _WIN32
        SetConsoleOutputCP(CP_UTF8);
        #endif
    }
};

static UnicodeTerminalInitializer unicodeTerminalInitializer;