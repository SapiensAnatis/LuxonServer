package com.gitlab.luxon_project;

import com.dylibso.chicory.runtime.ExportFunction;
import com.dylibso.chicory.runtime.HostFunction;
import com.dylibso.chicory.runtime.ImportFunction;
import com.dylibso.chicory.runtime.ImportValues;
import com.dylibso.chicory.runtime.Instance;
import com.dylibso.chicory.runtime.Memory;
import com.dylibso.chicory.wasi.WasiOptions;
import com.dylibso.chicory.wasi.WasiPreview1;
import com.dylibso.chicory.wasm.types.ValueType;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.net.Inet6Address;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.SocketAddress;
import java.net.UnknownHostException;
import java.nio.ByteBuffer;
import java.nio.channels.DatagramChannel;
import java.nio.channels.SelectionKey;
import java.nio.channels.Selector;
import java.nio.channels.ServerSocketChannel;
import java.nio.channels.SocketChannel;
import java.nio.charset.StandardCharsets;
import java.nio.file.Path;
import java.security.SecureRandom;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

public class LuxonServer {

    private static final int O_NONBLOCK = 0x00000800;
    private static final int F_GETFL = 3;
    private static final int F_SETFL = 4;

    private static final int AF_UNSPEC = 0;
    private static final int AF_INET = 2;
    private static final int AF_INET6 = 10;

    private static final int SOCK_STREAM = 1;
    private static final int SOCK_DGRAM = 2;

    private static final int IPPROTO_TCP = 6;
    private static final int IPPROTO_UDP = 17;

    private static final int AI_PASSIVE = 0x0001;
    private static final int AI_CANONNAME = 0x0002;
    private static final int AI_NUMERICHOST = 0x0004;
    private static final int AI_V4MAPPED = 0x0008;
    private static final int AI_ALL = 0x0010;
    private static final int AI_ADDRCONFIG = 0x0020;   // accepted, ignored
    private static final int AI_NUMERICSERV = 0x0400;

    private static final int EAI_BADFLAGS = -1;
    private static final int EAI_NONAME = -2;
    private static final int EAI_AGAIN = -3;
    private static final int EAI_FAIL = -4;
    private static final int EAI_FAMILY = -6;
    private static final int EAI_SOCKTYPE = -7;
    private static final int EAI_SERVICE = -8;
    private static final int EAI_MEMORY = -10;

    private static final int SOCKADDR_IN_LEN = 16;
    private static final int SOCKADDR_IN6_LEN = 28;
    private static final int ADDRINFO_LEN = 32;

    // Emulated POSIX descriptor map starting at 100 to clear standard WASI descriptors (0-3)
    private static final Map<Integer, SocketHandle> sockets = new HashMap<>();
    private static int nextFd = 100;

    private static class SocketHandle {
        final int fd;
        final int family;
        boolean isServer = false;
        boolean isUdp = false;
        boolean nonBlocking = false;
        ServerSocketChannel serverChannel;
        SocketChannel socketChannel;
        DatagramChannel datagramChannel;

        SocketHandle(int fd, int family, boolean isUdp) {
            this.fd = fd;
            this.family = family;
            this.isUdp = isUdp;
        }
    }

    public static void main(String[] args) {
        try {
            WasiOptions wasiOptions = WasiOptions.builder()
                    .withStdout(System.out)
                    .withStderr(System.err)
                    .withDirectory(".", Path.of("."))
                    .build();

            WasiPreview1 wasi = WasiPreview1.builder()
                    .withOptions(wasiOptions)
                    .build();

            List<ImportFunction> functions = new ArrayList<>(Arrays.asList(wasi.toHostFunctions()));

            ImportFunction origPathOpen = null;
            ImportFunction origFdRead = null;
            ImportFunction origFdClose = null;

            for (ImportFunction f : functions) {
                if ("wasi_snapshot_preview1".equals(f.module())) {
                    if ("path_open".equals(f.name())) origPathOpen = f;
                    else if ("fd_read".equals(f.name())) origFdRead = f;
                    else if ("fd_close".equals(f.name())) origFdClose = f;
                }
            }

            final ImportFunction finalPathOpen = origPathOpen;
            final ImportFunction finalFdRead = origFdRead;
            final ImportFunction finalFdClose = origFdClose;

            // Reserve a high virtual FD number that won't collide with WASI or sockets
            final int RANDOM_FD = 9999;
            final SecureRandom secureRandom = new SecureRandom();

            if (finalPathOpen != null) {
                functions.remove(finalPathOpen);
                functions.add(new HostFunction(finalPathOpen.module(), finalPathOpen.name(), finalPathOpen.paramTypes(), finalPathOpen.returnTypes(), (instance, wasmArgs) -> {
                    int pathPtr = (int) wasmArgs[2];
                    int pathLen = (int) wasmArgs[3];
                    int resultFdPtr = (int) wasmArgs[8];

                    Memory mem = instance.memory();
                    String path = new String(mem.readBytes(pathPtr, pathLen), StandardCharsets.UTF_8);

                    if (path.endsWith("/dev/random") || path.endsWith("/dev/urandom") || path.equals("dev/random") || path.equals("dev/urandom")) {
                        writeIntLE(mem, resultFdPtr, RANDOM_FD);
                        return new long[] { 0 }; // WASI_ESUCCESS
                    }
                    return finalPathOpen.handle().apply(instance, wasmArgs);
                }));
            }

            if (finalFdRead != null) {
                functions.remove(finalFdRead);
                functions.add(new HostFunction(finalFdRead.module(), finalFdRead.name(), finalFdRead.paramTypes(), finalFdRead.returnTypes(), (instance, wasmArgs) -> {
                    int fd = (int) wasmArgs[0];
                    if (fd == RANDOM_FD) {
                        int iovsPtr = (int) wasmArgs[1];
                        int iovsLen = (int) wasmArgs[2];
                        int resultSizePtr = (int) wasmArgs[3];
                        Memory mem = instance.memory();

                        int totalRead = 0;
                        for (int i = 0; i < iovsLen; i++) {
                            int bufPtr = readIntLE(mem, iovsPtr + (i * 8));
                            int bufLen = readIntLE(mem, iovsPtr + (i * 8) + 4);

                            byte[] randBytes = new byte[bufLen];
                            secureRandom.nextBytes(randBytes);
                            mem.write(bufPtr, randBytes);
                            totalRead += bufLen;
                        }
                        writeIntLE(mem, resultSizePtr, totalRead);
                        return new long[] { 0 }; // WASI_ESUCCESS
                    }
                    return finalFdRead.handle().apply(instance, wasmArgs);
                }));
            }

            if (finalFdClose != null) {
                functions.remove(finalFdClose);
                functions.add(new HostFunction(finalFdClose.module(), finalFdClose.name(), finalFdClose.paramTypes(), finalFdClose.returnTypes(), (instance, wasmArgs) -> {
                    int fd = (int) wasmArgs[0];
                    if (fd == RANDOM_FD) {
                        return new long[] { 0 }; // WASI_ESUCCESS
                    }
                    return finalFdClose.handle().apply(instance, wasmArgs);
                }));
            }

            functions.addAll(registerSocketEnvironmentImports());

            ImportValues imports = ImportValues.builder()
                    .withFunctions(functions)
                    .build();

            var module = LuxonServerModule.load();
            Instance instance = Instance.builder(module)
                    .withMachineFactory(LuxonServerModule::create)
                    .withImportValues(imports)
                    .build();

            ExportFunction startFn = instance.export("_start");
            if (startFn != null) {
                startFn.apply();
            }

        } catch (Exception e) {
            System.err.println("Fatal execution error inside WASM host wrapper: " + e.getMessage());
            e.printStackTrace();
            System.exit(1);
        }
    }

