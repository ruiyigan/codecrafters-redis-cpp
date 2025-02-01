#include <iostream>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/epoll.h>

// For convenience in this example:
static const int PORT = 6379;
static const int MAX_EVENTS = 10;
static const int BUFFER_SIZE = 1024;

int main(int argc, char **argv)
{
    // Flush after every std::cout / std::cerr
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;

    // 1. Create server socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
    {
        std::cerr << "Failed to create server socket\n";
        return 1;
    }

    // 2. Allow reuse of the address
    int reuse = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
    {
        std::cerr << "setsockopt failed\n";
        close(server_fd);
        return 1;
    }

    // 3. Bind to port
    struct sockaddr_in server_addr;
    std::memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) != 0)
    {
        std::cerr << "Failed to bind to port " << PORT << "\n";
        close(server_fd);
        return 1;
    }

    // 4. Listen
    if (listen(server_fd, SOMAXCONN) != 0)
    {
        std::cerr << "listen failed\n";
        close(server_fd);
        return 1;
    }

    std::cout << "Server listening on port " << PORT << "...\n";

    // 5. Create an epoll instance
    int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0)
    {
        std::cerr << "epoll_create1 failed\n";
        close(server_fd);
        return 1;
    }

    // 6. Add the server socket to the epoll instance
    struct epoll_event event;
    event.data.fd = server_fd;
    event.events = EPOLLIN; // We want to be notified when there's data to read (or incoming connections)
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &event) < 0)
    {
        std::cerr << "epoll_ctl (add server_fd) failed\n";
        close(server_fd);
        close(epoll_fd);
        return 1;
    }

    // 7. The main event loop
    while (true)
    {
        struct epoll_event events[MAX_EVENTS];
        // Wait for events on any of the file descriptors in the epoll instance
        int num_events = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (num_events < 0)
        {
            std::cerr << "epoll_wait failed\n";
            break; // You might want more graceful handling
        }

        // 8. Process each ready file descriptor
        for (int i = 0; i < num_events; i++)
        {
            // Check if it's the server socket (i.e., new connection)
            if (events[i].data.fd == server_fd)
            {
                // Accept new connection
                struct sockaddr_in client_addr;
                socklen_t client_len = sizeof(client_addr);
                int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
                if (client_fd < 0)
                {
                    std::cerr << "Accept failed\n";
                    continue;
                }

                std::cout << "Client connected, fd=" << client_fd << std::endl;

                // Set up the event for the new client
                struct epoll_event client_event;
                client_event.data.fd = client_fd;
                client_event.events = EPOLLIN; // Listen for reads from this client
                if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &client_event) < 0)
                {
                    std::cerr << "epoll_ctl (add client_fd) failed\n";
                    close(client_fd);
                    continue;
                }
            }
            else
            {
                // We have data from an existing client
                int client_fd = events[i].data.fd;
                if (events[i].events & EPOLLIN)
                {
                    // Read data from the client
                    char buffer[BUFFER_SIZE];
                    int bytes_received = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
                    if (bytes_received > 0)
                    {
                        buffer[bytes_received] = '\0';
                        std::cout << "Received from client_fd=" << client_fd
                                  << ": " << buffer << std::endl;

                        // Respond with a PONG (Redis-like response)
                        const char *msg = "+PONG\r\n";
                        send(client_fd, msg, std::strlen(msg), 0);
                    }
                    else if (bytes_received == 0)
                    {
                        // Client disconnected
                        std::cout << "Client fd=" << client_fd << " disconnected\n";
                        // Remove from epoll and close
                        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, nullptr);
                        close(client_fd);
                    }
                    else
                    {
                        // Error on recv
                        std::cerr << "recv error on fd=" << client_fd << "\n";
                        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, nullptr);
                        close(client_fd);
                    }
                }
            }
        }
    }

    // Cleanup: close server_fd and epoll_fd
    close(server_fd);
    close(epoll_fd);

    return 0;
}
