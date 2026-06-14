#include <windows.h>

#include <dbghelp.h>
#include <sstream>
#include <string>

#pragma comment(lib, "dbghelp.lib")

namespace litestl::platform {
  using std::string;

string getStackTrace()
{
  void *stackBuffer[64];
  HANDLE process = GetCurrentProcess();
  std::ostringstream out;

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
      out << "[" << i << "] " << symbol->Name
          << "() - Address: " << (void *)symbol->Address << "\n";
    } else {
      out << "[" << i << "] UnknownFunction - Address: " << stackBuffer[i] << "\n";
    }
  }

  // 5. Clean up resource
  SymCleanup(process);
  return out.str();
}
} // namespace litestl::platform