    private static List<ImportFunction> registerSocketEnvironmentImports() {
        List<ImportFunction> env = new ArrayList<>();
        final String NS = "env";

        // u32 w2c_env_socket_socket(domain, type, protocol) -> 3 params
        env.add(new HostFunction(NS, "socket_socket", params(3), returns(1), (instance, args) -> {
            int domain = (int) args[0];
            int type = (int) args[1];
            boolean isUdp = (type == SOCK_DGRAM);
            int fd = nextFd++;
            sockets.put(fd, new SocketHandle(fd, domain, isUdp));
            return new long[] { fd };
        }));

        // u32 w2c_env_socket_bind(sockfd, addr_ptr, addrlen) -> 3 params
        env.add(new HostFunction(NS, "socket_bind", params(3), returns(1), (instance, args) -> {
            int sockfd = (int) args[0];
            int addrPtr = (int) args[1];
            int addrlen = (int) args[2];
            SocketHandle h = sockets.get(sockfd);
            if (h == null) return new long[] { -1L };

            try {
                Memory mem = instance.memory();
                int family = mem.readBytes(addrPtr, 1)[0] & 0xFF;
                int port = ((mem.readBytes(addrPtr + 2, 2)[0] & 0xFF) << 8) | (mem.readBytes(addrPtr + 2, 2)[1] & 0xFF);

                InetAddress addr = (family == AF_INET6 || addrlen == 28) ?
                    InetAddress.getByAddress(mem.readBytes(addrPtr + 8, 16)) :
                    InetAddress.getByAddress(mem.readBytes(addrPtr + 4, 4));

                try {
                    if (h.isUdp) {
                        h.datagramChannel = DatagramChannel.open();
                        h.datagramChannel.configureBlocking(!h.nonBlocking);
                        h.datagramChannel.bind(new InetSocketAddress(addr, port));
                    } else {
                        h.isServer = true;
                        h.serverChannel = ServerSocketChannel.open();
                        h.serverChannel.configureBlocking(!h.nonBlocking);
                        h.serverChannel.bind(new InetSocketAddress(addr, port));
                    }
                } catch (java.nio.channels.UnsupportedAddressTypeException e) {
                    InetAddress fallback = getFallbackAddress(addr);
                    if (fallback != null) {
                        System.err.println("WARNING: Unsupported address type for bind. Falling back from " + addr + " to " + fallback);
                        if (h.isUdp) {
                            h.datagramChannel.bind(new InetSocketAddress(fallback, port));
                        } else {
                            h.serverChannel.bind(new InetSocketAddress(fallback, port));
                        }
                    } else {
                        throw e;
                    }
                }
                return new long[] { 0 };
            } catch (Exception e) {
                return new long[] { -1L };
            }
        }));

        // u32 w2c_env_socket_listen(sockfd, backlog) -> 2 params
        env.add(new HostFunction(NS, "socket_listen", params(2), returns(1), (instance, args) -> {
            SocketHandle h = sockets.get((int) args[0]);
            if (h != null && h.isUdp) return new long[] { 0 };
            return new long[] { (h != null && h.isServer) ? 0 : -1L };
        }));

        // u32 w2c_env_socket_accept(sockfd, addr_ptr, addrlen_ptr) -> 3 params
        env.add(new HostFunction(NS, "socket_accept", params(3), returns(1), (instance, args) -> {
            int sockfd = (int) args[0];
            int addrPtr = (int) args[1];
            int addrlenPtr = (int) args[2];

            SocketHandle h = sockets.get(sockfd);
            if (h == null || h.isUdp || !h.isServer || h.serverChannel == null) {
                return new long[] { -1L };
            }

            // POSIX ABI: if addr != NULL, addrlen must be a valid value-result pointer.
            if (addrPtr != 0 && addrlenPtr == 0) {
                return new long[] { -1L };
            }

            try {
                SocketChannel client = h.serverChannel.accept();
                if (client == null) {
                    return new long[] { -1L };
                }

                client.configureBlocking(true);

                int clientFd = nextFd++;
                SocketHandle clientHandle = new SocketHandle(clientFd, h.family, false);
                clientHandle.socketChannel = client;
                sockets.put(clientFd, clientHandle);

                if (addrPtr != 0) {
                    SocketAddress remote = client.getRemoteAddress();
                    if (!(remote instanceof InetSocketAddress) ||
                        !writeSockaddrResult(instance.memory(), addrPtr, addrlenPtr, (InetSocketAddress) remote, h.family)) {
                        closeHandle(clientFd);
                        return new long[] { -1L };
                    }
                }

                return new long[] { clientFd };
            } catch (IOException e) {
                return new long[] { -1L };
            }
        }));

        // u32 w2c_env_socket_connect(sockfd, addr_ptr, addrlen) -> 3 params
        env.add(new HostFunction(NS, "socket_connect", params(3), returns(1), (instance, args) -> {
            int sockfd = (int) args[0];
            int addrPtr = (int) args[1];
            int addrlen = (int) args[2];
            SocketHandle h = sockets.get(sockfd);
            if (h == null) return new long[] { -1L };

            try {
                Memory mem = instance.memory();
                int family = mem.readBytes(addrPtr, 1)[0] & 0xFF;
                int port = ((mem.readBytes(addrPtr + 2, 2)[0] & 0xFF) << 8) | (mem.readBytes(addrPtr + 2, 2)[1] & 0xFF);
                InetAddress addr = (family == AF_INET6 || addrlen == 28) ?
                    InetAddress.getByAddress(mem.readBytes(addrPtr + 8, 16)) :
                    InetAddress.getByAddress(mem.readBytes(addrPtr + 4, 4));

                try {
                    if (h.isUdp) {
                        h.datagramChannel = DatagramChannel.open();
                        h.datagramChannel.configureBlocking(!h.nonBlocking);
                        h.datagramChannel.connect(new InetSocketAddress(addr, port));
                        return new long[] { 0 };
                    } else {
                        h.socketChannel = SocketChannel.open();
                        h.socketChannel.configureBlocking(!h.nonBlocking);
                        boolean success = h.socketChannel.connect(new InetSocketAddress(addr, port));
                        return new long[] { success ? 0 : -1L };
                    }
                } catch (java.nio.channels.UnsupportedAddressTypeException e) {
                    InetAddress fallback = getFallbackAddress(addr);
                    if (fallback != null) {
                        System.err.println("WARNING: Unsupported address type for connect. Falling back from " + addr + " to " + fallback);
                        if (h.isUdp) {
                            h.datagramChannel.connect(new InetSocketAddress(fallback, port));
                            return new long[] { 0 };
                        } else {
                            boolean success = h.socketChannel.connect(new InetSocketAddress(fallback, port));
                            return new long[] { success ? 0 : -1L };
                        }
                    } else {
                        throw e; // Rethrow if we can't formulate a valid fallback
                    }
                }
            } catch (Exception e) {
                return new long[] { -1L };
            }
        }));

        env.add(new HostFunction(NS, "socket_send", params(4), returns(1), (instance, args) ->
            doSend(instance, (int) args[0], (int) args[1], (int) args[2], (int) args[3], 0, 0)
        ));

        env.add(new HostFunction(NS, "socket_recv", params(4), returns(1), (instance, args) ->
            doRecv(instance, (int) args[0], (int) args[1], (int) args[2], (int) args[3], 0, 0)
        ));

        env.add(new HostFunction(NS, "socket_sendto", params(6), returns(1), (instance, args) ->
            doSend(instance, (int) args[0], (int) args[1], (int) args[2], (int) args[3], (int) args[4], (int) args[5])
        ));

        env.add(new HostFunction(NS, "socket_recvfrom", params(6), returns(1), (instance, args) ->
            doRecv(instance, (int) args[0], (int) args[1], (int) args[2], (int) args[3], (int) args[4], (int) args[5])
        ));

        env.add(new HostFunction(NS, "socket_setsockopt", params(5), returns(1), (instance, args) -> new long[] { 0 }));
        env.add(new HostFunction(NS, "socket_shutdown", params(2), returns(1), (instance, args) -> closeHandle((int) args[0])));
        env.add(new HostFunction(NS, "socket_close", params(1), returns(1), (instance, args) -> closeHandle((int) args[0])));

        // u32 w2c_env_socket_fcntl(fd, cmd, arg) -> 3 params
        env.add(new HostFunction(NS, "socket_fcntl", params(3), returns(1), (instance, args) -> {
            SocketHandle h = sockets.get((int) args[0]);
            if (h == null) return new long[] { -1L };
            int cmd = (int) args[1];
            int arg = (int) args[2];

            if (cmd == F_GETFL) {
                return new long[] { h.nonBlocking ? O_NONBLOCK : 0 };
            } else if (cmd == F_SETFL) {
                h.nonBlocking = (arg & O_NONBLOCK) != 0;
                try {
                    if (h.serverChannel != null) h.serverChannel.configureBlocking(!h.nonBlocking);
                    if (h.datagramChannel != null) h.datagramChannel.configureBlocking(!h.nonBlocking);
                    if (h.socketChannel != null) h.socketChannel.configureBlocking(!h.nonBlocking);
                    return new long[] { 0 };
                } catch (IOException e) {
                    return new long[] { -1L };
                }
            }
            return new long[] { -1L };
        }));

        env.add(new HostFunction(NS, "socket_ioctl", params(3), returns(1), (instance, args) -> new long[] { 0 }));

        env.add(new HostFunction(NS, "socket_inet_pton", params(3), returns(1), (instance, args) -> {
            int af = (int) args[0];
            int srcPtr = (int) args[1];
            int dstPtr = (int) args[2];

            try {
                Memory mem = instance.memory();
                String src = readNullTerminatedString(mem, srcPtr);

                byte[] parsed;
                if (af == AF_INET) {
                    parsed = parseIpv4Literal(src);
                } else if (af == AF_INET6) {
                    parsed = parseIpv6Literal(src);
                } else {
                    // POSIX: unsupported family => -1 / EAFNOSUPPORT.
                    return new long[] { -1L };
                }

                if (parsed == null) {
                    // Invalid presentation format.
                    return new long[] { 0 };
                }

                mem.write(dstPtr, parsed);
                return new long[] { 1 };
            } catch (Exception e) {
                return new long[] { 0 };
            }
        }));

        env.add(new HostFunction(NS, "socket_inet_ntop", params(4), returns(1), (instance, args) -> {
            try {
                Memory mem = instance.memory();
                int af = (int) args[0];
                byte[] ipBytes = mem.readBytes((int) args[1], af == AF_INET6 ? 16 : 4);
                byte[] strBytes = InetAddress.getByAddress(ipBytes).getHostAddress().getBytes(StandardCharsets.UTF_8);
                if (strBytes.length + 1 <= (int) args[3]) {
                    mem.write((int) args[2], strBytes);
                    mem.write((int) args[2] + strBytes.length, new byte[] { 0 });
                    return new long[] { args[2] };
                }
            } catch (Exception e) {}
            return new long[] { 0 };
        }));

        // Selector loop evaluating Read/Write states asynchronously
        env.add(new HostFunction(NS, "socket_select", params(5), returns(1), (instance, args) -> {
            int nfds = (int) args[0];
            int readfdsPtr = (int) args[1];
            int writefdsPtr = (int) args[2];
            int exceptfdsPtr = (int) args[3];
            int timeoutPtr = (int) args[4];
            Memory mem = instance.memory();

            long timeoutMs = 0;
            boolean hasTimeout = false;
            if (timeoutPtr != 0) {
                hasTimeout = true;
                long sec = readLongLE(mem, timeoutPtr);
                long usec = readLongLE(mem, timeoutPtr + 8);
                timeoutMs = (sec * 1000) + (usec / 1000);
            }

            try (Selector selector = Selector.open()) {
                Map<SelectionKey, Integer> keyToFd = new HashMap<>();

                int bytesToClear = (nfds + 7) / 8;

                byte[] rfds = readfdsPtr != 0 && bytesToClear > 0 ? mem.readBytes(readfdsPtr, bytesToClear) : null;
                byte[] wfds = writefdsPtr != 0 && bytesToClear > 0 ? mem.readBytes(writefdsPtr, bytesToClear) : null;

                if (readfdsPtr != 0 && bytesToClear > 0) mem.write(readfdsPtr, new byte[bytesToClear]);
                if (writefdsPtr != 0 && bytesToClear > 0) mem.write(writefdsPtr, new byte[bytesToClear]);
                if (exceptfdsPtr != 0 && bytesToClear > 0) mem.write(exceptfdsPtr, new byte[bytesToClear]);

                for (int fd = 0; fd < nfds; fd++) {
                    boolean wantsRead = rfds != null && (rfds[fd / 8] & (1 << (fd % 8))) != 0;
                    boolean wantsWrite = wfds != null && (wfds[fd / 8] & (1 << (fd % 8))) != 0;

                    if (wantsRead || wantsWrite) {
                        SocketHandle h = sockets.get(fd);
                        if (h != null) {
                            int ops = 0;
                            if (wantsRead) {
                                if (h.isServer && h.serverChannel != null) ops |= SelectionKey.OP_ACCEPT;
                                else ops |= SelectionKey.OP_READ;
                            }
                            if (wantsWrite && !h.isServer) {
                                ops |= SelectionKey.OP_WRITE;
                            }

                            if (ops != 0) {
                                SelectionKey key = null;
                                if (h.isServer && h.serverChannel != null) {
                                    if (h.serverChannel.isBlocking()) h.serverChannel.configureBlocking(false);
                                    key = h.serverChannel.register(selector, ops);
                                } else if (h.isUdp && h.datagramChannel != null) {
                                    if (h.datagramChannel.isBlocking()) h.datagramChannel.configureBlocking(false);
                                    key = h.datagramChannel.register(selector, ops);
                                } else if (h.socketChannel != null) {
                                    if (h.socketChannel.isBlocking()) h.socketChannel.configureBlocking(false);
                                    key = h.socketChannel.register(selector, ops);
                                }
                                if (key != null) keyToFd.put(key, fd);
                            }
                        }
                    }
                }

                int readyCount = 0;
                if (!keyToFd.isEmpty()) {
                    if (hasTimeout) {
                        if (timeoutMs == 0) selector.selectNow();
                        else selector.select(timeoutMs);
                    } else {
                        selector.select();
                    }

                    for (SelectionKey key : selector.selectedKeys()) {
                        int fd = keyToFd.get(key);
                        if (readfdsPtr != 0 && (key.isReadable() || key.isAcceptable())) {
                            byte[] b = mem.readBytes(readfdsPtr + (fd / 8), 1);
                            b[0] |= (byte) (1 << (fd % 8));
                            mem.write(readfdsPtr + (fd / 8), b);
                            readyCount++;
                        }
                        if (writefdsPtr != 0 && key.isWritable()) {
                            byte[] b = mem.readBytes(writefdsPtr + (fd / 8), 1);
                            b[0] |= (byte) (1 << (fd % 8));
                            mem.write(writefdsPtr + (fd / 8), b);
                            readyCount++;
                        }
                    }
                } else if (hasTimeout && timeoutMs > 0) {
                    Thread.sleep(timeoutMs);
                }

                for (SelectionKey key : keyToFd.keySet()) key.cancel();
                selector.selectNow();

                for (int fd : keyToFd.values()) {
                    SocketHandle h = sockets.get(fd);
                    if (h != null) {
                        if (h.isServer && h.serverChannel != null) h.serverChannel.configureBlocking(!h.nonBlocking);
                        else if (h.isUdp && h.datagramChannel != null) h.datagramChannel.configureBlocking(!h.nonBlocking);
                        else if (h.socketChannel != null) h.socketChannel.configureBlocking(!h.nonBlocking);
                    }
                }
                return new long[] { readyCount };
            } catch (Exception e) {
                return new long[] { -1L };
            }
        }));

        env.add(new HostFunction(NS, "socket_getaddrinfo", params(4), returns(1), (instance, args) -> {
            Memory mem = instance.memory();

            int nodePtr = (int) args[0];
            int servicePtr = (int) args[1];
            int hintsPtr = (int) args[2];
            int resPtr = (int) args[3];

            if (resPtr == 0) {
                return new long[] { EAI_FAIL };
            }
            writeIntLE(mem, resPtr, 0);

            String node = nodePtr != 0 ? readNullTerminatedString(mem, nodePtr) : null;
            String service = servicePtr != 0 ? readNullTerminatedString(mem, servicePtr) : null;

            if (node == null && service == null) {
                return new long[] { EAI_NONAME };
            }

            int flags = 0;
            int family = AF_UNSPEC;
            int socktype = 0;
            int protocol = 0;

            if (hintsPtr != 0) {
                flags = readIntLE(mem, hintsPtr + 0);
                family = readIntLE(mem, hintsPtr + 4);
                socktype = readIntLE(mem, hintsPtr + 8);
                protocol = readIntLE(mem, hintsPtr + 12);
            }

            int supportedFlags =
                AI_PASSIVE | AI_CANONNAME | AI_NUMERICHOST |
                AI_V4MAPPED | AI_ALL | AI_ADDRCONFIG | AI_NUMERICSERV;

            if ((flags & ~supportedFlags) != 0) {
                return new long[] { EAI_BADFLAGS };
            }

            if (family != AF_UNSPEC && family != AF_INET && family != AF_INET6) {
                return new long[] { EAI_FAMILY };
            }

            if (socktype != 0 && socktype != SOCK_STREAM && socktype != SOCK_DGRAM) {
                return new long[] { EAI_SOCKTYPE };
            }

            if (protocol != 0 && protocol != IPPROTO_TCP && protocol != IPPROTO_UDP) {
                return new long[] { EAI_SERVICE };
            }

            if ((socktype == SOCK_STREAM && protocol == IPPROTO_UDP) ||
                (socktype == SOCK_DGRAM && protocol == IPPROTO_TCP)) {
                return new long[] { EAI_SERVICE };
            }

            int port = 0;
            if (service != null) {
                Integer parsedPort = parseNumericService(service);
                if (parsedPort == null) {
                    return new long[] { EAI_SERVICE };
                }
                port = parsedPort;
            }

            List<InetAddress> addresses;
            boolean numericNode = false;

            try {
                if (node == null) {
                    addresses = defaultAddressesForNullNode(family, (flags & AI_PASSIVE) != 0);
                } else {
                    byte[] v4 = parseIpv4Literal(node);
                    byte[] v6 = (v4 == null) ? parseIpv6Literal(node) : null;
                    numericNode = (v4 != null || v6 != null);

                    if (numericNode) {
                        addresses = resolveNumericNode(node, family, flags, v4, v6);
                    } else {
                        if ((flags & AI_NUMERICHOST) != 0) {
                            return new long[] { EAI_NONAME };
                        }
                        addresses = resolveDnsNode(node, family, flags);
                    }
                }
            } catch (UnknownHostException e) {
                return new long[] { EAI_NONAME };
            } catch (Exception e) {
                return new long[] { EAI_FAIL };
            }

            if (addresses.isEmpty()) {
                return new long[] { EAI_NONAME };
            }

            List<SockProtoPair> pairs = buildSockProtoPairs(socktype, protocol);
            if (pairs.isEmpty()) {
                return new long[] { EAI_SERVICE };
            }

            String canonName = ((flags & AI_CANONNAME) != 0 && node != null) ? node : null;

            List<AddrInfoResult> results = new ArrayList<>();
            boolean first = true;

            for (InetAddress addr : addresses) {
                byte[] raw = addr.getAddress();
                int outFamily = raw.length == 16 ? AF_INET6 : AF_INET;
                int scopeId = (addr instanceof Inet6Address) ? ((Inet6Address) addr).getScopeId() : 0;
                byte[] sockaddr = encodeSockaddr(outFamily, raw, port, scopeId);

                for (SockProtoPair pair : pairs) {
                    results.add(new AddrInfoResult(
                        outFamily,
                        pair.socktype,
                        pair.protocol,
                        sockaddr,
                        first ? canonName : null
                    ));
                    first = false;
                }
            }

            ExportFunction malloc = findExport(instance, "malloc", "_malloc");
            ExportFunction free = findExport(instance, "free", "_free");
            if (malloc == null) {
                return new long[] { EAI_MEMORY };
            }

            List<Integer> allocated = new ArrayList<>();

            try {
                int head = 0;
                int prev = 0;

                for (AddrInfoResult r : results) {
                    int saPtr = guestMalloc(malloc, r.sockaddr.length);
                    if (saPtr == 0) throw new OutOfMemoryError();
                    allocated.add(saPtr);
                    mem.write(saPtr, r.sockaddr);

                    int canonPtr = 0;
                    if (r.canonName != null) {
                        byte[] nameBytes = r.canonName.getBytes(StandardCharsets.UTF_8);
                        canonPtr = guestMalloc(malloc, nameBytes.length + 1);
                        if (canonPtr == 0) throw new OutOfMemoryError();
                        allocated.add(canonPtr);
                        mem.write(canonPtr, nameBytes);
                        mem.write(canonPtr + nameBytes.length, new byte[] { 0 });
                    }

                    int aiPtr = guestMalloc(malloc, ADDRINFO_LEN);
                    if (aiPtr == 0) throw new OutOfMemoryError();
                    allocated.add(aiPtr);

                    writeIntLE(mem, aiPtr + 0, 0);
                    writeIntLE(mem, aiPtr + 4, r.family);
                    writeIntLE(mem, aiPtr + 8, r.socktype);
                    writeIntLE(mem, aiPtr + 12, r.protocol);
                    writeIntLE(mem, aiPtr + 16, r.sockaddr.length);
                    writeIntLE(mem, aiPtr + 20, saPtr);
                    writeIntLE(mem, aiPtr + 24, canonPtr);
                    writeIntLE(mem, aiPtr + 28, 0);

                    if (head == 0) {
                        head = aiPtr;
                    } else {
                        writeIntLE(mem, prev + 28, aiPtr);
                    }
                    prev = aiPtr;
                }

                writeIntLE(mem, resPtr, head);
                return new long[] { 0 };
            } catch (Throwable t) {
                if (free != null) {
                    for (int i = allocated.size() - 1; i >= 0; i--) {
                        int ptr = allocated.get(i);
                        if (ptr != 0) free.apply(ptr);
                    }
                }
                writeIntLE(mem, resPtr, 0);
                return new long[] { EAI_MEMORY };
            }
        }));

        env.add(new HostFunction(NS, "socket_freeaddrinfo", params(1), returns(0), (instance, args) -> {
            ExportFunction free = instance.export("free");
            if (free == null) free = instance.export("_free");
            if (free == null) return new long[0];

            Memory mem = instance.memory();
            int curr = (int) args[0];
            while (curr != 0) {
                int addr = readIntLE(mem, curr + 20);
                int canon = readIntLE(mem, curr + 24);
                int next = readIntLE(mem, curr + 28);

                if (addr != 0) free.apply(addr);
                if (canon != 0) free.apply(canon);
                free.apply(curr);
                curr = next;
            }
            return new long[0];
        }));

        return env;
    }

private static InetAddress getFallbackAddress(InetAddress addr) {
        try {
            byte[] raw = addr.getAddress();
            if (raw.length == 16) {
                // Check if it's an IPv4-mapped IPv6 (::ffff:x.x.x.x)
                if (isIpv4MappedIpv6(raw)) {
                    return InetAddress.getByAddress(extractMappedIpv4(raw));
                }

                // Check for IPv6 ANY (::) -> Translate to IPv4 ANY (0.0.0.0)
                boolean isAny = true;
                for (byte b : raw) {
                    if (b != 0) {
                        isAny = false;
                        break;
                    }
                }
                if (isAny) {
                    return InetAddress.getByAddress(new byte[]{0, 0, 0, 0});
                }

                // Check for IPv6 Loopback (::1) -> Translate to IPv4 Loopback (127.0.0.1)
                boolean isLoopback = true;
                for (int i = 0; i < 15; i++) {
                    if (raw[i] != 0) {
                        isLoopback = false;
                        break;
                    }
                }
                if (isLoopback && raw[15] == 1) {
                    return InetAddress.getByAddress(new byte[]{127, 0, 0, 1});
                }

            } else if (raw.length == 4) {
                // Try promoting IPv4 up to IPv6 as a last resort
                return InetAddress.getByAddress(ipv4ToMappedIpv6(raw));
            }
        } catch (UnknownHostException e) {
            // Ignore, will return null
        }
        return null;
    }

