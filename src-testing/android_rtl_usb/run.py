#!/usr/bin/env python3
"""Host regression harness for the real Android USB bridge. Requires NDK r25c, cmake, clang++."""
import argparse
import pathlib
import subprocess
import tempfile
import sys

parser = argparse.ArgumentParser()
parser.add_argument('--ndk', required=True, type=pathlib.Path)
args = parser.parse_args()
root = pathlib.Path(__file__).resolve().parents[2]
fixture = pathlib.Path(__file__).resolve().parent
subprocess.run([sys.executable, str(fixture / 'lifecycle_check.py')], check=True)
plugin = root / 'plugins/sdr_sources/rtlsdr_sdr_support'
with tempfile.TemporaryDirectory(prefix='satdump-rtl-usb-') as directory:
    work = pathlib.Path(directory)
    (work / 'CMakeLists.txt').write_text(f'''cmake_minimum_required(VERSION 3.12)
project(android_rtl_check)
option(CHECK_ANDROID "Validate Android plugins" ON)
set(ANDROID ${{CHECK_ANDROID}})
set(RTLSDR_LIBRARY test_rtlsdr CACHE STRING "Harness library placeholder")
set(ANDROID_NDK "{args.ndk}")
set(CMAKE_INSTALL_LIBDIR lib)
add_library(satdump_core INTERFACE)
add_library(rtlsdr INTERFACE)
add_library(usb INTERFACE)
add_subdirectory("{plugin}" rtl)
add_subdirectory("{root}/plugins/sdr_sources/sdrplay_sdr_support" sdrplay)
if(CHECK_ANDROID AND TARGET sdrplay_sdr_support)
  message(FATAL_ERROR "SDRplay must be excluded from Android")
elseif(NOT CHECK_ANDROID AND NOT TARGET sdrplay_sdr_support)
  message(FATAL_ERROR "Desktop SDRplay support must remain available")
endif()
''')
    subprocess.run(['cmake', '-S', str(work), '-B', str(work / 'build')], check=True)
    subprocess.run(['cmake', '-S', str(work), '-B', str(work / 'desktop-build'), '-DCHECK_ANDROID=OFF'], check=True)
    # Use Android's JNI ABI, while keeping host libc/C++ headers for the runnable harness.
    (work / 'jni.h').write_bytes((args.ndk / 'toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include/jni.h').read_bytes())
    output = work / 'check'
    subprocess.run(['clang++', '-std=c++17', '-D__ANDROID__', '-DSOURCE_PATH_SIZE=0',
                    '-I' + str(fixture / 'fixtures'),
                    '-I' + str(work),
                    '-I' + str(root / 'src-core'), '-I' + str(plugin),
                    '-I' + str(root / 'android/deps/libusb'), '-I' + str(work / 'build/rtl'),
                    str(fixture / 'check.cpp'), str(plugin / 'rtlsdr_android.cpp'), '-o', str(output)], check=True)
    subprocess.run([str(output)], check=True)

    for platform in ['android', 'desktop']:
        source_test = work / ('source-' + platform)
        command = ['clang++', '-std=c++17', '-pthread', '-DSOURCE_PATH_SIZE=0',
                   '-I' + str(fixture / 'fixtures'), '-I' + str(root / 'src-core'),
                   '-I' + str(plugin), '-I' + str(root / 'android/deps/librtlsdr'),
                   str(fixture / 'source_check.cpp'), str(plugin / 'rtlsdr_sdr.cpp'), '-o', str(source_test)]
        if platform == 'android': command.insert(1, '-D__ANDROID__')
        subprocess.run(command, check=True)
        subprocess.run([str(source_test)], check=True, timeout=10)
