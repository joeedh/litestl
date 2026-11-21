/* Linked into every test executable (the `test` macro in tests/CMakeLists.txt). On
 * Windows, a failed CRT assert or abort() opens a dialog (Abort/Retry/Ignore in debug
 * builds, then Windows Error Reporting), and the test hangs until someone clicks it.
 * This sends CRT reports to stderr and turns the dialogs off, so a failing test prints
 * its message and exits with code 3. */

#ifdef _WIN32

#include <crtdbg.h>
#include <cstdlib>
#include <initializer_list>

#include <windows.h>

namespace {

struct NoDialogs {
  NoDialogs()
  {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    _set_abort_behavior(0, _CALL_REPORTFAULT); /* keep the message, skip WER */
#ifdef _DEBUG
    for (int type : {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT}) {
      _CrtSetReportMode(type, _CRTDBG_MODE_FILE);
      _CrtSetReportFile(type, _CRTDBG_FILE_STDERR);
    }
#endif
  }
};

const NoDialogs g_noDialogs;

} // namespace

#endif
