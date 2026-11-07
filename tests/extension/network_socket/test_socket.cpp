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
/* Licensed under the Apache License, Version 2.0 (the "License"); you    */
/* may not use this file except in compliance with the License. You may   */
/* obtain a copy of the License at                                        */
/*                                                                        */
/*     https://www.apache.org/licenses/LICENSE-2.0                        */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

/**
 * @file test_socket.cpp
 * @brief Tests Socket and Listener: exact reads, full writes, timeouts, shutdown, errors and addressing.
 * @see tests/extension/network_socket/README.md
 * @author Mustafa Garip
 */

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <SushiLog/log/log_capture.hpp>
#include <SushiLog/log/log_level.hpp>

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

    /** @brief Loopback address the IPv6 tests bind. */
    constexpr const char* K_IPV6_LOOPBACK = "::1";

    /** @brief Holds the OS error the refusing option setter reports. */
    constexpr int K_REFUSED_OPTION = 4242;

    /** @brief Refuses every option with K_REFUSED_OPTION, as an OS that cannot set it would. */
    int refuse_option(NativeSocketHandle /*handle*/, int /*level*/, int /*name*/, int /*value*/)
    {
        return K_REFUSED_OPTION;
    }

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

    /**
     * @brief Writes chunks until a write fails, as a peer's reset shows on a later write.
     * @return The first failed write.
     */
    SocketResult write_until_failure(Socket& socket, const std::vector<std::byte>& chunk)
    {
        constexpr int K_MAXIMUM_WRITES = 4096;
        SocketResult written;
        for (int attempt = 0; attempt < K_MAXIMUM_WRITES && written.is_ok(); ++attempt)
            written = socket.write_all(chunk, K_EVENT_LIMIT);
        return written;
    }
} // namespace

/** @brief Opens a listener on the IPv4 loopback with an ephemeral port. */
class Medium_ListeningSocketTest : public ::testing::Test
{
    protected:
        void SetUp() override
        {
            const SocketResult listening = listener_.listen(Endpoint{ K_IPV4_LOOPBACK, 0 }, 4);
            ASSERT_TRUE(listening.is_ok()) << describe(listening);
        }

        /// @brief The listener the test connects to.
        Listener listener_;
};

/** @brief Opens a listener and a connected client and server pair on the IPv4 loopback. */
class Medium_ConnectedSocketTest : public Medium_ListeningSocketTest
{
    protected:
        void SetUp() override
        {
            Medium_ListeningSocketTest::SetUp();
            if (HasFatalFailure())
                return;
            const SocketResult paired =
                connect_pair(listener_, listener_.local_endpoint(), client_, server_);
            ASSERT_TRUE(paired.is_ok()) << describe(paired);
        }

        /// @brief The connecting end.
        Socket client_;

        /// @brief The accepted end.
        Socket server_;
};

/** @brief Opens a connected pair on the IPv6 loopback, or skips on a machine that has none. */
class Medium_IPv6SocketTest : public ::testing::Test
{
    protected:
        void SetUp() override
        {
            const SocketResult listening = listener_.listen(Endpoint{ K_IPV6_LOOPBACK, 0 }, 4);
            if (!listening.is_ok())
                GTEST_SKIP() << "This machine has no IPv6 loopback: " << describe(listening);
            const SocketResult paired =
                connect_pair(listener_, listener_.local_endpoint(), client_, server_);
            ASSERT_TRUE(paired.is_ok()) << describe(paired);
        }

        /// @brief The listener bound to ::1.
        Listener listener_;

        /// @brief The connecting end.
        Socket client_;

        /// @brief The accepted end.
        Socket server_;
};

/** @brief Sends a buffer larger than the OS socket buffers and checks it arrives whole. */
TEST_F(Medium_ConnectedSocketTest, ALargeWriteArrivesWholeThroughAnExactRead)
{
    const std::vector<std::byte> sent = make_pattern((std::size_t{1} << 22) + 3);
    SocketResult written;
    std::thread writer([this, &sent, &written] {
        written = client_.write_all(sent, K_EVENT_LIMIT);
    });
    std::vector<std::byte> received(sent.size());

    const SocketResult read = server_.read_exact(received, K_EVENT_LIMIT);
    writer.join();

    ASSERT_TRUE(written.is_ok()) << describe(written);
    ASSERT_TRUE(read.is_ok()) << describe(read);
    EXPECT_EQ(written.transferred, sent.size());
    EXPECT_EQ(read.transferred, sent.size());
    EXPECT_TRUE(received == sent);
}

