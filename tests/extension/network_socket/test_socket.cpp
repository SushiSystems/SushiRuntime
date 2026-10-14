/**************************************************************************/
/* test_socket.cpp                                                        */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                         https://sushisystems.io                        */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the PolyForm Noncommercial License 1.0.0 (the           */
/* "License"); you may not use this file except in compliance with the    */
/* License. You may obtain a copy of the License at                       */
/*                                                                        */
/*     https://polyformproject.org/licenses/noncommercial/1.0.0           */
/*                                                                        */
/* Noncommercial use is free. Commercial use requires a separate licence  */
/* from Sushi Systems; see COMMERCIAL.md. The software is provided        */
/* "as is", without warranty of any kind.                                 */
/**************************************************************************/

/**
 * @file test_socket.cpp
 * @brief Tests Socket and Listener: exact reads, full writes, timeouts and peer loss.
 * @see tests/extension/network_socket/README.md
 * @author Mustafa Garip
 */

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/transport/endpoint.hpp>
#include <SushiRuntime/network_socket/socket.hpp>



using namespace SushiRuntime;
using namespace SushiRuntime::Distributed;

namespace
{
    /** @brief Longest a test waits for an event that loopback delivers at once. */
    constexpr std::chrono::milliseconds K_EVENT_LIMIT{10000};

    /** @brief Wait given to a call that the test expects to time out. */
    constexpr std::chrono::milliseconds K_SHORT_WAIT{20};

    /** @brief Loopback address the IPv4 tests bind. */
    constexpr const char* K_IPV4_LOOPBACK = "127.0.0.1";

    /** @brief Loopback address the IPv6 test binds. */
    constexpr const char* K_IPV6_LOOPBACK = "::1";

    /**
     * @brief Connects a client to a listener and accepts the other end.
     * @return The first failure, or an Ok result.
     */
    SocketResult connect_pair(Listener& listener, const Endpoint& remote, Socket& client,
                              Socket& server)
    {
        const SocketResult connected = client.connect(remote, K_EVENT_LIMIT);
        if (!connected.is_ok())
            return connected;
        return listener.accept(server, K_EVENT_LIMIT);
    }

    /**
     * @brief Fills a buffer with a fixed pattern that no shifted copy of itself matches.
     */
    std::vector<std::byte> make_pattern(std::size_t size)
    {
        std::vector<std::byte> bytes(size);
        for (std::size_t index = 0; index < size; ++index)
            bytes[index] = static_cast<std::byte>((index * 31 + index / 251 + 7) & 0xFF);
        return bytes;
    }
} // namespace

/**
 * @brief Sends a buffer larger than the OS socket buffers one way and a short reply back.
 */
TEST(Unit_SocketTest, ExactReadAndFullWriteRoundTrip)
{
    Listener listener;
    const SocketResult listening = listener.listen(Endpoint{ K_IPV4_LOOPBACK, 0 }, 4);
    ASSERT_TRUE(listening.is_ok()) << describe(listening);
    ASSERT_NE(listener.local_endpoint().port, 0);

    Socket client;
    Socket server;
    const SocketResult paired = connect_pair(listener, listener.local_endpoint(), client, server);
    ASSERT_TRUE(paired.is_ok()) << describe(paired);

    const std::vector<std::byte> sent = make_pattern((std::size_t{1} << 22) + 3);
    SocketResult                 written;
    std::thread writer([&client, &sent, &written] {
        written = client.write_all(sent, K_EVENT_LIMIT);
    });

    std::vector<std::byte> received(sent.size());
    const SocketResult     read = server.read_exact(received, K_EVENT_LIMIT);
    writer.join();

    ASSERT_TRUE(written.is_ok()) << describe(written);
    ASSERT_TRUE(read.is_ok()) << describe(read);
    EXPECT_EQ(written.transferred, sent.size());
    EXPECT_EQ(read.transferred, sent.size());
    EXPECT_TRUE(received == sent);

    const std::vector<std::byte> reply = make_pattern(5);
    std::vector<std::byte>       echoed(reply.size());
    const SocketResult replied = server.write_all(reply, K_EVENT_LIMIT);
    const SocketResult heard   = client.read_exact(echoed, K_EVENT_LIMIT);
    ASSERT_TRUE(replied.is_ok()) << describe(replied);
    ASSERT_TRUE(heard.is_ok()) << describe(heard);
    EXPECT_TRUE(echoed == reply);

    Endpoint peer;
    const SocketResult named = server.peer_endpoint(peer);
    ASSERT_TRUE(named.is_ok()) << describe(named);
    EXPECT_EQ(peer.host, K_IPV4_LOOPBACK);
    EXPECT_NE(peer.port, 0);
}

