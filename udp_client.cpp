// Client side implementation of UDP file transport

#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>

#include <cassert>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <thread>

#include "udp_util.h"

class UdpClient {
private:
  static const int k_SERVER_RESPONSE_TIMEOUT = 5000; // 5 sec
  static const int k_SENDING_TIMEOUT_MIN     = 10;   // 10 ms
  static const int k_SENDING_TIMEOUT_MAX     = 100;  // 100 ms

  
  int                                d_sockFd;
                                       // socket fd
  
  sockaddr_in                        d_servAddr;
                                       // remote host data
  
  std::time_t                        d_lastFileId;
                                       // timestamp used and file id
  
  std::random_device                 d_randomDevice;
                                       // random device
  
  std::default_random_engine         d_randomEngine;
                                       // to generate random timeouts
  
  std::uniform_int_distribution<int> d_uniformDist;
                                       // range of timeout values

  

  int send(const char* data, size_t len)
  {
    int res = sendto(d_sockFd,
		     data,
		     len,
		     MSG_CONFIRM,
                     NULL, // sockaddr* not needed if connect was called
		     sizeof d_servAddr);

    return res;
  }

  std::time_t createFileId()
  {
    return ++d_lastFileId;
  }

  void sendGreeting()
  {
    const size_t msgLen = sizeof udpdemo::UdpUtil::k_START_CLIENT_MSG;
    const int    sent   = send(udpdemo::UdpUtil::k_START_CLIENT_MSG, msgLen);
    if (sent != msgLen) {
      std::cerr << "Failed to send greeting\n";
      throw std::runtime_error("Socket send error");
    }
  }

  bool checkServerResponse()
  {
    // Wait for response
    pollfd pfds[1];
    pfds[0].fd     = d_sockFd;
    pfds[0].events = POLLIN; // Tell me when ready to read
      
    std::cout << "Waiting for Server response\n";
      
    int numEvents = poll(pfds, 1, k_SERVER_RESPONSE_TIMEOUT);
      
    if (numEvents <= 0) {
      return false;
    }
      
    bool pollinHappened = pfds[0].revents & POLLIN;
      
    if (!pollinHappened) {
      return false;
    }
    const size_t respLen = sizeof udpdemo::UdpUtil::k_START_SERVER_MSG;    
    char         buffer[respLen];
    socklen_t    socLen = sizeof d_servAddr;  // socLen is value/resuslt
    memset(buffer, 0, sizeof buffer);

    int byteCount = recvfrom(d_sockFd,
                             (char*)buffer,
                             sizeof buffer,
                             MSG_WAITALL,
                             (sockaddr*)&d_servAddr,
                             &socLen);

    if (byteCount != respLen) {
      std::cout << "Unexpected server response of size " << byteCount
                << std::endl;
      return false;
    }
    
    const std::string respMsg(udpdemo::UdpUtil::k_START_SERVER_MSG);
    const std::string incomingMsg(buffer);

    if (respMsg != incomingMsg) {
      std::cout << "Unexpected server response [" << incomingMsg << "]\n";
      return false;
    }

    return true;
  }

  void makePause()
  {
    int timeout = d_uniformDist(d_randomEngine);
    std::this_thread::sleep_for(std::chrono::milliseconds(timeout));
  }

public:
  