/** @brief Sends a short reply back and checks it arrives whole. */
TEST_F(Medium_ConnectedSocketTest, AReplyArrivesWholeThroughAnExactRead)
{
    const std::vector<std::byte> reply = make_pattern(5);
    std::vector<std::byte> echoed(reply.size());

    const SocketResult replied = server_.write_all(reply, K_EVENT_LIMIT);
    const SocketResult heard = client_.read_exact(echoed, K_EVENT_LIMIT);

    ASSERT_TRUE(replied.is_ok()) << describe(replied);
    ASSERT_TRUE(heard.is_ok()) << describe(heard);
    EXPECT_TRUE(echoed == reply);
}

/** @brief Checks that the accepted end names the client by the loopback host and a port. */
TEST_F(Medium_ConnectedSocketTest, PeerEndpointNamesTheClient)
{
    Endpoint peer;

    const SocketResult named = server_.peer_endpoint(peer);

    ASSERT_TRUE(named.is_ok()) << describe(named);
    EXPECT_EQ(peer.host, K_IPV4_LOOPBACK);
    EXPECT_NE(peer.port, 0);
}

/** @brief Checks that a read with nothing to read returns Timeout and no bytes. */
TEST_F(Medium_ConnectedSocketTest, AnIdleReadTimesOut)
{
    std::byte one[1];

    const SocketResult read = server_.read_exact(one, K_SHORT_WAIT);

    EXPECT_EQ(read.status, SocketStatus::Timeout) << describe(read);
    EXPECT_EQ(read.transferred, 0u);
}

/** @brief Checks that shutdown() from another thread ends a read that waits for bytes. */
TEST_F(Medium_ConnectedSocketTest, ShutdownEndsAWaitingRead)
{
    SocketResult read;
    std::thread reader([this, &read] {
        std::byte one[1];
        read = server_.read_exact(one, K_EVENT_LIMIT);
    });

    server_.shutdown();
    reader.join();

    EXPECT_EQ(read.status, SocketStatus::Closed) << describe(read);
}

/** @brief Writes to a peer that has closed and expects a status with its OS cause. */
TEST_F(Medium_ConnectedSocketTest, AWriteToAClosedPeerReturnsClosedWithTheOSCause)
{
    server_.close();
    std::byte one[1];
    ASSERT_EQ(client_.read_exact(one, K_EVENT_LIMIT).status, SocketStatus::Closed);

    // The peer answers the first write with a reset; a later write meets it.
    const SocketResult written = write_until_failure(client_, make_pattern(std::size_t{1} << 16));

    EXPECT_EQ(written.status, SocketStatus::Closed) << describe(written);
    EXPECT_NE(written.os_error, 0);
    EXPECT_FALSE(written.cause.empty());
}

/** @brief Checks that a write after the failed one returns Closed with no OS error. */
TEST_F(Medium_ConnectedSocketTest, AWriteAfterTheFailureReturnsClosedWithNoOSError)
{
    const std::vector<std::byte> chunk = make_pattern(std::size_t{1} << 16);
    server_.close();
    std::byte one[1];
    ASSERT_EQ(client_.read_exact(one, K_EVENT_LIMIT).status, SocketStatus::Closed);
    ASSERT_EQ(write_until_failure(client_, chunk).status, SocketStatus::Closed);

    const SocketResult again = client_.write_all(chunk, K_EVENT_LIMIT);

    EXPECT_EQ(again.status, SocketStatus::Closed) << describe(again);
    EXPECT_EQ(again.os_error, 0);
    EXPECT_EQ(again.transferred, 0u);
}

/** @brief Checks that an accept with no client returns Timeout and opens nothing. */
TEST_F(Medium_ListeningSocketTest, AnIdleAcceptTimesOut)
{
    Socket nobody;

    const SocketResult waited = listener_.accept(nobody, K_SHORT_WAIT);

    EXPECT_EQ(waited.status, SocketStatus::Timeout) << describe(waited);
    EXPECT_FALSE(nobody.is_open());
}

/** @brief Binds a port twice and expects the refusal to name the port and the OS cause. */
TEST_F(Medium_ListeningSocketTest, BindInUseNamesPortAndCause)
{
    Listener second;

    const SocketResult refused = second.listen(listener_.local_endpoint(), 4);

    ASSERT_EQ(refused.status, SocketStatus::Failed) << describe(refused);
    EXPECT_FALSE(second.is_open());
    EXPECT_STREQ(refused.operation, "bind");
    EXPECT_NE(refused.os_error, 0);
    EXPECT_FALSE(refused.cause.empty());
    EXPECT_EQ(refused.address, listener_.local_endpoint().to_string());
    const std::string line = describe(refused);
    const std::string port = ":" + std::to_string(listener_.local_endpoint().port);
    EXPECT_NE(line.find(port), std::string::npos) << line;
    EXPECT_NE(line.find(refused.cause), std::string::npos) << line;
}

