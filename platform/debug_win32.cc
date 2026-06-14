#include <windows.h>

#include <dbghelp.h>
#include <iostream>

#pragma comment(lib, "dbghelp.lib")

namespace litestl::platform {
void printStackTrace()
{
  void *stackBuffer[64];
  HANDLE process = GetCurrentProcess();

  // 1. Initialize the symbol handler
  SymInitialize(process, NULL, TRUE);

  // 2. Capture the raw pointers
  USHORT frames = CaptureStackBackTrace(1, 64, stackBuffer, NULL);

  // 3. Allocate memory for symbol metadata
  char buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR)];
  PSYMBOL_INFO symbol = (PSYMBOL_INFO)buffer;
  symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
  symbol->MaxNameLen = MAX_SYM_NAME;

  // 4. Translate pointers to names
  for (USHORT i = 0; i < frames; ++i) {
    DWORD64 displacement = 0;
    if (SymFromAddr(process, (DWORD64)stackBuffer[i], &displacement, symbol)) {
      std::cout << "[" << i << "] " << symbol->Name
                << "() - Address: " << (void *)symbol->Address << "\n";
    } else {
      std::cout << "[" << i << "] UnknownFunction - Address: " << stackBuffer[i] << "\n";
    }
  }

  // 5. Clean up resource
  SymCleanup(process);
}
} // namespace litestl::platform