  UdpClient(const std::string& host, int port)
    :d_lastFileId(std::time(nullptr))
    ,d_randomDevice()
    ,d_randomEngine(d_randomDevice())
    ,d_uniformDist(k_SENDING_TIMEOUT_MIN, k_SENDING_TIMEOUT_MAX)
  {
    // Creating socket file descriptor
    if ( (d_sockFd = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) {
      throw std::runtime_error("Socket creation failed");
    }

    memset(&d_servAddr, 0, sizeof d_servAddr);

    // Filling server information
    d_servAddr.sin_family = AF_INET;
    d_servAddr.sin_port   = htons(port);
    
    if (host == udpdemo::UdpUtil::k_DEFAULT_HOST) {
      d_servAddr.sin_addr.s_addr = INADDR_ANY;
    } else {
      std::cout << "Setting host " << host << std::endl;
      if (inet_pton(AF_INET, host.data(), &(d_servAddr.sin_addr)) <= 0) {
        throw std::runtime_error("Invalid IP address");
      }
    }

    // Setup remote host data
    int res = connect(d_sockFd,
    		      (sockaddr*)&d_servAddr,
    		      sizeof d_servAddr);
    if (res < 0) {
      std::cerr << "Connect error: " << res << std::endl;
      throw std::runtime_error("Connect failed");
    }
    std::cout << "Connected. Host [" << host
              << "] Port [" << port << "]\n";
  }

  ~UdpClient()
  {
    close(d_sockFd);
  }

  // Non copyable
  UdpClient(const UdpClient&)            = delete;
  UdpClient& operator=(const UdpClient&) = delete;

  bool waitServer()
  {
    for(;;) {
      sendGreeting();

      if (checkServerResponse()) {
        return true;
      }
    }
    return false;
  }

  void sendFile(const std::string& fName)
  {
    // Open a file for reading in binary mode and set position to the end of
    // file
    std::ifstream file(fName.data(),
                       std::ios::in|std::ios::binary|std::ios::ate);
    
    if (!file.is_open()) {
      std::cerr << "Unable to open file " << fName << std::endl;
      throw std::runtime_error("File open error");
    }

    // Get the file size and move to the beginning
    std::streampos fileSize  = file.tellg();
    file.seekg(0, std::ios::beg);

    const std::time_t fileId = createFileId();

    std::cout << "Sending file data of size " << fileSize
              << ". FileID: " << fileId << std::endl;
    
    while (fileSize > 0) {
      char buffer[udpdemo::UdpUtil::k_DATA_BUF_SIZE];
      memset(buffer, 0, sizeof buffer);

      // Add timestamp
      int res = snprintf(buffer,
                         udpdemo::UdpUtil::k_TIMESTAMP_BUF_SIZE,
                         "%lu",
                         fileId);
      if (res < 0) {
        std::cerr << "Failed to add file ID for " << fName << std::endl;
        throw std::runtime_error("IO error");
      }

      // Add payload
      const size_t payloadLen = file.read(
                                        buffer
                                      + udpdemo::UdpUtil::k_TIMESTAMP_BUF_SIZE,
                                      udpdemo::UdpUtil::k_PAYLOAD_SIZE)
                                    .gcount();
      const size_t msgLen     = udpdemo::UdpUtil::k_TIMESTAMP_BUF_SIZE +
                                                                    payloadLen;
      // Send datagram
      const int sent = send(buffer, msgLen);
      if (sent != msgLen) {
	std::cerr << "Failed to send file data " << fName << std::endl;
        throw std::runtime_error("Socket send error");
      }
      fileSize -= payloadLen;
      makePause();
    }
    file.close();
  }
};

int
main(int         argc,
     const char *argv[])
{
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " <file_name> [ip4 address] [port]" << std::endl;
    return -1;
  }

  std::string filename;
  std::string host    = udpdemo::UdpUtil::k_DEFAULT_HOST;
  int         port    = udpdemo::UdpUtil::k_DEFAULT_PORT;
  const int   maxArgs = std::min(argc, 4);

  for (int i = 1; i < maxArgs; ++i) {
    switch(i) {
    case 1: {
      filename = argv[i];
    } break;
    case 2: {
      host = argv[i];
    } break;
    case 3: {
      int val;
      std::istringstream iss(argv[i]);
      if (iss >> val) {
        port = val;
      }
    } break;
    default:
      break;
    }
  }

  try {
    
    UdpClient cli(host, port);
    if (cli.waitServer()) {
      cli.sendFile(filename);
    }
    
  } catch (const std::runtime_error& e) {
    std::cerr << "Runtime exception: " << e.what() << std::endl;
    return -1;
  } catch (...) {
    std::cerr << "Unknown exception" << std::endl;
    return -2;
  }
  
  return 0;
}
