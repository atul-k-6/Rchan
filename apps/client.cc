#define RCHAN_CLIENT

#include "Rchan_req.hpp"

class RchanClient {
private:
    std::atomic<bool> running{true};
    std::string username{};
    std::unique_ptr<RchanSocket> RsockPtr{};
    std::string RchanServerIP{};
    std::unique_ptr<std::thread> RchanClientPtr{};
    // server_name -> server_ip
    std::map<std::string, std::string> servers{};
    std::mutex socketMutex{};
public:
    void listenForMessages(std::unique_ptr<RchanSocket>& sock) {
        static std::string buffer;

        while (running && !sock -> eof()) {
            sock -> read(buffer);
            if (!buffer.empty()) {
                // std::cout << "Received: " << buffer << std::endl;
                std::vector<json> messages = splitJSON(buffer);
                // std::cout << messages.size() << std::endl;
                for(const json& message : messages) {
                    // std::cout << "Message: " << message.dump(4) << std::endl;
                    if(message["status"].get<std::string>() == "error") {
                        if(message["type"] == "username") {
                            std::cout << "Error: " << message["message"].get<std::string>() << std::endl;
                            getUserName();
                        } else {
                            std::cout << "Error: " << message["message"].get<std::string>() << std::endl;
                        }
                    } else if (message["type"].get<std::string>() == "chat") {
                        std::cout << message["message"].get<std::string>() << std::endl;
                    } else if (message["type"].get<std::string>() == "username") {
                        std::cout << message["message"].get<std::string>() << std::endl;
                    } else if (message["type"] == "available_servers") {
                        servers = message["servers"].get<std::map<std::string, std::string>>();
                        for(auto& server : servers) {
                            std::cout << server.first << " -> " << server.second << std::endl;
                        }
                    } else if (message["type"].get<std::string>() == "add_server") {
                        std::cout << "Added local server to Rchan!\n";
                    } else if (message["type"].get<std::string>() == "remove_server") {
                        std::cout << "Removed local server from Rchan!\n";
                    } else if (message["type"].get<std::string>() == "chat_history") {
                        std::cout << message["message"].get<std::string>() << std::endl;
                        std::cout << "Chat history loaded!\n";
                    }
                }
            }
            buffer.clear();
        }
    }

    RchanClient() {
        RchanServerIP = "10.81.92.228";
        {
            std::lock_guard<std::mutex> lock(socketMutex);
            RsockPtr = std::make_unique<RchanSocket>();
            RsockPtr->connect(Address(RchanServerIP, 8080));
        }
        std::cout << "Connected to Server" << std::endl;
        RchanClientPtr = std::make_unique<std::thread>([this]() { listenForMessages(std::ref(RsockPtr)); });
        RchanClientPtr->detach();
    }

    ~RchanClient() {
        running = false;
        if (RchanClientPtr && RchanClientPtr->joinable()) {
            RchanClientPtr->join();
        }
        RsockPtr -> wait_until_closed();
        RsockPtr.reset();
    }

    void EnterServer(std::string server_name) {
        if (servers.find(server_name) == servers.end()) {
            std::cout << "Error: Server " << server_name << " not found!" << std::endl;
            return;
        }

        // First stop the thread
        running = false;

        // Wait for thread to finish
        if (RchanClientPtr && RchanClientPtr->joinable()) {
            RchanClientPtr->join();
        }

        RsockPtr -> wait_until_closed();
        RsockPtr.reset();

        // Give some time for the socket cleanup
        // std::this_thread::sleep_for(std::chrono::milliseconds(2000));

        try {
            {
                std::lock_guard<std::mutex> lock(socketMutex);
                RsockPtr = std::make_unique<RchanSocket>();
                RsockPtr->connect(Address(servers[server_name], 8080));
            }

            running = true;
            RchanClientPtr = std::make_unique<std::thread>([this]() { 
                listenForMessages(std::ref(RsockPtr)); 
            });
            RchanClientPtr->detach();

            getChatHistory();
        }
        catch (const std::exception& e) {
            std::cout << "Error connecting to server: " << e.what() << std::endl;
            running = false;
        }
    }



    void HostServer() {
        std::cout << "Enter server IP> ";
        std::string host_server_ip;
        std::getline(std::cin >> std::ws, host_server_ip);
        std::cout << "Enter server name> ";
        std::string server_name;
        std::getline(std::cin >> std::ws, server_name);
        std::cout << "Enter root password> ";
        std::string root_password;
        std::getline(std::cin >> std::ws, root_password);
        std::cout << "Enter server password> ";
        std::string server_password;
        std::getline(std::cin >> std::ws, server_password);
        json message = {
            {"type", "add_server"},
            {"server_name", server_name},
            {"server_ip", host_server_ip},
            {"root_password", root_password},
            {"server_password", server_password}
        };
        {
            std::lock_guard<std::mutex> lock(socketMutex);
            RsockPtr -> write(message.dump());
        }
    }

    void unHostServer() {
        std::cout << "Enter server name> ";
        std::string unhost_server_name;
        std::getline(std::cin >> std::ws, unhost_server_name);
        std::cout << "Enter root password> ";
        std::string root_password;
        std::getline(std::cin >> std::ws, root_password);
        json message = {
            {"type", "remove_server"},
            {"server_name", unhost_server_name},
            {"root_password", root_password}
        };
        {
            std::lock_guard<std::mutex> lock(socketMutex);
            RsockPtr -> write(message.dump());
        }
    }

    void getUserName() {
        std::cout << "Enter username> ";
        std::getline(std::cin >> std::ws, username);
        json message = {
            {"type", "username"},
            {"username", username}
        };
        {
            std::lock_guard<std::mutex> lock(socketMutex);
            RsockPtr -> write(message.dump());
        }
    }

    void getChatHistory() {
        json message = {
            {"type", "chat_history"}
        };
        {
            std::lock_guard<std::mutex> lock(socketMutex);
            RsockPtr -> write(message.dump());
        }
    }

    void sendMessage(std::string message) {
        json messageJson = {
            {"type", "chat"},
            {"username", username},
            {"message", message}
        };
        {
            std::lock_guard<std::mutex> lock(socketMutex);
            RsockPtr -> write(messageJson.dump());
        }
    }
};

int main() {
    try {
        RchanClient client;
        client.getUserName();
        // std::this_thread::sleep_for(std::chrono::seconds(1));
        
        // Add error handling for server entry
        try {
            client.EnterServer("Rchan");
        } catch (const std::exception& e) {
            std::cout << "Error entering server: " << e.what() << std::endl;
            return 1;
        }
        
        // std::this_thread::sleep_for(std::chrono::seconds(1));
        client.sendMessage("Hello World!");
        while(true) {}
    } catch (const std::exception& e) {
        std::cout << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}