/**
 * @brief Checks that an idle read and an accept with no client return Timeout.
 */
TEST(Unit_SocketTest, IdleReadAndIdleAcceptTimeOut)
{
    Listener listener;
    const SocketResult listening = listener.listen(Endpoint{ K_IPV4_LOOPBACK, 0 }, 4);
    ASSERT_TRUE(listening.is_ok()) << describe(listening);

    Socket nobody;
    const SocketResult waited = listener.accept(nobody, K_SHORT_WAIT);
    EXPECT_EQ(waited.status, SocketStatus::Timeout) << describe(waited);
    EXPECT_FALSE(nobody.is_open());

    Socket client;
    Socket server;
    const SocketResult paired = connect_pair(listener, listener.local_endpoint(), client, server);
    ASSERT_TRUE(paired.is_ok()) << describe(paired);

    std::byte          one[1];
    const SocketResult read = server.read_exact(one, K_SHORT_WAIT);
    EXPECT_EQ(read.status, SocketStatus::Timeout) << describe(read);
    EXPECT_EQ(read.transferred, 0u);
}

/**
 * @brief Checks that shutdown() from another thread ends a read that waits for bytes.
 */
TEST(Unit_SocketTest, ShutdownEndsAWaitingRead)
{
    Listener listener;
    const SocketResult listening = listener.listen(Endpoint{ K_IPV4_LOOPBACK, 0 }, 4);
    ASSERT_TRUE(listening.is_ok()) << describe(listening);

    Socket client;
    Socket server;
    const SocketResult paired = connect_pair(listener, listener.local_endpoint(), client, server);
    ASSERT_TRUE(paired.is_ok()) << describe(paired);

    SocketResult read;
    std::thread  reader([&server, &read] {
        std::byte one[1];
        read = server.read_exact(one, K_EVENT_LIMIT);
    });

    server.shutdown();
    reader.join();

    EXPECT_EQ(read.status, SocketStatus::Closed) << describe(read);
}

/**
 * @brief Writes to a peer that has closed and expects a status, with the process alive.
 */
TEST(Unit_SocketTest, WriteToClosedPeerReturnsError)
{
    Listener listener;
    const SocketResult listening = listener.listen(Endpoint{ K_IPV4_LOOPBACK, 0 }, 4);
    ASSERT_TRUE(listening.is_ok()) << describe(listening);

    Socket client;
    Socket server;
    const SocketResult paired = connect_pair(listener, listener.local_endpoint(), client, server);
    ASSERT_TRUE(paired.is_ok()) << describe(paired);

    server.close();

    std::byte          one[1];
    const SocketResult ended = client.read_exact(one, K_EVENT_LIMIT);
    ASSERT_EQ(ended.status, SocketStatus::Closed) << describe(ended);

    // The peer answers the first write with a reset; a later write meets it.
    constexpr int                K_MAXIMUM_WRITES = 4096;
    const std::vector<std::byte> chunk = make_pattern(std::size_t{1} << 16);
    SocketResult                 written;
    for (int attempt = 0; attempt < K_MAXIMUM_WRITES && written.is_ok(); ++attempt)
        written = client.write_all(chunk, K_EVENT_LIMIT);

    EXPECT_EQ(written.status, SocketStatus::Closed) << describe(written);
    EXPECT_NE(written.os_error, 0);
    EXPECT_FALSE(written.cause.empty());

    const SocketResult again = client.write_all(chunk, K_EVENT_LIMIT);
    EXPECT_EQ(again.status, SocketStatus::Closed) << describe(again);
    EXPECT_EQ(again.os_error, 0);
    EXPECT_EQ(again.transferred, 0u);
}