    private static long[] doSend(Instance instance, int sockfd, int bufPtr, int len, int flags, int destAddrPtr, int addrlen) {
        SocketHandle h = sockets.get(sockfd);
        if (h == null) return new long[] { -1L };
        try {
            Memory mem = instance.memory();
            byte[] data = mem.readBytes(bufPtr, len);
            ByteBuffer buf = ByteBuffer.wrap(data);

            if (h.isUdp && h.datagramChannel != null) {
                if (destAddrPtr != 0) {
                    int family = mem.readBytes(destAddrPtr, 1)[0] & 0xFF;
                    int port = ((mem.readBytes(destAddrPtr + 2, 2)[0] & 0xFF) << 8) | (mem.readBytes(destAddrPtr + 2, 2)[1] & 0xFF);
                    InetAddress addr = (family == AF_INET6 || addrlen == 28) ?
                        InetAddress.getByAddress(mem.readBytes(destAddrPtr + 8, 16)) :
                        InetAddress.getByAddress(mem.readBytes(destAddrPtr + 4, 4));
                    int sent = h.datagramChannel.send(buf, new InetSocketAddress(addr, port));
                    return new long[] { sent };
                } else {
                    return new long[] { h.datagramChannel.write(buf) };
                }
            } else if (h.socketChannel != null) {
                return new long[] { h.socketChannel.write(buf) };
            }
            return new long[] { -1L };
        } catch (IOException e) {
            return new long[] { -1L };
        }
    }

