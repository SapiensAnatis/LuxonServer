// Copyright (c) 2026, the Luxon Server contributors
// SPDX-License-Identifier: BSD-3-Clause

#include "luxon/server/server_manager.hpp"
#ifdef LUXON_SERVER_ENABLE_MULTIPROCESSING
#include "luxon/server/ipc.hpp"
#endif
#include "platform.hpp"

#include <iostream>
#include <cstdlib>
#include <string>
#include <string_view>

// Platform-specific headers
#if defined(_WIN32)
#include <windows.h>
#include <io.h>
#else
#include <cerrno>
#include <unistd.h>
#include <sys/types.h>
#ifdef __linux__
#include <csignal>
#include <sys/prctl.h>
#endif
#endif

#ifdef LUXON_SERVER_ENABLE_MULTIPROCESSING
int main(int argc, char *argv[]) {
    Platform P;

    // If spawned as a subprocess intercept CLI flag and run as child
    if (argc >= 3 && std::string_view(argv[1]) == "--child-fd") {
#ifdef __linux__
        prctl(PR_SET_PDEATHSIG, SIGINT);

#endif
        auto child_fd = server::IPC::socket_from_string(argv[2]);
        if (!child_fd || *child_fd == server::IPC::INVALID_OS_SOCKET) {
            std::cerr << "Invalid IPC descriptor! Dying..." << std::endl;
            return EXIT_FAILURE;
        }
        try {
            server::ServerManager child_manager((server::IPC(*child_fd)));
            child_manager.run();
        } catch (const std::exception& e) {
            std::cerr << "std::terminate about to be called in subprocess: " << e.what() << std::endl;
            std::cerr << "Child is about to die!" << std::endl;
            throw;
        }
        return EXIT_SUCCESS;
    }

    // Capture the executable path to spawn exact clones later
    std::string exe_path = argv[0];

    // Set up the subprocess spawning mechanism before the manager is initialized
    server::ServerManager::handle_start_subprocess = [exe_path](const std::string& fd) {
#if defined(_WIN32)
        // Build command line
        std::string cmd = exe_path + " --child-fd " + fd;

        STARTUPINFOA si;
        ZeroMemory(&si, sizeof(si));
        si.cb = sizeof(si);

        PROCESS_INFORMATION pi;
        ZeroMemory(&pi, sizeof(pi));

        // Spawn child
        if (!CreateProcessA(nullptr,    // Application name (use cmd line instead)
                            cmd.data(), // Command line string
                            nullptr,    // Process handle not inheritable
                            nullptr,    // Thread handle not inheritable
                            TRUE,       // Set handle inheritance to TRUE
                            0,          // No creation flags
                            nullptr,    // Use parent's environment block
                            nullptr,    // Use parent's starting directory
                            &si,        // Pointer to STARTUPINFO structure
                            &pi         // Pointer to PROCESS_INFORMATION structure
                            )) {
            std::cerr << "CreateProcess to create subprocess failed! Error: " << GetLastError() << std::endl;
            return;
        }

        // Close process and thread handles in parent
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

#else
        pid_t pid = fork();

        if (pid < 0) {
            std::cerr << "Failed to fork subprocess! Errno: " << errno << std::endl;
            return;
        }

        if (pid == 0) {
            // Re-execute the binary. The open file descriptor will be natively inherited.
            execl(exe_path.c_str(), exe_path.c_str(), "--child-fd", fd.c_str(), nullptr);

            // If execv returns, the replacement failed
            std::cerr << "Subprocess failed to execv! Errno: " << errno << std::endl;
            std::exit(EXIT_FAILURE);
        }
#endif
    };
#else
int main() {
    Platform P;
#endif

    // Boot primary process
    try {
        server::ServerManager("config.yml").run();
    } catch (const std::exception& e) {
        std::cerr << "std::terminate about to be called: " << e.what() << std::endl;
        throw;
    }

    return EXIT_SUCCESS;
}
