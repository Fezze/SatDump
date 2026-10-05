# Android direct RTL-SDR fix

Implemented and built on 5 October 2026 for the existing native RTL-SDR source.

## Cause found in the pinned dependencies

The Android dependency revision is `4b1f699db2e829fa6ee8070fac001b784b111916`.

* `librtlsdr/rtlsdr_get_device_usb_strings()` opens the USB device. Therefore serial lookup and the previous source enumeration were not permission-free operations. The NDSP device listing also opened the tuner to probe gains.
* `libusb/os/linux_usbfs.c:get_android_jni_fd()` calls `android_jni_connect()`. Without permission, it requests permission and immediately returns `LIBUSB_ERROR_ACCESS`; it does **not** wait for the asynchronous Android reply or retry the open.
* The previous Kotlin receiver ignored `EXTRA_PERMISSION_GRANTED` and recreated the activity for both acceptance and denial. There was no controlled continuation of Recorder's Start operation.
* Enumeration falls back to a numeric index when USB strings cannot be read. Start then treated that index as a serial number. `rtlsdr_get_index_by_serial()` can return `-2` or `-3`; both source implementations only excluded `-1` and could continue with an uninitialized device handle. This was an independent crash path.
* `LIBUSB_ANDROID_JVM_PTR` is misleadingly named: the pinned `libusb/core.c:libusb_init()` expects an `android_app*` and extracts `app->activity->vm`. The existing setting in `android/main.cpp` is correct and remains in place.

The native backend already opens `UsbManager.openDevice()`, obtains the descriptor from its `UsbDeviceConnection`, and closes that connection when libusb closes the device. No external driver or separate descriptor ownership is needed.

## Resulting flow

1. Listing uses libusb descriptors and the exact device catalog extracted from the pinned librtlsdr at CMake configure time. It reads neither serial strings nor tuner gains. Android source IDs are USB device paths, e.g. `/dev/bus/usb/001/007`, rather than serial/index fallbacks.
2. Recorder's Start checks `UsbManager.hasPermission()` through JNI. If permission is absent, Kotlin posts a package-scoped, mutable permission PendingIntent on the Android UI thread. Recorder displays a waiting state and polls the result without blocking rendering.
3. The receiver checks the requested device, grant extra, and actual permission. Denial and detach become explicit errors. No activity recreation occurs.
4. After acceptance, Recorder opens/configures the existing librtlsdr source on a worker, then connects its stream to the existing FFT/waterfall pipeline on the render thread. Source selection and conflicting controls remain disabled during opening.
5. Stop cancels reception, unblocks the writer, joins the receive worker, disables bias and closes the device/connection. Repeated cancellation covers the race where the worker has not yet entered `read_async`. Another Start uses the retained Android permission while the device remains attached.
6. Unexpected read termination, including a zero result after unplugging, ends the receive worker instead of repeatedly retrying a lost handle. Recorder detects the ended receive worker on its next frame, stops the pipeline and releases the USB device, displaying a disconnect/read failure message. Reconnect and refresh the source list before starting the newly enumerated device.

Devices attached before app launch and devices attached later use the same enumeration/permission flow. The existing source refresh button discovers later attachments. Attach/detach broadcasts update permission bookkeeping.

Android omits the SDRplay plugin at CMake configuration time. Desktop SDRplay build/loading remains unchanged. Existing File Source samplerate validation remains unchanged; Start failures retain the selected RTL-SDR source.

## Changed files