    // Dynamic address structure promotion mapped securely to guest memory buffers
    private static long[] doRecv(Instance instance, int sockfd, int bufPtr, int len, int flags, int srcAddrPtr, int addrlenPtr) {
        SocketHandle h = sockets.get(sockfd);
        if (h == null) return new long[] { -1L };

        // POSIX ABI: if src_addr != NULL, addrlen must be a valid value-result pointer.
        if (srcAddrPtr != 0 && addrlenPtr == 0) {
            return new long[] { -1L };
        }

        try {
            Memory mem = instance.memory();
            ByteBuffer buf = ByteBuffer.allocate(len);

            if (h.isUdp && h.datagramChannel != null) {
                SocketAddress sender = h.datagramChannel.receive(buf);
                if (sender == null) {
                    return new long[] { -1L };
                }

                buf.flip();
                mem.write(bufPtr, Arrays.copyOf(buf.array(), buf.limit()));

                if (srcAddrPtr != 0) {
                    if (!(sender instanceof InetSocketAddress) ||
                        !writeSockaddrResult(mem, srcAddrPtr, addrlenPtr, (InetSocketAddress) sender, h.family)) {
                        return new long[] { -1L };
                    }
                }

                return new long[] { buf.limit() };
            } else if (h.socketChannel != null) {
                int read = h.socketChannel.read(buf);
                if (read > 0) {
                    mem.write(bufPtr, Arrays.copyOf(buf.array(), read));
                    return new long[] { read };
                }
                return new long[] { read == -1 ? 0 : -1L };
            }

            return new long[] { -1L };
        } catch (IOException e) {
            return new long[] { -1L };
        }
    }

