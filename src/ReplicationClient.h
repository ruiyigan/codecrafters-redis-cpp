#ifndef REPLICATIONCLIENT_H
#define REPLICATIONCLIENT_H

#include <asio.hpp>
#include <iostream>
#include <memory>
#include <functional>
#include <string>
#include <sstream>

using asio::ip::tcp;

class ReplicationClient {
public:
    /// Construct a replication client with the I/O context and master details.
    ReplicationClient(asio::io_context& io_context, const std::string& masterDetails);

    /// Start the connection and the handshake process with the master.
    void start();
    
    private:
    /// The entire handshake process (PING -> REPLCONF -> PSYNC) merged into one chain.
    void handshake();
    void readResponse(std::shared_ptr<tcp::socket> socket, const std::string& context, std::function<void()> callback);

    /// Utility function to parse the master details string into host and port.
    static std::pair<std::string, std::string> parseHostPort(const std::string& masterDetails);

    asio::io_context& io_context_;
    std::string masterDetails_;
    std::shared_ptr<tcp::socket> master_socket_;
};

#endif // REPLICATIONCLIENT_H
