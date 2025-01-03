#include "RchanServer.hh"

std::string RchanServer::hashPSWD( const std::string& input )
{
  EVP_MD_CTX* context = EVP_MD_CTX_new();
  if ( context == nullptr ) {
    return "";
  }

  if ( EVP_DigestInit_ex( context, EVP_sha256(), nullptr ) != 1 ) {
    EVP_MD_CTX_free( context );
    return "";
  }

  if ( EVP_DigestUpdate( context, input.c_str(), input.length() ) != 1 ) {
    EVP_MD_CTX_free( context );
    return "";
  }

  unsigned char hash[EVP_MAX_MD_SIZE];
  unsigned int lengthOfHash = 0;
  if ( EVP_DigestFinal_ex( context, hash, &lengthOfHash ) != 1 ) {
    EVP_MD_CTX_free( context );
    return "";
  }

  EVP_MD_CTX_free( context );

  std::stringstream ss;
  for ( unsigned int i = 0; i < lengthOfHash; i++ ) {
    ss << std::hex << std::setw( 2 ) << std::setfill( '0' ) << static_cast<int>( hash[i] );
  }
  return ss.str();
}

RchanServer::RchanServer() : server_fd( 0 ), new_socket( 0 ), addrlen( sizeof( address ) ), opt( 1 )
{

  servers["Rchan"] = "10.81.92.228";

  if ( ( server_fd = socket( AF_INET, SOCK_STREAM, 0 ) ) == 0 ) {
    perror( "socket failed" );
    exit( EXIT_FAILURE );
  }

  if ( setsockopt( server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof( opt ) ) ) {
    perror( "setsockopt failed" );
    exit( EXIT_FAILURE );
  }

  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons( PORT );

  if ( bind( server_fd, (sockaddr*)&address, sizeof( address ) ) < 0 ) {
    perror( "bind failed" );
    exit( EXIT_FAILURE );
  }

  if ( listen( server_fd, 3 ) < 0 ) {
    perror( "listen failed" );
    exit( EXIT_FAILURE );
  }

  std::cout << "Server listening on port " << PORT << "...\n";
}

RchanServer::~RchanServer()
{
  for ( int client : clients ) {
    close( client );
  }
}

void RchanServer::Run()
{
  while ( true ) {
    if ( ( new_socket = accept( server_fd, (sockaddr*)&address, (socklen_t*)&addrlen ) ) < 0 ) {
      perror( "accept failed" );
      exit( EXIT_FAILURE );
    }

    {
      std::lock_guard<std::mutex> lock( clientsMutex );
      std::cout << new_socket << " connected\n";
      clients.push_back( new_socket );
    }
    std::thread( [this]() { this->handleClient( new_socket ); } ).detach();
  }
}

void RchanServer::writeMessageToFile( const std::string& message )
{
  std::ofstream outFile( "chat_history.txt", std::ios::app );
  if ( outFile.is_open() ) {
    outFile << message << std::endl;
    outFile.close();
  }
}

void RchanServer::sendMessage( const nlohmann::json& message, int clientSocket )
{
  static std::string buffer;
  buffer.clear(); // Just resets length, keeps capacity
  buffer = message.dump();
  try {
    send( clientSocket, buffer.c_str(), buffer.length(), 0 );
  } catch ( const std::exception& e ) {
    std::cout << "Error sending message: " << e.what() << std::endl;
  }
}

void RchanServer::sendChatHistory( int clientSocket )
{
  std::ifstream inFile( "chat_history.txt" );
  std::string line;

  std::string chatHistory;
  while ( std::getline( inFile, line ) ) {
    chatHistory += line + "\n";
  }
  inFile.close();
  nlohmann::json message = { { "status", "success" }, { "type", "chat_history" }, { "message", chatHistory } };
  sendMessage( message, clientSocket );
}

void RchanServer::sendAvailableServers( int clientSocket )
{
  std::map<std::string, std::string> available_servers;
  for ( auto& server : servers ) {
    if ( server.first == "Rchan" ) {
      available_servers["Rchan"] = server.second + ":" + std::to_string( PORT );
    } else {
      available_servers[server.first]
        = server.second + ":" + std::to_string( localRChanServers[server.first].server_port );
    }
  }
  nlohmann::json message
    = { { "status", "success" }, { "type", "available_servers" }, { "servers", available_servers } };
  sendMessage( message, clientSocket );
}