    private static long[] closeHandle(int fd) {
        SocketHandle h = sockets.remove(fd);
        if (h != null) {
            try { if (h.serverChannel != null) h.serverChannel.close(); } catch (IOException e) {}
            try { if (h.datagramChannel != null) h.datagramChannel.close(); } catch (IOException e) {}
            try { if (h.socketChannel != null) h.socketChannel.close(); } catch (IOException e) {}
        }
        return new long[] { 0 };
    }

    private static List<ValueType> params(int count) {
        List<ValueType> res = new ArrayList<>(count);
        for (int i = 0; i < count; i++) res.add(ValueType.I32);
        return res;
    }

    private static List<ValueType> returns(int count) {
        return count == 0 ? List.of() : List.of(ValueType.I32);
    }

    private static void writeIntLE(Memory mem, int offset, int val) {
        mem.write(offset, new byte[] {
            (byte) val, (byte) (val >> 8), (byte) (val >> 16), (byte) (val >> 24)
        });
    }

    private static int readIntLE(Memory mem, int offset) {
        byte[] b = mem.readBytes(offset, 4);
        return (b[0] & 0xFF) | ((b[1] & 0xFF) << 8) | ((b[2] & 0xFF) << 16) | ((b[3] & 0xFF) << 24);
    }

    private static long readLongLE(Memory mem, int offset) {
        byte[] b = mem.readBytes(offset, 8);
        long res = 0;
        for (int i = 0; i < 8; i++) {
            res |= ((long) (b[i] & 0xFF) << (8 * i));
        }
        return res;
    }