* `android/app/src/main/java/MainActivity.kt`: asynchronous permission state/receiver and receiver cleanup.
* `android/app/src/main/AndroidManifest.xml`: proper USB host feature and device-filter placement; remove fictitious USB permission declarations.
* `plugins/sdr_sources/rtlsdr_sdr_support/CMakeLists.txt`: Android catalog generation and JNI/libusb include paths.
* `plugins/sdr_sources/rtlsdr_sdr_support/rtlsdr_android.h` and `rtlsdr_android.cpp`: permission bridge and safe path-based enumeration/lookup.
* `plugins/sdr_sources/rtlsdr_sdr_support/rtlsdr_sdr.h` and `rtlsdr_sdr.cpp`: Recorder readiness, open diagnostics/cleanup, disconnect and cancellation handling.
* `plugins/sdr_sources/rtlsdr_sdr_support/rtlsdr_dev.h` and `rtlsdr_dev.cpp`: safe NDSP listing, permission gate, initialized handles, negative lookup checks and receive cleanup.
* `plugins/sdr_sources/sdrplay_sdr_support/CMakeLists.txt`: Android exclusion.
* `src-core/common/dsp_source_sink/dsp_sample_source.h`: Android-only readiness hooks with defaults preserving other sources.
* `src-interface/recorder/recorder.h`, `recorder.cpp` and `recorder_proc.cpp`: asynchronous RTL-SDR Start and waiting UI.
* `src-testing/android_rtl_usb/`: executable regression harness, USB/JNI and source lifecycle cases, and mock UI/DSP fixtures.
* `docs/android-direct-rtl.md`: this report.

No SDK, AGP, Kotlin, NDK, app version, application ID or dependency revision was changed. No signing configuration was edited.

## Build and verification

Built the existing dependency script's arm64 versions and the app in an isolated `/tmp/satdump-android-checks` workspace. Used SDK 34, build tools 30.0.3, NDK 25.2.9519653, Kotlin 1.8.0, AGP 7.4.1, compatible Gradle 7.5 and JDK 17. ABI filtering was supplied through a temporary Gradle init script rather than a project configuration change.

The host's CMake 4 required its compatibility floor when configuring older dependencies; the temporary dependency build also supplied Android's POSIX `strerror_r` result to SQLite's cross-compilation check. These were build-environment adjustments, not repository dependency changes.

* `:app:assembleDebug`: **BUILD SUCCESSFUL**.
* APK contains 70 native libraries, all `arm64-v8a`; native RTL-SDR plugin is present and SDRplay is absent.
* Package remains `org.satdump.SatDump`, version `2.0.0` / code `9`, min SDK `26`, target SDK `27`, compile SDK `34`.
* APK signature verification passed (v2); this is the standard Gradle **debug** build.
* Kotlin 1.8/SDK 34 compilation and NDK r25c arm64 native checks passed. The completed APK build additionally compiled and linked the actual app and dependencies.
* Regression harness passed Android USB identity/enumeration/permission cases, NDSP worker continuation/cancellation, desktop SDRplay CMake availability, and Android/desktop source cases for missing device, busy open, configuration failure cleanup, repeated Start/Stop, cancellation-before-read race, read failure and zero-result unplug termination. UI/DSP, USB and JNI are mocked in the executable harness; it does not emulate RF hardware.
* Build source files were compared byte-for-byte with the workspace changes before delivering the APK.
* `git diff --check` passed.

Artifacts in `build/artifacts/`:

* `satdump-android-direct-rtl-arm64-debug.apk` (109,061,274 bytes)
* `android-direct-rtl-build.log`
* `android-direct-rtl-tests.log`

APK SHA-256: `8fe0e00018472c2e52c0391cebbe1fcb6225d6950fe1c54fe9b8ad054a3d2dd1`.

Run the regression harness with:

```sh
python3 src-testing/android_rtl_usb/run.py --ndk /path/to/android-ndk-r25c
```

## Remaining hardware validation and limitations

ADB device enumeration was empty, including after APK assembly. Installation and the real USB permission dialog, RF reception/waterfall, tablet Stop/Start and unplug behavior could not be tested on the Xiaomi Pad 6. These acceptance checks remain hardware-unverified.

USB paths can change on reattachment, so refresh/reselect the device after unplugging. After receive failure, Recorder retains the selected source and stops reception automatically. The NDSP flowgraph already starts devices on its flowgraph worker. That worker waits for the asynchronous permission result and continues opening after the grant; Stop can cancel this permission wait. Recorder uses frame polling and an opening worker.

The desktop RTL source was checked using host compilation and mocked lifecycle tests. A complete desktop application build and SDRplay hardware runtime test were not performed.
