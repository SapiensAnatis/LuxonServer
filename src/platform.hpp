// Copyright (c) 2026, the Luxon Server contributors
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

class Platform {
public:
    Platform();
    Platform(Platform&) = delete;
    Platform(const Platform&) = delete;
    Platform(Platform&&) = delete;

    ~Platform();

    static bool cooperate() noexcept;
    static const char *read_input(const char *hint);
    static void clear_screen();
};

#if !defined(PLATFORM_WINDOWS)
#define MSG_FLAGS_OR_ZERO(...) __VA_ARGS__
#else
#define MSG_FLAGS_OR_ZERO(...) 0
#endif