    private static String readNullTerminatedString(Memory mem, int offset) {
        ByteArrayOutputStream baos = new ByteArrayOutputStream();
        int curr = offset;
        while (true) {
            byte[] b = mem.readBytes(curr++, 1);
            if (b[0] == 0) break;
            baos.write(b[0]);
        }
        return baos.toString(StandardCharsets.UTF_8);
    }

    private static final class SockProtoPair {
        final int socktype;
        final int protocol;

        SockProtoPair(int socktype, int protocol) {
            this.socktype = socktype;
            this.protocol = protocol;
        }
    }

    private static final class AddrInfoResult {
        final int family;
        final int socktype;
        final int protocol;
        final byte[] sockaddr;
        final String canonName;

        AddrInfoResult(int family, int socktype, int protocol, byte[] sockaddr, String canonName) {
            this.family = family;
            this.socktype = socktype;
            this.protocol = protocol;
            this.sockaddr = sockaddr;
            this.canonName = canonName;
        }
    }

    private static ExportFunction findExport(Instance instance, String primary, String fallback) {
        ExportFunction fn = instance.export(primary);
        return fn != null ? fn : instance.export(fallback);
    }

    private static int guestMalloc(ExportFunction malloc, int size) {
        return (int) malloc.apply(size)[0];
    }