void RchanServer::handleClient( int clientSocket )
{
  char buffer[BUFFER_SIZE] = {};
  std::string fragment_store;
  std::cout << "Handling Client\n";
  sendAvailableServers( clientSocket );

  while ( true ) {
    memset( buffer, 0, BUFFER_SIZE );
    int bytesRead = read( clientSocket, buffer, BUFFER_SIZE );

    if ( bytesRead <= 0 ) {
      std::cout << "Client disconnected.\n";
      break;
    }

    std::string REVCDmessage( buffer, bytesRead );
    std::cout << "Received: " << REVCDmessage << std::endl;
    std::pair<std::string, std::vector<nlohmann::json>> split = splitJSON( REVCDmessage );
    std::vector<nlohmann::json> messagesJSON = split.second;
    fragment_store += split.first;
    split = splitJSON( fragment_store );
    for ( auto it : split.second ) {
      messagesJSON.push_back( it );
    }
    fragment_store = split.first;

    for ( const nlohmann::json& messageJSON : messagesJSON ) {
      std::cout << "JSON>\n";
      std::cout << messageJSON.dump( 4 ) << std::endl;
      if ( messageJSON["type"].get<std::string>() == "get_servers" ) {
        sendAvailableServers( clientSocket );
      } else if ( messageJSON["type"].get<std::string>() == "chat_history" ) {
        sendChatHistory( clientSocket );
      } else if ( messageJSON["type"].get<std::string>() == "username" ) {
        std::string username = messageJSON["username"].get<std::string>();
        if ( std::find( usernames.begin(), usernames.end(), username ) != usernames.end() ) {
          nlohmann::json message
            = { { "status", "error" },
                { "type", "username" },
                { "message", "Username " + username + " already taken, please try a different user name" } };
          sendMessage( message, clientSocket );
        } else {
          usernames.push_back( username );
          nlohmann::json message = { { "status", "success" },
                                     { "type", "username" },
                                     { "message", "Username " + username + " successfully set" } };
          sendMessage( message, clientSocket );
        }
      } else if ( messageJSON["type"].get<std::string>() == "add_server" ) {
        std::string server_name = messageJSON["server_name"].get<std::string>();
        std::string server_ip = messageJSON["server_ip"].get<std::string>();
        int server_port = messageJSON["server_port"].get<int>();

        if ( servers.find( server_name ) != servers.end() ) {
          nlohmann::json message
            = { { "status", "error" },
                { "type", "add_server" },
                { "message", "Server " + server_name + " already exists, please try a different server name" } };
          sendMessage( message, clientSocket );
        } else {
          servers[server_name] = server_ip;
          localRChanServers[server_name]
            = server_info { server_name,
                            server_ip,
                            server_port,
                            hashPSWD( messageJSON["root_password"].get<std::string>() ),
                            hashPSWD( messageJSON["server_password"].get<std::string>() ) };

          nlohmann::json message = { { "status", "success" },
                                     { "type", "add_server" },
                                     { "message", "Server " + server_name + " successfully added" },
                                     { "server_port", server_port } };
          sendMessage( message, clientSocket );
          std::cout << "Server " << server_name << " joined Rchan!\n";

          // Broadcast updated servers to all clients
          {
            std::lock_guard<std::mutex> lock( clientsMutex );
            for ( int client : clients ) {
              sendAvailableServers( client );
            }
          }
        }
      } else if ( messageJSON["type"].get<std::string>() == "remove_server" ) {
        std::string server_name = messageJSON["server_name"].get<std::string>();
        std::string root_password = hashPSWD( messageJSON["root_password"].get<std::string>() );

        if ( localRChanServers.find( server_name ) != localRChanServers.end() ) {
          if ( localRChanServers[server_name].root_password != root_password ) {
            nlohmann::json message = { { "status", "error" },
                                       { "type", "remove_server" },
                                       { "message", "Incorrect root password for server " + server_name } };
            sendMessage( message, clientSocket );
          } else {
            localRChanServers.erase( server_name );
            servers.erase( server_name );
            nlohmann::json message = { { "status", "success" },
                                       { "type", "remove_server" },
                                       { "message", "Server " + server_name + " successfully removed" } };
            sendMessage( message, clientSocket );
            std::cout << "Server " << server_name << " left Rchan!\n";

            // Broadcast updated servers to all clients
            {
              std::lock_guard<std::mutex> lock( clientsMutex );
              for ( int client : clients ) {
                sendAvailableServers( client );
              }
            }
          }
        } else {
          nlohmann::json message = { { "status", "error" },
                                     { "type", "remove_server" },
                                     { "message", "Server " + server_name + " does not exist" } };
          sendMessage( message, clientSocket );
        }
      } else if ( messageJSON["type"].get<std::string>() == "chat" ) {
        std::string username = messageJSON["username"].get<std::string>();
        std::string msg = messageJSON["message"].get<std::string>();
        std::string timestampedMessage = "[" + getCurrentTime() + "] " + username + "> " + msg + "\n";
        writeMessageToFile( timestampedMessage );
        nlohmann::json message = { { "status", "success" }, { "type", "chat" }, { "message", timestampedMessage } };
        {
          std::lock_guard<std::mutex> lock( clientsMutex );
          for ( int client : clients ) {
            sendMessage( message, client );
          }
        }
      }
    }
  }

  {
    std::lock_guard<std::mutex> lock( clientsMutex );
    clients.erase( std::remove( clients.begin(), clients.end(), clientSocket ), clients.end() );
  }

  close( clientSocket );
}