/** @brief Checks that connecting by the name `localhost` reaches a listener on 127.0.0.1. */
TEST_F(Medium_ListeningSocketTest, ConnectingByNameReachesTheListener)
{
    Socket client;
    Socket server;
    const Endpoint by_name{ "localhost", listener_.local_endpoint().port };

    const SocketResult paired = connect_pair(listener_, by_name, client, server);

    ASSERT_TRUE(paired.is_ok()) << describe(paired);
    Endpoint peer;
    const SocketResult named = client.peer_endpoint(peer);
    ASSERT_TRUE(named.is_ok()) << describe(named);
    EXPECT_EQ(peer.host, K_IPV4_LOOPBACK);
    EXPECT_EQ(peer.port, listener_.local_endpoint().port);
}

/** @brief Checks that a refused TCP_NODELAY on connect is logged once and the connection stands. */
TEST_F(Medium_ListeningSocketTest, ARefusedNoDelayOnConnectIsLoggedOnceAndTheConnectionStands)
{
    SushiLog::LogCapture capture;
    Socket client;

    const SocketResult connected =
        client.connect(listener_.local_endpoint(), K_EVENT_LIMIT, refuse_option);

    ASSERT_TRUE(connected.is_ok()) << describe(connected);
    EXPECT_TRUE(client.is_open());
    const std::vector<SushiLog::CapturedRecord> records = capture.records();
    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].level, SushiLog::LogLevel::Warn);
    EXPECT_NE(records[0].message.find("TCP_NODELAY could not be set on the connection with " +
                                      listener_.local_endpoint().to_string()),
              std::string::npos)
        << records[0].message;
    EXPECT_NE(records[0].message.find("(os error 4242)"), std::string::npos) << records[0].message;
}

/** @brief Checks that a refused TCP_NODELAY on accept is logged once and the connection stands. */
TEST_F(Medium_ListeningSocketTest, ARefusedNoDelayOnAcceptIsLoggedOnceAndTheConnectionStands)
{
    SushiLog::LogCapture capture;
    Socket client;
    Socket server;
    ASSERT_TRUE(client.connect(listener_.local_endpoint(), K_EVENT_LIMIT).is_ok());

    const SocketResult accepted = listener_.accept(server, K_EVENT_LIMIT, refuse_option);

    ASSERT_TRUE(accepted.is_ok()) << describe(accepted);
    EXPECT_TRUE(server.is_open());
    const std::vector<SushiLog::CapturedRecord> records = capture.records();
    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].level, SushiLog::LogLevel::Warn);
    EXPECT_NE(records[0].message.find("a client of " + listener_.local_endpoint().to_string()),
              std::string::npos)
        << records[0].message;
    EXPECT_NE(records[0].message.find("(os error 4242)"), std::string::npos) << records[0].message;
}

/** @brief Checks that `localhost` resolves to the IPv4 loopback among its addresses. */
TEST(Medium_SocketTest, LocalhostResolvesToTheIPv4Loopback)
{
    std::vector<std::string> addresses;

    const SocketResult resolved = resolve_addresses(Endpoint{ "localhost", 0 }, addresses);

    ASSERT_TRUE(resolved.is_ok()) << describe(resolved);
    EXPECT_NE(std::find(addresses.begin(), addresses.end(), K_IPV4_LOOPBACK), addresses.end())
        << "localhost has " << addresses.size() << " addresses";
}

/** @brief Checks that a listener bound to ::1 reports ::1 as its host. */
TEST_F(Medium_IPv6SocketTest, AListenerReportsItsLoopbackHost)
{
    EXPECT_EQ(listener_.local_endpoint().host, K_IPV6_LOOPBACK);
}

/** @brief Checks that the connecting end names the listener by ::1 and its port. */
TEST_F(Medium_IPv6SocketTest, TheClientNamesItsPeer)
{
    Endpoint peer;

    const SocketResult named = client_.peer_endpoint(peer);

    ASSERT_TRUE(named.is_ok()) << describe(named);
    EXPECT_EQ(peer.host, K_IPV6_LOOPBACK);
    EXPECT_EQ(peer.port, listener_.local_endpoint().port);
}

/** @brief Checks that bytes written on one end of an IPv6 connection arrive on the other. */
TEST_F(Medium_IPv6SocketTest, BytesCrossTheConnection)
{
    const std::vector<std::byte> sent = make_pattern(64);
    std::vector<std::byte> received(sent.size());

    const SocketResult written = client_.write_all(sent, K_EVENT_LIMIT);
    const SocketResult read = server_.read_exact(received, K_EVENT_LIMIT);

    ASSERT_TRUE(written.is_ok()) << describe(written);
    ASSERT_TRUE(read.is_ok()) << describe(read);
    EXPECT_TRUE(received == sent);
}