    private static void putU16LE(byte[] out, int off, int v) {
        out[off] = (byte) (v & 0xFF);
        out[off + 1] = (byte) ((v >>> 8) & 0xFF);
    }

    private static void putU16BE(byte[] out, int off, int v) {
        out[off] = (byte) ((v >>> 8) & 0xFF);
        out[off + 1] = (byte) (v & 0xFF);
    }

    private static void putIntLE(byte[] out, int off, int v) {
        out[off] = (byte) (v & 0xFF);
        out[off + 1] = (byte) ((v >>> 8) & 0xFF);
        out[off + 2] = (byte) ((v >>> 16) & 0xFF);
        out[off + 3] = (byte) ((v >>> 24) & 0xFF);
    }

    private static byte[] ipv4ToMappedIpv6(byte[] v4) {
        byte[] out = new byte[16];
        out[10] = (byte) 0xFF;
        out[11] = (byte) 0xFF;
        System.arraycopy(v4, 0, out, 12, 4);
        return out;
    }

    private static boolean isIpv4MappedIpv6(byte[] v6) {
        if (v6 == null || v6.length != 16) return false;
        for (int i = 0; i < 10; i++) {
            if (v6[i] != 0) return false;
        }
        return v6[10] == (byte) 0xFF && v6[11] == (byte) 0xFF;
    }

    private static byte[] extractMappedIpv4(byte[] v6) {
        return Arrays.copyOfRange(v6, 12, 16);
    }

    private static byte[] encodeSockaddr(int family, byte[] addr, int port, int scopeId) {
        if (family == AF_INET) {
            if (addr.length != 4) throw new IllegalArgumentException("AF_INET requires 4 bytes");
            byte[] out = new byte[SOCKADDR_IN_LEN];
            putU16LE(out, 0, AF_INET);
            putU16BE(out, 2, port);
            System.arraycopy(addr, 0, out, 4, 4);
            return out;
        }

        if (family == AF_INET6) {
            if (addr.length != 16) throw new IllegalArgumentException("AF_INET6 requires 16 bytes");
            byte[] out = new byte[SOCKADDR_IN6_LEN];
            putU16LE(out, 0, AF_INET6);
            putU16BE(out, 2, port);
            putIntLE(out, 4, 0); // flowinfo
            System.arraycopy(addr, 0, out, 8, 16);
            putIntLE(out, 24, scopeId);
            return out;
        }

        throw new IllegalArgumentException("unsupported family: " + family);
    }

    private static boolean writeSockaddrResult(Memory mem, int addrPtr, int addrlenPtr, InetSocketAddress remote, int socketFamily) {
        if (addrPtr == 0) {
            return true;
        }
        if (addrlenPtr == 0) {
            return false;
        }

        InetAddress inet = remote.getAddress();
        if (inet == null) {
            return false;
        }

        byte[] raw = inet.getAddress();
        int family;
        int scopeId = 0;

        if (socketFamily == AF_INET6) {
            family = AF_INET6;
            if (raw.length == 4) {
                raw = ipv4ToMappedIpv6(raw);
            }
            if (inet instanceof Inet6Address) {
                scopeId = ((Inet6Address) inet).getScopeId();
            }
        } else {
            family = AF_INET;
            if (raw.length == 16) {
                if (!isIpv4MappedIpv6(raw)) {
                    return false;
                }
                raw = extractMappedIpv4(raw);
            }
        }

        byte[] sockaddr = encodeSockaddr(family, raw, remote.getPort(), scopeId);
        int callerLen = Math.max(0, readIntLE(mem, addrlenPtr));
        int copyLen = Math.min(callerLen, sockaddr.length);

        if (copyLen > 0) {
            mem.write(addrPtr, Arrays.copyOf(sockaddr, copyLen));
        }
        writeIntLE(mem, addrlenPtr, sockaddr.length);
        return true;
    }

    private static Integer parseNumericService(String s) {
        if (s == null || s.isEmpty()) return null;
        for (int i = 0; i < s.length(); i++) {
            if (!Character.isDigit(s.charAt(i))) return null;
        }
        try {
            int port = Integer.parseInt(s);
            return (port >= 0 && port <= 65535) ? port : null;
        } catch (NumberFormatException e) {
            return null;
        }
    }

    private static int hexValue(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
        if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
        return -1;
    }