/**
 * @brief Binds a port twice and expects the refusal to name the port and the OS cause.
 */
TEST(Unit_SocketTest, BindInUseNamesPortAndCause)
{
    Listener first;
    const SocketResult listening = first.listen(Endpoint{ K_IPV4_LOOPBACK, 0 }, 4);
    ASSERT_TRUE(listening.is_ok()) << describe(listening);

    Listener second;
    const SocketResult refused = second.listen(first.local_endpoint(), 4);

    ASSERT_EQ(refused.status, SocketStatus::Failed) << describe(refused);
    EXPECT_FALSE(second.is_open());
    EXPECT_STREQ(refused.operation, "bind");
    EXPECT_NE(refused.os_error, 0);
    EXPECT_FALSE(refused.cause.empty());
    EXPECT_EQ(refused.address, first.local_endpoint().to_string());

    const std::string line = describe(refused);
    const std::string port = ":" + std::to_string(first.local_endpoint().port);
    EXPECT_NE(line.find(port), std::string::npos) << line;
    EXPECT_NE(line.find(refused.cause), std::string::npos) << line;
}

/**
 * @brief Resolves `localhost` and connects by that name to a listener on 127.0.0.1.
 */
TEST(Unit_SocketTest, LocalhostResolves)
{
    std::vector<std::string> addresses;
    const SocketResult       resolved = resolve_addresses(Endpoint{ "localhost", 0 }, addresses);
    ASSERT_TRUE(resolved.is_ok()) << describe(resolved);

    bool has_ipv4_loopback = false;
    for (const std::string& address : addresses)
        has_ipv4_loopback = has_ipv4_loopback || address == K_IPV4_LOOPBACK;
    ASSERT_TRUE(has_ipv4_loopback) << "localhost has " << addresses.size() << " addresses";

    Listener listener;
    const SocketResult listening = listener.listen(Endpoint{ K_IPV4_LOOPBACK, 0 }, 4);
    ASSERT_TRUE(listening.is_ok()) << describe(listening);

    Socket client;
    Socket server;
    const Endpoint     by_name{ "localhost", listener.local_endpoint().port };
    const SocketResult paired = connect_pair(listener, by_name, client, server);
    ASSERT_TRUE(paired.is_ok()) << describe(paired);

    Endpoint peer;
    const SocketResult named = client.peer_endpoint(peer);
    ASSERT_TRUE(named.is_ok()) << describe(named);
    EXPECT_EQ(peer.host, K_IPV4_LOOPBACK);
    EXPECT_EQ(peer.port, listener.local_endpoint().port);
}

/**
 * @brief Connects over the IPv6 loopback; skipped on a machine that has none.
 */
TEST(Unit_SocketTest, Ipv6LoopbackConnects)
{
    Listener listener;
    const SocketResult listening = listener.listen(Endpoint{ K_IPV6_LOOPBACK, 0 }, 4);
    if (!listening.is_ok())
        GTEST_SKIP() << "This machine has no IPv6 loopback: " << describe(listening);

    EXPECT_EQ(listener.local_endpoint().host, K_IPV6_LOOPBACK);

    Socket client;
    Socket server;
    const SocketResult paired = connect_pair(listener, listener.local_endpoint(), client, server);
    ASSERT_TRUE(paired.is_ok()) << describe(paired);

    Endpoint peer;
    const SocketResult named = client.peer_endpoint(peer);
    ASSERT_TRUE(named.is_ok()) << describe(named);
    EXPECT_EQ(peer.host, K_IPV6_LOOPBACK);
    EXPECT_EQ(peer.port, listener.local_endpoint().port);

    const std::vector<std::byte> sent = make_pattern(64);
    std::vector<std::byte>       received(sent.size());
    const SocketResult written = client.write_all(sent, K_EVENT_LIMIT);
    const SocketResult read    = server.read_exact(received, K_EVENT_LIMIT);
    ASSERT_TRUE(written.is_ok()) << describe(written);
    ASSERT_TRUE(read.is_ok()) << describe(read);
    EXPECT_TRUE(received == sent);
}
