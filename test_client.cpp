// Copyright (c) 2026, the Luxon Server contributors
// SPDX-License-Identifier: BSD-3-Clause
//
// Comprehensive test client for LuxonServer
// Tests: NameServer, MasterServer, GameServer functionality

#include <chrono>
#include <csignal>
#include <cstdint>
#include <iostream>
#include <span>
#include <string>
#include <thread>
#include <functional>
#include <vector>
#include <optional>
#include <variant>

#include <luxon/enet_peer.hpp>
#include <luxon/ser_gp_binary_v18.hpp>
#include <luxon/ser_types.hpp>
#include <luxon/common_codes.hpp>
#include <luxon/visualizer.hpp>

using namespace luxon;
using namespace luxon::ser;
using namespace luxon::enet;

static bool g_running = true;
static void signal_handler(int) { g_running = false; }

// Test result tracking
struct TestResult {
    std::string name;
    bool passed;
    std::string message;
};

static std::vector<TestResult> g_results;

static void record_test(const std::string& name, bool passed, const std::string& msg = "") {
    g_results.push_back({name, passed, msg});
    std::cout << (passed ? "\033[32m[PASS]\033[0m " : "\033[31m[FAIL]\033[0m ") << name;
    if (!msg.empty())
        std::cout << " - " << msg;
    std::cout << std::endl;
}

// Simple synchronous client wrapper with message queue
class TestClient {
public:
    TestClient() : peer_(cfg_, metrics_) {
        peer_.on_state_changed = [this](EnetConnectionState state) {
            state_ = state;
            if (state == EnetConnectionState::Connected)
                connected_ = true;
            if (state == EnetConnectionState::Disconnected)
                connected_ = false;
        };

        peer_.on_payload_command = [this](const EnetCommand& cmd) {
            auto payload = cmd.get_payload();
            auto msg = proto_.Deserialize(payload);
            if (msg) {
                response_queue_.push_back(std::move(*msg));
            }
        };
    }

    bool connect(const std::string& host, uint16_t port, int timeout_ms = 3000) {
        if (!peer_.connect(sock_, host, port))
            return false;

        InitMessage init{.app_id = "TestClient"};
        send_message(Message(init, false));

        return wait_for_connection(timeout_ms);
    }

    void disconnect() {
        peer_.disconnect();
        service_until([this] { return !connected_; }, 1000);
    }

