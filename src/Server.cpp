#include <iostream>
#include <memory>       // For smart pointers and enable_shared_from_this
#include <asio.hpp>
#include <vector>
#include <sstream>

using asio::ip::tcp;  // Simplify TCP namespace

// Session handles each client connection. Inherits from enable_shared_from_this
// to allow safe shared_ptr management in async callbacks
class Session : public std::enable_shared_from_this<Session> {
public:
    // Constructor takes ownership of the socket
    Session(tcp::socket socket) : socket_(std::move(socket)) {}

    // Start the session's async operations
    void start() {
        read();  // Initiate first read
    }

private:
    std::vector<std::string> splitString(const std::string& input, char delimiter) {
        std::vector<std::string> tokens;
        std::stringstream stream(input);
        std::string token;

        while (std::getline(stream, token, delimiter)) {
            if (!token.empty() && token.back() == '\r') {
                token.pop_back();
            }
            tokens.push_back(token);
        }

        return tokens;
    }

    void read() {
        // Capture a shared_ptr to keep object alive during async operation
        auto self(shared_from_this());
        
        // Async read with buffer and completion handler
        socket_.async_read_some(asio::buffer(buffer_),
            // Lambda captures 'this' and self (shared_ptr) for proper lifetime
            [this, self](asio::error_code ec, std::size_t length) {
                if (!ec) {
                    // Successfully read data
                    std::string data = std::string(buffer_.data(), length);
                    std::cout << "Received: \n" << data << std::endl;
                    std::vector<std::string> split_data = splitString(data, '\n');

                    std::string message;
                    if (split_data[2] == "ECHO") {
                        // repeat
                        message = split_data.back();
                    } else {
                        message = "PONG";
                    }

                    write(message, message.size());  // Respond to client
                } else {
                    // Handle errors (including client disconnects)
                    if (ec != asio::error::eof) {
                        std::cerr << "Read error: " << ec.message() << std::endl;
                    }
                }
            });
    }

    void write(std::string data, int length) {
        auto self(shared_from_this());
        std::stringstream msg_stream;
        msg_stream << "$" << length << "\r\n" << data << "\r\n";
        std::string msg = msg_stream.str();
        
        // Async write operation
        asio::async_write(socket_, asio::buffer(msg, msg.size()),
            [this, self](asio::error_code ec, std::size_t /*length*/) {
                if (!ec) {
                    read();  // Continue reading after successful write
                } else {
                    std::cerr << "Write error: " << ec.message() << std::endl;
                }
            });
    }

    tcp::socket socket_;          // Client connection socket
    std::array<char, 1024> buffer_;  // Data buffer (fixed-size array)
};

void accept_connections(tcp::acceptor& acceptor) {
    // Async accept with completion handler
    acceptor.async_accept(
        // Lambda captures acceptor by reference
        [&acceptor](asio::error_code ec, tcp::socket socket) {
            if (!ec) {
                // Create session for new client and start it
                std::make_shared<Session>(std::move(socket))->start();
                std::cout << "Client connected" << std::endl;
            }
            // Continue accepting new connections (recursive call)
            accept_connections(acceptor);
        });
}

int main() {
    try {
        asio::io_context io_context;
        
        // Create acceptor listening on port 6379 (IPv4)
        tcp::acceptor acceptor(io_context, tcp::endpoint(tcp::v4(), 6379));
        
        // Start accepting connections
        accept_connections(acceptor);
        std::cout << "Server listening on port 6379..." << std::endl;
        
        // Run the I/O service - blocks until all work is done
        io_context.run();
    } catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