    private static byte[] parseIpv4Literal(String s) {
        if (s == null || s.isEmpty()) return null;
        String[] parts = s.split("\\.", -1);
        if (parts.length != 4) return null;

        byte[] out = new byte[4];
        for (int i = 0; i < 4; i++) {
            String p = parts[i];
            if (p.isEmpty() || p.length() > 3) return null;
            int v = 0;
            for (int j = 0; j < p.length(); j++) {
                char c = p.charAt(j);
                if (!Character.isDigit(c)) return null;
                v = (v * 10) + (c - '0');
            }
            if (v < 0 || v > 255) return null;
            out[i] = (byte) v;
        }
        return out;
    }

    private static List<Integer> parseIpv6Section(String part, boolean allowIpv4Tail) {
        List<Integer> words = new ArrayList<>();
        if (part.isEmpty()) return words;

        String[] tokens = part.split(":", -1);
        for (int i = 0; i < tokens.length; i++) {
            String token = tokens[i];
            if (token.isEmpty()) return null;

            if (token.indexOf('.') >= 0) {
                if (!allowIpv4Tail || i != tokens.length - 1) return null;
                byte[] v4 = parseIpv4Literal(token);
                if (v4 == null) return null;
                words.add(((v4[0] & 0xFF) << 8) | (v4[1] & 0xFF));
                words.add(((v4[2] & 0xFF) << 8) | (v4[3] & 0xFF));
            } else {
                if (token.length() > 4) return null;
                int value = 0;
                for (int j = 0; j < token.length(); j++) {
                    int hv = hexValue(token.charAt(j));
                    if (hv < 0) return null;
                    value = (value << 4) | hv;
                }
                words.add(value);
            }
        }

        return words;
    }

    private static byte[] parseIpv6Literal(String s) {
        if (s == null || s.isEmpty()) return null;
        if (s.indexOf('%') >= 0) return null; // inet_pton does not accept zone ids

        String[] halves = s.split("::", -1);
        if (halves.length > 2) return null;

        List<Integer> words = new ArrayList<>(8);

        if (halves.length == 1) {
            List<Integer> all = parseIpv6Section(s, true);
            if (all == null || all.size() != 8) return null;
            words.addAll(all);
        } else {
            List<Integer> left = parseIpv6Section(halves[0], false);
            List<Integer> right = parseIpv6Section(halves[1], true);
            if (left == null || right == null) return null;

            int zeros = 8 - (left.size() + right.size());
            if (zeros < 1) return null;

            words.addAll(left);
            for (int i = 0; i < zeros; i++) words.add(0);
            words.addAll(right);
        }

        if (words.size() != 8) return null;

        byte[] out = new byte[16];
        for (int i = 0; i < 8; i++) {
            int w = words.get(i);
            out[i * 2] = (byte) ((w >>> 8) & 0xFF);
            out[i * 2 + 1] = (byte) (w & 0xFF);
        }
        return out;
    }

    private static List<InetAddress> defaultAddressesForNullNode(int family, boolean passive) throws UnknownHostException {
        List<InetAddress> out = new ArrayList<>();

        if (family == AF_UNSPEC || family == AF_INET6) {
            byte[] v6 = new byte[16];
            if (!passive) v6[15] = 1; // ::1
            out.add(InetAddress.getByAddress(v6));
        }

        if (family == AF_UNSPEC || family == AF_INET) {
            byte[] v4 = passive ? new byte[] {0, 0, 0, 0} : new byte[] {127, 0, 0, 1};
            out.add(InetAddress.getByAddress(v4));
        }

        return out;
    }

    private static List<InetAddress> resolveNumericNode(String node, int family, int flags, byte[] v4, byte[] v6)
            throws UnknownHostException {
        List<InetAddress> out = new ArrayList<>();

        if (v4 != null) {
            if (family == AF_UNSPEC || family == AF_INET) {
                out.add(InetAddress.getByAddress(v4));
            } else if (family == AF_INET6 && (flags & AI_V4MAPPED) != 0) {
                out.add(InetAddress.getByAddress(ipv4ToMappedIpv6(v4)));
            }
            return out;
        }

        if (v6 != null) {
            if (family == AF_UNSPEC || family == AF_INET6) {
                out.add(InetAddress.getByAddress(v6));
            }
        }

        return out;
    }

    private static List<InetAddress> resolveDnsNode(String node, int family, int flags) throws UnknownHostException {
        InetAddress[] resolved = InetAddress.getAllByName(node);

        List<InetAddress> v4 = new ArrayList<>();
        List<InetAddress> v6 = new ArrayList<>();
        for (InetAddress addr : resolved) {
            if (addr.getAddress().length == 16) v6.add(addr);
            else v4.add(addr);
        }

        List<InetAddress> out = new ArrayList<>();

        if (family == AF_UNSPEC) {
            out.addAll(Arrays.asList(resolved));
            return out;
        }

        if (family == AF_INET) {
            out.addAll(v4);
            return out;
        }

        // AF_INET6
        out.addAll(v6);
        if ((flags & AI_V4MAPPED) != 0 && (((flags & AI_ALL) != 0) || v6.isEmpty())) {
            for (InetAddress a : v4) {
                out.add(InetAddress.getByAddress(ipv4ToMappedIpv6(a.getAddress())));
            }
        }
        return out;
    }

    private static List<SockProtoPair> buildSockProtoPairs(int socktype, int protocol) {
        List<SockProtoPair> out = new ArrayList<>();

        if (socktype == 0) {
            if (protocol == 0 || protocol == IPPROTO_TCP) {
                out.add(new SockProtoPair(SOCK_STREAM, IPPROTO_TCP));
            }
            if (protocol == 0 || protocol == IPPROTO_UDP) {
                out.add(new SockProtoPair(SOCK_DGRAM, IPPROTO_UDP));
            }
            return out;
        }

        if (socktype == SOCK_STREAM) {
            out.add(new SockProtoPair(SOCK_STREAM, IPPROTO_TCP));
            return out;
        }

        if (socktype == SOCK_DGRAM) {
            out.add(new SockProtoPair(SOCK_DGRAM, IPPROTO_UDP));
            return out;
        }

        return out;
    }
}
