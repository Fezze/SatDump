#!/usr/bin/env python3
"""Exercise the actual native Activity command handler with mocked Android/EGL hooks."""
import pathlib
import subprocess
import tempfile
import xml.etree.ElementTree as ET

root = pathlib.Path(__file__).resolve().parents[2]
activity = ET.parse(root / 'android/app/src/main/AndroidManifest.xml').find('application/activity')
assert activity.get('{http://schemas.android.com/apk/res/android}launchMode') == 'singleTask', 'USB attach must reuse the native Activity'
source = (root / 'android/main.cpp').read_text()
handler = source.split('static void handleAppCmd(', 1)[1].split('static int32_t handleInputEvent', 1)[0]
with tempfile.TemporaryDirectory(prefix='satdump-lifecycle-') as directory:
    cpp = pathlib.Path(directory) / 'check.cpp'
    cpp.write_text('''#include <cassert>
#include <cstdint>
struct android_app { void *window; };
enum { APP_CMD_INIT_WINDOW, APP_CMD_TERM_WINDOW, APP_CMD_RESUME,
       APP_CMD_PAUSE, APP_CMD_SAVE_STATE };
bool g_Resumed = false;
int init_calls = 0, shutdown_calls = 0;
void init(android_app *) { ++init_calls; }
void shutdown() { ++shutdown_calls; }
namespace satdump { struct Config { void saveUser() {} } satdump_cfg; }
static void handleAppCmd(''' + handler + '''
int main() {
 android_app app{nullptr};
 handleAppCmd(&app, APP_CMD_INIT_WINDOW);
 assert(init_calls == 0);
 app.window = &app;
 handleAppCmd(&app, APP_CMD_INIT_WINDOW);
 handleAppCmd(&app, APP_CMD_RESUME);
 assert(init_calls == 1 && g_Resumed);
 // A USB dialog pauses/resumes without TERM_WINDOW/INIT_WINDOW.
 handleAppCmd(&app, APP_CMD_PAUSE);
 assert(!g_Resumed && shutdown_calls == 0);
 handleAppCmd(&app, APP_CMD_RESUME);
 assert(g_Resumed && init_calls == 1 && shutdown_calls == 0);
 handleAppCmd(&app, APP_CMD_TERM_WINDOW);
 assert(shutdown_calls == 1);
 handleAppCmd(&app, APP_CMD_INIT_WINDOW);
 assert(init_calls == 2);
}
''')
    exe = pathlib.Path(directory) / 'check'
    subprocess.run(['clang++', '-std=c++17', str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('Native Activity USB dialog pause/resume and window teardown checks passed')