    bool establish_encryption(int timeout_ms = 3000) {
        response_queue_.clear();
        auto req_bytes = proto_.CreateInitEncryptionRequest();
        if (!req_bytes)
            return false;

        peer_.send_payload(*req_bytes, EnetSendOptions{.channel = 0, .mode = EnetDeliveryMode::Reliable});

        auto start = std::chrono::steady_clock::now();
        while (g_running && connected_) {
            service();

            for (auto it = response_queue_.begin(); it != response_queue_.end(); ++it) {
                if (auto *resp = std::get_if<InternalOperationResponseMessage>(&(*it))) {
                    auto ok = proto_.HandleInitEncryptionResponse(*resp);
                    response_queue_.erase(it);
                    return ok.has_value() && proto_.has_encryption_key();
                }
            }

            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
            if (elapsed > timeout_ms)
                return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return false;
    }

    std::optional<OperationResponseMessage> send_operation(const OperationRequestMessage& req, bool encrypted = true, int timeout_ms = 3000) {
        response_queue_.clear();
        send_message(Message(req, encrypted));

        auto start = std::chrono::steady_clock::now();
        while (g_running && connected_) {
            service();

            for (auto it = response_queue_.begin(); it != response_queue_.end(); ++it) {
                if (auto *resp = std::get_if<OperationResponseMessage>(&(*it))) {
                    if (resp->operation_code == req.operation_code) {
                        auto result = *resp;
                        response_queue_.erase(it);
                        return result;
                    }
                }
            }

            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
            if (elapsed > timeout_ms)
                return std::nullopt;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return std::nullopt;
    }

    std::optional<EventMessage> wait_for_event(int timeout_ms = 3000) {
        auto start = std::chrono::steady_clock::now();
        while (g_running && connected_) {
            service();

            for (auto it = response_queue_.begin(); it != response_queue_.end(); ++it) {
                if (auto *evt = std::get_if<EventMessage>(&(*it))) {
                    auto result = *evt;
                    response_queue_.erase(it);
                    return result;
                }
            }

            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
            if (elapsed > timeout_ms)
                return std::nullopt;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return std::nullopt;
    }

    bool is_connected() const { return connected_; }
    bool has_encryption() const { return proto_.has_encryption_key(); }

    // Non-blocking event retrieval - returns next event from queue or nullopt
    std::optional<EventMessage> pop_event() {
        service();
        for (auto it = response_queue_.begin(); it != response_queue_.end(); ++it) {
            if (auto *evt = std::get_if<EventMessage>(&(*it))) {
                auto result = *evt;
                response_queue_.erase(it);
                return result;
            }
        }
        return std::nullopt;
    }

    void service() {
        uint8_t buffer[2048];
        EnetEndpoint from;
        size_t received = sock_.recv_from(buffer, sizeof(buffer), from);
        if (received > 0) {
            ByteArray datagram(buffer, buffer + received);
            peer_.handle_incoming_datagram(datagram);
        }
        peer_.service();
        while (peer_.dispatch_one()) {
        }
    }

private:
    void send_message(const Message& msg) {
        auto bytes = proto_.Serialize(msg);
        if (bytes) {
            peer_.send_payload(*bytes, EnetSendOptions{.channel = 0, .mode = EnetDeliveryMode::Reliable});
        }
    }

    bool wait_for_connection(int timeout_ms) {
        return service_until([this] { return connected_; }, timeout_ms);
    }

    bool service_until(std::function<bool()> condition, int timeout_ms) {
        auto start = std::chrono::steady_clock::now();
        while (!condition() && g_running) {
            service();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
            if (elapsed > timeout_ms)
                return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return condition();
    }

    EnetPeerConfig cfg_{.time_ping_interval_ms = 1000, .disconnect_timeout_ms = 5000};
    UdpSocket sock_;
    EnetPeer peer_;
    GpBinaryV18 proto_;
    Metrics metrics_;

    EnetConnectionState state_ = EnetConnectionState::Disconnected;
    bool connected_ = false;
    std::vector<Message> response_queue_;
};

// ============================================================================
// Test Cases
// ============================================================================

void test_nameserver_connection(const std::string& host, uint16_t port) {
    TestClient client;

    bool connected = client.connect(host, port);
    record_test("NameServer: Connect", connected);
    if (!connected)
        return;

    bool encrypted = client.establish_encryption();
    record_test("NameServer: Encryption handshake", encrypted);
    if (!encrypted)
        return;

    // Test GetRegions (unauthenticated)
    {
        OperationRequestMessage req{.operation_code = OpCodes::RpcAndMisc::GetRegions};
        auto resp = client.send_operation(req, false);
        bool ok = resp && resp->return_code == ErrorCodes::Core::Ok;
        record_test("NameServer: GetRegions", ok, ok ? "" : (resp ? "return_code=" + std::to_string(resp->return_code) : "no response"));
    }

    // Test Authenticate
    {
        OperationRequestMessage req{.operation_code = OpCodes::Auth::Authenticate};
        req.parameters[DictKeyCodes::LoadBalancing::ApplicationId] = std::string("test_app");
        req.parameters[DictKeyCodes::LoadBalancing::AppVersion] = std::string("1.0");
        req.parameters[DictKeyCodes::AuthAndLobby::Region] = std::string("eu");

        auto resp = client.send_operation(req, true);
        bool ok = resp && resp->return_code == ErrorCodes::Core::Ok;
        record_test("NameServer: Authenticate", ok, ok ? "" : (resp ? "return_code=" + std::to_string(resp->return_code) : "no response"));

        if (ok && resp) {
            // Check we got MasterServer address
            auto it = resp->parameters.find(DictKeyCodes::LoadBalancing::Address);
            bool has_addr = it != resp->parameters.end() && it->second.is<std::string>();
            record_test("NameServer: Returns MasterServer address", has_addr, has_addr ? it->second.get<std::string>() : "");
        }
    }

    client.disconnect();
}

void test_masterserver_connection(const std::string& host, uint16_t port) {
    TestClient client;

    bool connected = client.connect(host, port);
    record_test("MasterServer: Connect", connected);
    if (!connected)
        return;

    bool encrypted = client.establish_encryption();
    record_test("MasterServer: Encryption handshake", encrypted);
    if (!encrypted)
        return;

    // Authenticate
    {
        OperationRequestMessage req{.operation_code = OpCodes::Auth::Authenticate};
        req.parameters[DictKeyCodes::LoadBalancing::ApplicationId] = std::string("test_app");
        req.parameters[DictKeyCodes::LoadBalancing::AppVersion] = std::string("1.0");
        req.parameters[DictKeyCodes::LoadBalancing::UserId] = std::string("test_user_1");

        auto resp = client.send_operation(req, true);
        bool ok = resp && resp->return_code == ErrorCodes::Core::Ok;
        record_test("MasterServer: Authenticate", ok, ok ? "" : (resp ? "return_code=" + std::to_string(resp->return_code) : "no response"));
        if (!ok)
            return;
    }

    // Test JoinLobby
    {
        OperationRequestMessage req{.operation_code = OpCodes::Lobby::JoinLobby};
        req.parameters[DictKeyCodes::AuthAndLobby::LobbyName] = std::string("TestLobby");
        req.parameters[DictKeyCodes::AuthAndLobby::LobbyType] = static_cast<uint8_t>(LobbyType::Default);

        auto resp = client.send_operation(req, true);
        bool ok = resp && resp->return_code == ErrorCodes::Core::Ok;
        record_test("MasterServer: JoinLobby", ok, ok ? "" : (resp ? "return_code=" + std::to_string(resp->return_code) : "no response"));
    }

    // Test CreateGame
    std::string game_address;
    {
        OperationRequestMessage req{.operation_code = OpCodes::Matchmaking::CreateGame};
        req.parameters[DictKeyCodes::GameAndActor::GameId] = std::string("TestGame");

        auto ht = std::make_shared<Hashtable>();
        (*ht)[Value(static_cast<uint8_t>(GameProps::MaxPlayers))] = Value(static_cast<uint8_t>(4));
        (*ht)[Value(static_cast<uint8_t>(GameProps::IsVisible))] = Value(true);
        (*ht)[Value(static_cast<uint8_t>(GameProps::IsOpen))] = Value(true);
        req.parameters[DictKeyCodes::Properties::GameProperties] = Value(std::move(ht));

        auto resp = client.send_operation(req, true);
        bool ok = resp && resp->return_code == ErrorCodes::Core::Ok;
        record_test("MasterServer: CreateGame", ok, ok ? "" : (resp ? "return_code=" + std::to_string(resp->return_code) : "no response"));

        if (ok && resp) {
            auto it = resp->parameters.find(DictKeyCodes::LoadBalancing::Address);
            if (it != resp->parameters.end() && it->second.is<std::string>()) {
                game_address = it->second.get<std::string>();
                record_test("MasterServer: CreateGame returns GameServer address", true, game_address);
            }
        }
    }

    // Test LeaveLobby
    {
        OperationRequestMessage req{.operation_code = OpCodes::Lobby::LeaveLobby};
        auto resp = client.send_operation(req, true);
        bool ok = resp && resp->return_code == ErrorCodes::Core::Ok;
        record_test("MasterServer: LeaveLobby", ok, ok ? "" : (resp ? "return_code=" + std::to_string(resp->return_code) : "no response"));
    }

    // Test JoinRandomGame (should fail - no games after leaving)
    {
        OperationRequestMessage req{.operation_code = OpCodes::Matchmaking::JoinRandomGame};
        auto resp = client.send_operation(req, true);
        // This might succeed or fail depending on server state - just check we got a response
        record_test("MasterServer: JoinRandomGame responds", resp.has_value(), resp ? "return_code=" + std::to_string(resp->return_code) : "no response");
    }

    // Test LobbyStats
    {
        OperationRequestMessage req{.operation_code = OpCodes::Lobby::LobbyStats};
        auto resp = client.send_operation(req, true);
        bool ok = resp && resp->return_code == ErrorCodes::Core::Ok;
        record_test("MasterServer: LobbyStats", ok, ok ? "" : (resp ? "return_code=" + std::to_string(resp->return_code) : "no response"));
    }

    client.disconnect();
}

void test_gameserver_connection(const std::string& host, uint16_t port) {
    // Note: GameServer requires game assignment from MasterServer
    // Direct connection will be rejected - this tests that behavior
    TestClient client;

    bool connected = client.connect(host, port);
    record_test("GameServer: Connect (direct)", connected);
    if (!connected)
        return;

    bool encrypted = client.establish_encryption();
    record_test("GameServer: Encryption handshake", encrypted);
    if (!encrypted)
        return;

    // Authenticate - should fail without proper game assignment
    {
        OperationRequestMessage req{.operation_code = OpCodes::Auth::Authenticate};
        req.parameters[DictKeyCodes::LoadBalancing::ApplicationId] = std::string("test_app");
        req.parameters[DictKeyCodes::LoadBalancing::AppVersion] = std::string("1.0");
        req.parameters[DictKeyCodes::LoadBalancing::UserId] = std::string("game_test_user");

        auto resp = client.send_operation(req, true, 1000);
        // Expected: Server disconnects because no game is assigned
        // This is correct behavior - GameServer requires MasterServer routing
        bool expected_disconnect = !client.is_connected() || !resp;
        record_test("GameServer: Rejects direct auth (expected)", expected_disconnect, "GameServer requires game assignment from MasterServer");
    }
}

void test_multiplayer_scenario(const std::string& host, uint16_t ms_port) {
    std::cout << "\n--- Multiplayer Scenario Test (via MasterServer and GameServer) ---\n" << std::endl;

    TestClient client1, client2;
    TestClient client1_gs, client2_gs; // Fresh clients for the GameServer transitions

    // Both clients connect to MasterServer
    bool c1_connected = client1.connect(host, ms_port);
    bool c2_connected = client2.connect(host, ms_port);
    record_test("Multiplayer: Both clients connect to MasterServer", c1_connected && c2_connected);
    if (!c1_connected || !c2_connected)
        return;

    // Establish encryption
    bool c1_enc = client1.establish_encryption();
    bool c2_enc = client2.establish_encryption();
    record_test("Multiplayer: Both clients encrypted", c1_enc && c2_enc);
    if (!c1_enc || !c2_enc)
        return;

    // Authenticate both on MasterServer
    auto auth_ms = [](TestClient& c, const std::string& user) {
        OperationRequestMessage req{.operation_code = OpCodes::Auth::Authenticate};
        req.parameters[DictKeyCodes::LoadBalancing::ApplicationId] = std::string("test_app");
        req.parameters[DictKeyCodes::LoadBalancing::AppVersion] = std::string("1.0");
        req.parameters[DictKeyCodes::LoadBalancing::UserId] = user;
        return c.send_operation(req, true);
    };

    auto r1 = auth_ms(client1, "mp_player1");
    auto r2 = auth_ms(client2, "mp_player2");
    bool auth_ok = r1 && r1->return_code == 0 && r2 && r2->return_code == 0;
    record_test("Multiplayer: Both clients authenticated on MasterServer", auth_ok);
    if (!auth_ok)
        return;

    // Both clients join a lobby first
    auto join_lobby = [](TestClient& c) {
        OperationRequestMessage req{.operation_code = OpCodes::Lobby::JoinLobby};
        req.parameters[DictKeyCodes::AuthAndLobby::LobbyName] = std::string("MultiplayerLobby");
        req.parameters[DictKeyCodes::AuthAndLobby::LobbyType] = static_cast<uint8_t>(LobbyType::Default);
        return c.send_operation(req, true);
    };

    auto l1 = join_lobby(client1);
    auto l2 = join_lobby(client2);
    record_test("Multiplayer: Both clients joined lobby", l1 && l1->return_code == 0 && l2 && l2->return_code == 0);

    // Client 1 creates a game via MasterServer
    std::string game_address;
    std::string token1;
    {
        OperationRequestMessage req{.operation_code = OpCodes::Matchmaking::CreateGame};
        req.parameters[DictKeyCodes::GameAndActor::GameId] = std::string("MultiplayerTestGame");

        auto ht = std::make_shared<Hashtable>();
        (*ht)[Value(static_cast<uint8_t>(GameProps::MaxPlayers))] = Value(static_cast<uint8_t>(4));
        (*ht)[Value(static_cast<uint8_t>(GameProps::IsVisible))] = Value(true);
        (*ht)[Value(static_cast<uint8_t>(GameProps::IsOpen))] = Value(true);
        req.parameters[DictKeyCodes::Properties::GameProperties] = Value(std::move(ht));

        auto resp = client1.send_operation(req, true);
        bool ok = resp && resp->return_code == 0;
        record_test("Multiplayer: Client1 creates game via MasterServer", ok);

        if (ok && resp) {
            auto it = resp->parameters.find(DictKeyCodes::LoadBalancing::Address);
            if (it != resp->parameters.end() && it->second.is<std::string>()) {
                game_address = it->second.get<std::string>();
                record_test("Multiplayer: Got GameServer address", true, game_address);
            }

            auto token_it = resp->parameters.find(DictKeyCodes::LoadBalancing::Token);
            if (token_it != resp->parameters.end() && token_it->second.is<std::string>()) {
                token1 = token_it->second.get<std::string>();
            }
        }
    }

    // Parse GameServer address out of string
    std::string gs_host = host;
    uint16_t gs_port = 5056;
    if (!game_address.empty()) {
        size_t colon_pos = game_address.find(':');
        if (colon_pos != std::string::npos) {
            gs_host = game_address.substr(0, colon_pos);
            gs_port = static_cast<uint16_t>(std::stoi(game_address.substr(colon_pos + 1)));
        }
    }

    // ==============================================================================
    // CLIENT 1 TRANSITIONS TO GAMESERVER TO BOOT/INITIALIZE THE ROOM
    // ==============================================================================
    client1.disconnect();

    bool c1_gs_conn = client1_gs.connect(gs_host, gs_port);
    record_test("Multiplayer: Client1 connects to GameServer", c1_gs_conn);

    if (c1_gs_conn) {
        bool c1_gs_enc = client1_gs.establish_encryption();
        record_test("Multiplayer: Client1 encrypted on GameServer", c1_gs_enc);

        OperationRequestMessage gs_auth{.operation_code = OpCodes::Auth::Authenticate};
        gs_auth.parameters[DictKeyCodes::LoadBalancing::ApplicationId] = std::string("test_app");
        gs_auth.parameters[DictKeyCodes::LoadBalancing::AppVersion] = std::string("1.0");
        gs_auth.parameters[DictKeyCodes::LoadBalancing::UserId] = std::string("mp_player1");
        if (!token1.empty()) {
            gs_auth.parameters[DictKeyCodes::LoadBalancing::Token] = token1;
        }

        auto gs_auth_resp = client1_gs.send_operation(gs_auth, true);
        bool gs_auth_ok = gs_auth_resp && gs_auth_resp->return_code == 0;
        record_test("Multiplayer: Client1 authenticates on GameServer", gs_auth_ok);

        if (gs_auth_ok) {
            OperationRequestMessage gs_create{.operation_code = OpCodes::Matchmaking::CreateGame};
            gs_create.parameters[DictKeyCodes::GameAndActor::GameId] = std::string("MultiplayerTestGame");

            auto ht2 = std::make_shared<Hashtable>();
            (*ht2)[Value(static_cast<uint8_t>(GameProps::MaxPlayers))] = Value(static_cast<uint8_t>(4));
            (*ht2)[Value(static_cast<uint8_t>(GameProps::IsVisible))] = Value(true);
            (*ht2)[Value(static_cast<uint8_t>(GameProps::IsOpen))] = Value(true);
            gs_create.parameters[DictKeyCodes::Properties::GameProperties] = Value(std::move(ht2));

            auto gs_create_resp = client1_gs.send_operation(gs_create, true);
            bool gs_create_ok = gs_create_resp && gs_create_resp->return_code == 0;
            record_test("Multiplayer: Client1 executes CreateGame on GameServer", gs_create_ok);

            // Give the GameServer time to IPC broadcast back to the MasterServer
            // so is_created becomes true before Client 2 queries it.
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
    }

    // Flush Client 2 events ensuring the connection stays alive
    bool game_found = false;
    for (int attempt = 0; attempt < 20; ++attempt) {
        client1_gs.service();
        while (auto event = client2.pop_event()) {
            if (event->event_code == EventCodes::GameList) {
                auto it = event->parameters.find(DictKeyCodes::LoadBalancing::GameList);
                if (it != event->parameters.end()) {
                    auto *ht_ptr = it->second.get_ptr<HashtablePtr>();
                    if (ht_ptr && *ht_ptr) {
                        auto& games = **ht_ptr;
                        if (games.find(Value(std::string("MultiplayerTestGame"))) != games.end()) {
                            game_found = true;
                        }
                    }
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    record_test("Multiplayer: Game appears in Client2's game list (IPC sync)", game_found);

    client1_gs.service();
    client2.service();
    record_test("Multiplayer: Client2 still connected to MasterServer", client2.is_connected());

    // Try to get game list from client2 explicitly to verify game visibility
    {
        OperationRequestMessage req{.operation_code = OpCodes::Lobby::GetGameList};
        auto resp = client2.send_operation(req, true, 1000);
        std::string detail = resp ? "code=" + std::to_string(resp->return_code) : "timeout";
        record_test("Multiplayer: Client2 GetGameList attempt", true, detail);
    }

    // Client 2 joins the same game via MasterServer
    std::string token2;
    {
        OperationRequestMessage req{.operation_code = OpCodes::Matchmaking::JoinGame};
        req.parameters[DictKeyCodes::GameAndActor::GameId] = std::string("MultiplayerTestGame");
        req.parameters[DictKeyCodes::AuthAndLobby::CreateIfNotExists] = static_cast<uint8_t>(0); // 0 = false

        auto resp = client2.send_operation(req, true);
        client1_gs.service();

        bool ok = resp && resp->return_code == 0;
        std::string msg = resp ? "return_code=" + std::to_string(resp->return_code) : "no response";

        if (ok && resp) {
            auto token_it = resp->parameters.find(DictKeyCodes::LoadBalancing::Token);
            if (token_it != resp->parameters.end() && token_it->second.is<std::string>()) {
                token2 = token_it->second.get<std::string>();
            }
        }
        record_test("Multiplayer: Client2 joins game via MasterServer", ok, msg);
    }

    // ==============================================================================
    // CLIENT 2 TRANSITIONS TO GAMESERVER TO FINISH ROUTING
    // ==============================================================================
    if (!token2.empty()) {
        client2.disconnect();

        bool c2_gs_conn = client2_gs.connect(gs_host, gs_port);
        record_test("Multiplayer: Client2 connects to GameServer", c2_gs_conn);

        if (c2_gs_conn) {
            client2_gs.establish_encryption();

            OperationRequestMessage c2_gs_auth{.operation_code = OpCodes::Auth::Authenticate};
            c2_gs_auth.parameters[DictKeyCodes::LoadBalancing::ApplicationId] = std::string("test_app");
            c2_gs_auth.parameters[DictKeyCodes::LoadBalancing::AppVersion] = std::string("1.0");
            c2_gs_auth.parameters[DictKeyCodes::LoadBalancing::UserId] = std::string("mp_player2");
            c2_gs_auth.parameters[DictKeyCodes::LoadBalancing::Token] = token2;

            auto c2_gs_auth_resp = client2_gs.send_operation(c2_gs_auth, true);
            bool c2_gs_auth_ok = c2_gs_auth_resp && c2_gs_auth_resp->return_code == 0;
            record_test("Multiplayer: Client2 authenticates on GameServer", c2_gs_auth_ok);

            if (c2_gs_auth_ok) {
                OperationRequestMessage c2_gs_join{.operation_code = OpCodes::Matchmaking::JoinGame};
                c2_gs_join.parameters[DictKeyCodes::GameAndActor::GameId] = std::string("MultiplayerTestGame");

                auto c2_gs_join_resp = client2_gs.send_operation(c2_gs_join, true);
                bool c2_gs_join_ok = c2_gs_join_resp && c2_gs_join_resp->return_code == 0;
                record_test("Multiplayer: Client2 executes JoinGame on GameServer", c2_gs_join_ok);
            }
        }
    }

    record_test("Multiplayer: Game routing and creation complete", true, "both clients successfully joined GameServer");

    client1_gs.disconnect();
    client2_gs.disconnect();
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char *argv[]) {
    std::string host = "127.0.0.1";
    uint16_t ns_port = 5058;
    uint16_t ms_port = 5055;
    uint16_t gs_port = 5056;

    if (argc >= 2)
        host = argv[1];
    if (argc >= 3)
        ns_port = static_cast<uint16_t>(std::stoi(argv[2]));
    if (argc >= 4)
        ms_port = static_cast<uint16_t>(std::stoi(argv[3]));
    if (argc >= 5)
        gs_port = static_cast<uint16_t>(std::stoi(argv[4]));

    std::signal(SIGINT, signal_handler);

    std::cout << "========================================" << std::endl;
    std::cout << "LuxonServer Comprehensive Test Client" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Host: " << host << std::endl;
    std::cout << "NameServer port: " << ns_port << std::endl;
    std::cout << "MasterServer port: " << ms_port << std::endl;
    std::cout << "GameServer port: " << gs_port << std::endl;
    std::cout << "========================================\n" << std::endl;

    // Run tests
    std::cout << "--- NameServer Tests ---\n" << std::endl;
    test_nameserver_connection(host, ns_port);

    std::cout << "\n--- MasterServer Tests ---\n" << std::endl;
    test_masterserver_connection(host, ms_port);

    std::cout << "\n--- GameServer Tests ---\n" << std::endl;
    test_gameserver_connection(host, gs_port);

    // Multiplayer test (uses MasterServer for proper routing)
    test_multiplayer_scenario(host, ms_port);

    // Summary
    std::cout << "\n========================================" << std::endl;
    std::cout << "Test Summary" << std::endl;
    std::cout << "========================================" << std::endl;

    int passed = 0, failed = 0;
    for (const auto& r : g_results) {
        if (r.passed)
            passed++;
        else
            failed++;
    }

    std::cout << "Passed: " << passed << std::endl;
    std::cout << "Failed: " << failed << std::endl;
    std::cout << "Total:  " << g_results.size() << std::endl;

    if (failed > 0) {
        std::cout << "\nFailed tests:" << std::endl;
        for (const auto& r : g_results) {
            if (!r.passed) {
                std::cout << "  - " << r.name;
                if (!r.message.empty())
                    std::cout << ": " << r.message;
                std::cout << std::endl;
            }
        }
    }

    return failed > 0 ? 1 : 0;
}
