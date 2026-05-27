// Copyright (c) 2026, the Luxon Server contributors
// SPDX-License-Identifier: BSD-3-Clause

#include "platform.hpp"

#include <iostream>
#include <string>
#include <stdexcept>

// PLatform-specific includes/globals

#if defined(PLATFORM_LINUX)
#include <cstring>
#include <csignal>
#include <cerrno>

static bool g_stopping = false;
static void sig_handler(int, siginfo_t *, void *) { g_stopping = true; }

#elif defined(PLATFORM_WINDOWS)
#include <winsock2.h>
#include <cstdlib>

#elif defined(PLATFORM_3DS)
#include <string_view>
#include <thread>
#include <chrono>
#include <exception>
#include <cerrno>
#include <cstring>
#include <malloc.h>
#include <3ds.h>

static u32 *g_soc_buffer = nullptr;
constexpr auto SOC_ALIGN = 0x1000, SOC_BUFFERSIZE = 0x100000;

[[noreturn]]
static void custom_terminate() noexcept {
    std::string message;
    try {
        std::rethrow_exception(std::current_exception());
    } catch (const std::exception& e) {
        message = e.what();
    } catch (...) {
        message = "Unknown";
    }

    errorConf conf = {.type = ERROR_TEXT_WORD_WRAP,
                      .errorCode = errno,
                      .upperScreenFlag = ERROR_NORMAL,
                      .useLanguage = CFG_LANGUAGE_EN,
                      .Text = {L'I', L'N', L'V', L'A', L'L', L'I', L'D', L'\0'},
                      .homeButton = true,
                      .softwareReset = false,
                      .appJump = false,
                      .returnCode = ERROR_UNKNOWN,
                      .eulaVersion = 0};
    errorText(&conf, ("An exception was thrown but never handled:\n\n" + message).c_str());
    errorDisp(&conf);

    aptExit();
    socExit();
    gfxExit();
    exit(-errno);
}

// Stubs for SQLite3
extern "C" {
uid_t geteuid(void) { return 0; }
int fchown(int fd, uid_t owner, gid_t group) { return 0; }
}

#elif defined(PLATFORM_NDS)
#include <exception>
#include <nds.h>
#include <fat.h>
#include <dswifi9.h>

[[noreturn]]
static void custom_terminate() noexcept {
    std::cout << "\n\n--- UNCAUGHT EXCEPTION ---\n";
    try {
        std::rethrow_exception(std::current_exception());
    } catch (const std::exception& e) {
        std::cout << typeid(e).name() << ": " << e.what() << "\n";
    } catch (...) {
        std::cout << "Unknown exception type.\n";
    }

    std::cout << "\nSystem halted.\nPress START to power off.";

    while (true) {
        swiWaitForVBlank();
        scanKeys();
        if (keysDown() & KEY_START)
            systemShutDown();
    }
}

#if defined(__BLOCKSDS__)
#include <cstdint>
#include <ctime>

extern "C" {
int nanosleep(const struct timespec *req, struct timespec *rem) {
    if (!req)
        return -1;

    // Base DS ARM9 clock: 67,027,964 Hz
    // swiDelay(1) = 4 CPU cycles
    uint32_t units_per_sec = 16756991;

    // In native DSi, CPU runs at 134 MHz (2x speed)
    if (isDSiMode()) {
        units_per_sec *= 2;
    }

    // Sleep for full seconds
    for (time_t i = 0; i < req->tv_sec; ++i)
        swiDelay(units_per_sec);

    // Sleep for fractional nanoseconds
    if (req->tv_nsec > 0) {
        uint32_t delay_units = (uint32_t)(((uint64_t)req->tv_nsec * units_per_sec) / 1000000000ULL);
        if (delay_units > 0)
            swiDelay(delay_units);
    }

    // No time wil ever be remaining
    if (rem) {
        rem->tv_sec = 0;
        rem->tv_nsec = 0;
    }

    return 0;
}
}
#endif
#endif

// Class implementation

Platform::Platform() {
#if defined(PLATFORM_LINUX)
    struct sigaction act = {0};
    act.sa_flags = SA_SIGINFO;
    act.sa_sigaction = sig_handler;
    for (int sig : {SIGTERM, SIGINT, SIGQUIT, SIGHUP}) {
        if (sigaction(sig, &act, nullptr) < 0) {
            throw std::runtime_error("sigaction() = " + std::string(strerror(errno)));
        }
    }
#elif defined(PLATFORM_WINDOWS)
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        throw std::runtime_error("Failed to initialize WinSock");
    }
#elif defined(PLATFORM_3DS)
    std::set_terminate(custom_terminate);
    gfxInitDefault();
    consoleInit(GFX_TOP, nullptr);
    aptInit();
    g_soc_buffer = static_cast<u32 *>(memalign(SOC_ALIGN, SOC_BUFFERSIZE));
    if (Result ret = socInit(g_soc_buffer, SOC_BUFFERSIZE); ret != 0) {
        throw std::runtime_error("socInit() = " + std::to_string(static_cast<unsigned>(ret)));
    }
#elif defined(PLATFORM_NDS)
    // Configure video
    videoSetMode(MODE_0_2D);
    lcdMainOnBottom();

    // Initialize console
    consoleDemoInit();

    // Set terminate handler
    std::set_terminate(custom_terminate);

    // Initialize SD Card Filesystem
    std::cout << "Mounting SD Card... " << std::flush;
    if (!fatInitDefault())
        throw std::runtime_error("Failed to mount filesystem");
    std::cout << "Done!" << std::endl;

    // Initialize WiFi
    std::cout << "Connecting via WFC data... " << std::flush;
    if (!Wifi_InitDefault(WFC_CONNECT))
        throw std::runtime_error("Failed to initialize and conect to WiFi");
    std::cout << "Done!" << std::endl;
#endif
}

Platform::~Platform() {
#if defined(PLATFORM_3DS)
    aptSetHomeAllowed(false);
#endif

    std::cout << std::flush;
    std::cerr << std::flush;
    std::clog << "\nRuntime destroyed." << std::endl;

#if defined(PLATFORM_WINDOWS)
    WSACleanup();
#elif defined(PLATFORM_3DS)
    std::clog << "Press START to exit" << std::flush;
    for (u32 kDown; !(hidKeysDown() & KEY_START) && cooperate(); hidScanInput()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    aptExit();
    socExit();
    gfxExit();
#endif
}

bool Platform::cooperate() noexcept {
#if defined(PLATFORM_LINUX)
    return !g_stopping;
#elif defined(PLATFORM_3DS)
    return aptMainLoop();
#elif defined(PLATFORM_NDS)
    return true;
#else
    // Windows / Generic
    return true;
#endif
}

const char *Platform::read_input(const char *hint) {
#if defined(PLATFORM_3DS)
    static SwkbdState swkbd;
    static char swkbd_buf[2048];
    memset(swkbd_buf, 0, sizeof(swkbd_buf));
    swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 3, sizeof(swkbd_buf));
    swkbdSetHintText(&swkbd, hint);
    swkbdInputText(&swkbd, swkbd_buf, sizeof(swkbd_buf));
    return swkbd_buf;
#else
    // Linux, Windows, NDS, Generic
    static std::string content;
    std::cout << hint << ": ";
    std::getline(std::cin, content);
    return content.c_str();
#endif
}

void Platform::clear_screen() {
#if defined(PLATFORM_WINDOWS)
    system("cls");
#elif defined(PLATFORM_3DS) || defined(PLATFORM_NDS)
    consoleClear();
#else
    // Linux, Generic
    std::cout << "\033[H\033[2J\033[3J";
#endif
}
