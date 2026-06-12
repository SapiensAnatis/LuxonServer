# Luxon Server

Luxon Server is a clean-room implementation of the Photon Realtime server. It is built on top of the Luxon project, which provides the necessary reimplementation of the ENet protocol and Photon's binary serialization format.
The goal of this project is to be a drop-in replacement for the official server for multiplayer games that utilize Photon. It aims to support games out of the box, provided they do not rely on complex server-side plugins, though a plugin system is available if needed.

## Table of Contents
- [Legal Disclaimer and Legal Contributing Requirements](#legal-disclaimer)
- [Getting Started](#getting-started)
- [Configuration](#configuration)
- [Usage](#features)
- [Platform Support](#platform-support)
- [Foreign Function Interface (FFI)](#foreign-function-interface-ffi)
- [FAQ](#faq)

---
---

## Legal Disclaimer

> Luxon Server is an independent, open‑source project developed by its contributors. It is **not** affiliated with, endorsed by, or sponsored by Exit Games GmbH or any of its subsidiaries.
> 
> **Photon** and **Photon Realtime** are registered trademarks of Exit Games GmbH. All other trademarks, service marks, and trade names referenced in this project are the property of their respective owners.
> 
> Luxon Server is designed to be protocol‑compatible with the Photon Realtime client SDKs. This compatibility is achieved solely through independent analysis of publicly available protocol documentation and network traffic observation. No proprietary code, confidential information, or reverse‑engineered assets from Exit Games are included in this project.
> 
> Any use of the term "Photon" within this repository is for descriptive purposes only, to indicate compatibility, and does not imply any endorsement or official relationship.
> 
> Luxon Server is NOT a competitive product, and will never intersect with the group of people that would generate Exit Games any income. Luxon Server intentionally lacks important features that make the Photon Server SDK useful to paying Exit Games customers (most importantly scalability and load balancing).
>
> If you are a representative of Exit Games and have concerns regarding this project, please contact me at tuxifan@posteo.de so I may address them promptly.

### ⚠️ **STOP: Read Carefully Before Contributing**

**Before submitting any issues, pull requests, or code, you must verify that you meet the following legal requirement:**

 - **No Exit Games Agreements**: You must **never** have accepted, signed, or otherwise agreed to the Exit Games / Photon Engine Terms of Service, End User License Agreement (EULA), Non-Disclosure Agreement (NDA), or any other binding agreement with Exit Games in any capacity.

**Additionally, to ensure no intellectual property contamination occurs, contributors must not have:**

 - Decompiled, reverse-engineered using "white-box" methods, or viewed the source code of any proprietary Exit Games/Photon binaries/SDKs. Discovering functionality through "black-box" testing (interacting with the software externally to observe its behavior) is acceptable.

**Why is this necessary?**

If you have ever agreed to the Exit Games Terms of Service, you are bound by their restrictions against reverse engineering and creating derivative works. By accepting code from developers who have agreed to those terms, this project could be exposed to breach-of-contract or copyright claims.

If you do not meet these criteria, you are considered legally "tainted" for the purposes of this project and **cannot contribute**. I appreciate your understanding in helping me keep Luxon Server safe and legally sound.

---
---

## Compatibility

Most games using standard matchmaking logic (joining lobbies, creating rooms, random matching) should work immediately without modification to the game client or server configuration.\
Chat opcodes aren't implemented yet.

## Getting Started

There are three ways to get a build of Luxon Server: downloading a stable release, grabbing the latest CI build, or compiling it yourself. 

### 1. Download a Release (Recommended)
For most users, the easiest and most stable way to get started is to download the latest pre-compiled release.
* [Download from GitHub Releases](https://github.com/niansa/LuxonServer/releases)

### 2. Download from CI (Bleeding Edge)
If you need the absolute latest features or bug fixes that haven't been released yet, you can download the build artifacts directly from the CI pipelines.
* [Download from GitLab Pipelines](https://gitlab.com/luxon_project/LuxonServer/-/pipelines)

### 3. Build from Source
If you prefer to compile the server yourself, plan to modify the code, or would like to use plugins you can build Luxon Server from source.

#### Prerequisites
To build Luxon Server, you will need:
* CMake 3.16 or higher
* A C++ compiler and standard library capable of supporting **C++23** and exceptions support

#### Building
The project uses standard CMake build procedures.

```bash
git submodule update --init --depth 1 --recursive
mkdir build
cd build
cmake ..
cmake --build .
```

Possible compile time options:
 - **`LUXON_SERVER_ENABLE_WEBSERVER`** (default: `ON`): Enable the built-in webserver including the web interface
 - **`LUXON_SERVER_ENABLE_PLUGINS`** (default: `OFF`): Enables plugin system
 - **`LUXON_PLUGINS`** (default: empty): Semicolon separated list of CMake projects to configure containing `luxon_register_plugin()` CMake calls for statically linking a plugin into Luxon Server
 - **`LUXON_SERVER_BUILD_FFI`** (default: `OFF`): Builds the FFI library
 - **`LUXON_SERVER_EXPOSE_FULL_FFI`** (default: `OFF`): Enables all features required to expose the *full* FFI. Forces `LUXON_SERVER_BUILD_FFI`, `LUXON_SERVER_ENABLE_PLUGINS`, `LUXON_SERVER_HOOKPOINTS`, and `LUXON_SERVER_ENABLE_COMMAND_RESTARTER` to be `ON`. Strictly disables `LUXON_SERVER_USE_SPDLOG`*
 - **`LUXON_SERVER_POLL`** (default: `OFF`): Polls sockets blindly and rapidly, less efficient and slower
 - **`LUXON_SERVER_HOOKPOINTS`** (default: `OFF`, forced `ON` if full FFI is exposed): Useful when linking LuxonServer as a library, allows hooking into some parts of the server via `ServerManager::hookpoints` (see [hookpoints.hpp](https://github.com/niansa/LuxonServer/blob/master/include/luxon/server/hookpoints.hpp))
 - **`LUXON_SERVER_ENABLE_COMMAND_RESTARTER`** (default: `OFF`, forced `ON` if full FFI is exposed): Allow commands to be restarted later for better async support
 - **`LUXON_USE_EMBED_RESOURCE`** (default: `OFF` except on MSVC and WebAssembly): Uses the [embedresource](https://github.com/ankurvdev/embedresource) library for binary embedding instead of inline assembly
 - **`LUXON_SERVER_TRACY`** (default: `OFF`): Links and enables [Tracy](https://github.com/wolfpld/tracy) client
 - **`LUXON_SERVER_TRACY_ON_DEMAND`** (default: `ON`): Only collect tracy data with profiler server connected. *Only available if `LUXON_SERVER_TRACY` is `ON`*
 - **`LUXON_ENET_ENABLE_METRICS`** (default: `OFF`): Collects more metrics available as a Prometheus endpoint on webserver (`/metrics`), ready for use with provided [Grafana Dashboard](https://github.com/niansa/LuxonServer/blob/master/grafana-dashboard.json)
 - **`LUXON_SERVER_USE_SPDLOG`** (default: `ON` on Linux/Windows/macOS/BSD, `OFF` otherwise): Use spdlog for Luxon server. *Strictly disabled if `LUXON_SERVER_EXPOSE_FULL_FFI` is `ON`*
 - **`LUXON_SERVER_USE_SANMAKE`** (default: `ON` on Linux/Windows/macOS/BSD, `OFF` otherwise): Use basic sanitizers for Luxon server
 - **`LUXON_SERVER_ENABLE_SETTINGS_DATABASE`** (default: `ON` on Linux/Windows/macOS/BSD, `OFF` otherwise): Build Luxon Server with settings storage database support enabled
 - **`LUXON_USE_TOMCRYPT`** (default: `OFF` on supported desktop platforms): Use alternative encryption library with wider compatibility
 
### Configuration

The server is configured via a `config.yml` file. A `config.example.yml` is provided in the repository.
The configuration defines the listening ports for the three main server components:

1. **NameServer:** Handles initial region requests (ignored for now) and authentication (stubbed for now).
2. **MasterServer:** Handles lobbies and matchmaking.
3. **GameServer:** Hosts the actual room logic and relay.

By default, an HTTP server is also available on port `5088` to provide a web-based dashboard for monitoring connections and server load.

## Usage

The easiest way to use Luxon Server with an existing game is to redirect the game's DNS requests to your local machine (or wherever you are hosting the server).
You do not need to patch the game executable. Instead, add an entry to your hosts file (or configure your router's DNS) to point the standard Photon domains to your server IP.\
For example, if your server is running on 192.168.1.56:

```
192.168.1.56 ns.exitgames.com ns.photonengine.io
```

Once this is set, the game will connect to Luxon Server thinking it is the official cloud.

## Features

* **Load Balancing Logic:** Full implementation of the Name/Master/Game server flow.
* **Web Dashboard:** An embedded HTTP server (default port 5088) provides a real-time monitor. It shows active connections, packet loss, round-trip times, and a visual graph of server load/busy time at path `/stats`.
* **Peer Persistence:** Handles player authentication tokens and state transfer between Master and Game servers.
* **Plugin System (Optional):** If you need custom server-side logic, Luxon supports plugins written in C++. This is disabled by default in CMake (LUXON_SERVER_ENABLE_PLUGINS=OFF) to keep the build lightweight, strictly single-threaded.

## Platform Support

Luxon Server is highly portable. It natively runs on: 
 * Linux
 * Windows (down to Vista)
 * Mac OS
 * FreeBSD
 * OpenBSD
 * Nintendo 3DS (devkitpro)

It can additionally target WASI (preview 1) with a custom BSD sockets interface (as p1 doesn't provide one that is complete enough).
This allows it to be compiled down to other languages / ILs, allowing support for "runtime-native" execution in:
 * [JVM](/WASMImpl/Java/)
 * dotnet/mono runtime
 * V8

Compilation to C and then to old platforms is also possible, including:
 * [DOS](WASMImpl/C/)
 * Windows 3.1 with *win32s*

Note that I can't "officially" support the latter 2 platforms. Expect them to run non-optimally. Still free to create an issue if you see any problems with them.

## Foreign Function Interface (FFI)

Luxon Server implements a "Foreign Function Interface". That means it can optionally expose a simple interface for controlling its operation, usable from almost any programming language. This means you can **embed the server**, **write plugins in languages other than C++**, and more.

It exposes and accepts Photon/Luxon ser messages and values in a format documented here: https://gitlab.com/luxon_project/Luxon/-/blob/master/doc/ipc_binary.md

This interface is also available with Luxon Server built as a WebAssembly module.

I consider the ABI to be reasonably stable, but I recommend pinning Luxon Server to a specific commit or tag when publishing your own bindings!

For enabling it, see [Building](#building).

## FAQ

**Q:** Why is the server completely single-threaded?\
**A:** Luxon Server is NOT supposed to be used as an alternative the the official Photon Server SDK. That means it doesn't have to handle loads big enough to saturate a single core even on very low-end systems. I have estimated the *New Nintendo 3DS* as a server to be able to handle at least 10, probably up to 30 concurrently active players! Plus, strict single-threading keeps the codebase simple.

**Q:** Are there any plans on implementing *actual* load balancing (not just the protocol part of it) across multiple systems/processes?\
**A:** I am strictly against supporting load balancing across different systems. I do NOT want to agitate Exit Games by releasing a competitive product.\
HOWEVER, it *is* possible to run GameServers inside separate processes *on the same machine* by specifying `subprocess: true` in a GameServer type server configuration block. You can specify several GameServers on different ports in subprocesses this way and games will automatically be created on a randomly chosen GameServer.

**Q:** Are you going to write bindings for writing plugins in C#, Python, Javascript, ...?\
**A:** An FFI interface exists now, and I consider it to be quite stable. I might implement bare Python bindings in the future for reference. When writing your own bindings however, be sure to pin luxonserver to a specific commit or tag to avoid any breakages that may occur anyways. I can't guarantee full, complete FFI ABI stability yet.
