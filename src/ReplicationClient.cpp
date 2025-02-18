#include "ReplicationClient.h"

// Utility parser to split masterDetails into host and port.
std::pair<std::string, std::string> ReplicationClient::parseHostPort(const std::string& masterDetails) {
    std::istringstream iss(masterDetails);
    std::string host, port;
    iss >> host >> port;
    return {host, port};
}

ReplicationClient::ReplicationClient(asio::io_context& io_context, const std::string& masterDetails)
    : io_context_(io_context), masterDetails_(masterDetails)
{}

void ReplicationClient::start() {
    auto [masterHost, masterPort] = parseHostPort(masterDetails_);

    master_socket_ = std::make_shared<tcp::socket>(io_context_);
    tcp::resolver resolver(io_context_);
    auto endpoints = resolver.resolve(masterHost, masterPort);

    asio::async_connect(
        *master_socket_,
        endpoints,
        [this](asio::error_code ec, tcp::endpoint /*ep*/) {
            if (!ec) {
                std::cout << "Connected to master. Starting handshake..." << std::endl;
                handshake();
            } else {
                std::cerr << "Error connecting to master: " << ec.message() << std::endl;
            }
        }
    );
}

void ReplicationClient::readResponse(std::shared_ptr<tcp::socket> socket, const std::string& context, std::function<void()> callback) {
    auto response_buffer = std::make_shared<std::string>();
    asio::async_read_until(
        *socket,
        asio::dynamic_buffer(*response_buffer),
        "\r\n",
        [socket, response_buffer, context, callback](asio::error_code ec, std::size_t length) {
            if (!ec) {
                std::string response = response_buffer->substr(0, length);
                std::cout << "Received from master " << context << ": " << response << std::endl;
                callback();
            } else {
                std::cerr << "Error reading response " << context << ": " << ec.message() << std::endl;
            }
        }
    );
}

void ReplicationClient::handshake() {
    // Forward declarations of lambda functions.
    std::function<void()> sendPing;
    std::function<void()> sendReplConf;
    std::function<void()> sendPsync;

    // Step 1: Send PING
    sendPing = [this, &sendReplConf](/* no parameters */) {
        std::string ping_cmd = "*1\r\n$4\r\nPING\r\n";
        asio::async_write(
            *master_socket_,
            asio::buffer(ping_cmd),
            [this, &sendReplConf](asio::error_code ec, std::size_t /*length*/) {
                if (!ec) {
                    std::cout << "PING command sent successfully." << std::endl;
                    // Read PING response.
                    auto ping_response = std::make_shared<std::string>();
                    asio::async_read_until(
                        *master_socket_,
                        asio::dynamic_buffer(*ping_response),
                        "\r\n",
                        [this, ping_response, &sendReplConf](asio::error_code ec, std::size_t length) {
                            if (!ec) {
                                std::cout << "Response after PING: " 
                                          << ping_response->substr(0, length) << std::endl;
                                // Proceed to REPLCONF.
                                sendReplConf();
                            } else {
                                std::cerr << "Error reading PING response: " << ec.message() << std::endl;
                            }
                        }
                    );
                } else {
                    std::cerr << "Error sending PING: " << ec.message() << std::endl;
                }
            }
        );
    };

    // Step 2: Send REPLCONF commands
    sendReplConf = [this, &sendPsync]() {
        std::string first_replconf = "*3\r\n$8\r\nREPLCONF\r\n$14\r\nlistening-port\r\n$4\r\n6380\r\n";
        asio::async_write(
            *master_socket_,
            asio::buffer(first_replconf),
            [this, &sendPsync](asio::error_code ec, std::size_t /*length*/) {
                if (!ec) {
                    std::cout << "First REPLCONF sent successfully." << std::endl;
                    std::string second_replconf = "*3\r\n$8\r\nREPLCONF\r\n$4\r\ncapa\r\n$6\r\npsync2\r\n";
                    asio::async_write(
                        *master_socket_,
                        asio::buffer(second_replconf),
                        [this, &sendPsync](asio::error_code ec, std::size_t /*length*/) {
                            if (!ec) {
                                std::cout << "Second REPLCONF sent successfully." << std::endl;
                                // Read REPLCONF response.
                                auto replconf_response = std::make_shared<std::string>();
                                asio::async_read_until(
                                    *master_socket_,
                                    asio::dynamic_buffer(*replconf_response),
                                    "\r\n",
                                    [this, replconf_response, &sendPsync](asio::error_code ec, std::size_t length) {
                                        if (!ec) {
                                            std::cout << "Response after REPLCONF: " 
                                                      << replconf_response->substr(0, length) << std::endl;
                                            // Proceed to PSYNC.
                                            sendPsync();
                                        } else {
                                            std::cerr << "Error reading REPLCONF response: " << ec.message() << std::endl;
                                        }
                                    }
                                );
                            } else {
                                std::cerr << "Error sending second REPLCONF: " << ec.message() << std::endl;
                            }
                        }
                    );
                } else {
                    std::cerr << "Error sending first REPLCONF: " << ec.message() << std::endl;
                }
            }
        );
    };

    // Step 3: Send PSYNC
    sendPsync = [this]() {
        std::string psync_cmd = "*3\r\n$5\r\nPSYNC\r\n$1\r\n?\r\n$2\r\n-1\r\n";
        asio::async_write(
            *master_socket_,
            asio::buffer(psync_cmd),
            [this](asio::error_code ec, std::size_t /*length*/) {
                if (!ec) {
                    std::cout << "PSYNC command sent successfully." << std::endl;
                    // Read PSYNC response.
                    auto psync_response = std::make_shared<std::string>();
                    asio::async_read_until(
                        *master_socket_,
                        asio::dynamic_buffer(*psync_response),
                        "\r\n",
                        [this, psync_response](asio::error_code ec, std::size_t length) {
                            if (!ec) {
                                std::cout << "Response after PSYNC: " 
                                          << psync_response->substr(0, length) << std::endl;
                                std::cout << "Handshake complete." << std::endl;
                            } else {
                                std::cerr << "Error reading PSYNC response: " << ec.message() << std::endl;
                            }
                        }
                    );
                } else {
                    std::cerr << "Error sending PSYNC: " << ec.message() << std::endl;
                }
            }
        );
    };

    // Start the handshake chain by sending the PING.
    sendPing();
}
