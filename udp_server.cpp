// Server side implementation of UDP file transport

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <string.h>
#include <unistd.h>

#include <cassert>
#include <condition_variable>
#include <csignal>
#include <ctime>
#include <deque>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <thread>

#include "udp_util.h"

namespace
{
  volatile std::sig_atomic_t gSignalStatus;
}

void signal_handler(int signal)
{
  gSignalStatus = signal;
}

class UdpServer {
private:
  static const int k_SERVER_READ_TIMEOUT = 1000; // 1 sec
  static const int k_STAT_DUMP_INTERVAL  = 5;    // 5 sec
  static const int k_MAX_STAT_ITEMS      = 15;
  
  struct DataEvent {
    // Represent incoming message
    
    char             d_buffer[udpdemo::UdpUtil::k_DATA_BUF_SIZE];
                       // buffer for message data
    
    sockaddr_in      d_clientAddr;
                       // client socket data
    
    std::string_view d_fileId;
                       // fileId stored in this message
    
    size_t           d_dataLen;
                       // actual payload length
    
    bool             d_isValid;
                       // flag to indicate the message has expected layout
    
    bool             d_isGreeting;
                       // flag to indicate the message is greeting message
    
    bool             d_isData;
                       // flag to idicate the message carries file data
    
    DataEvent()
      :d_dataLen(0)
      ,d_isValid(false)
      ,d_isGreeting(false)
      ,d_isData(false)
    {
      memset(&d_buffer, 0, sizeof d_buffer);
      memset(&d_clientAddr, 0, sizeof d_clientAddr);
    }

    void validate()
    {
      if (d_isValid) {
        return;
      }
      
      const size_t greetingLen = sizeof udpdemo::UdpUtil::k_START_CLIENT_MSG;
            
      if (d_dataLen == greetingLen) {
        const std::string_view greetingMsg(udpdemo::UdpUtil::k_START_CLIENT_MSG);
        const std::string_view incomingMsg(d_buffer);
        if (greetingMsg == incomingMsg) {
          d_isValid    = true;
          d_isGreeting = true;
          return;
        }
      } else if (d_dataLen < udpdemo::UdpUtil::k_TIMESTAMP_BUF_SIZE) {
        std::cerr << "Unexpected message of size " << d_dataLen << std::endl;
        return;
      }

      // Read fileId. It should be withing the first k_TIMESTAMP_BUF_SIZE
      // bytes
      std::string_view fileId(d_buffer,
                              udpdemo::UdpUtil::k_TIMESTAMP_BUF_SIZE);
      auto pos = fileId.find_first_of('\0');

      if (pos == std::string_view::npos) {
        std::cerr << "Message with unexpected file id: " << d_buffer
                  << std::endl;
        return;
      }
      
      d_fileId = fileId.substr(0, pos);
      d_isValid = true;
      d_isData  = true;
    }

    std::string getClientAddress() const
    {
      char ipstr[INET_ADDRSTRLEN];
      memset(&ipstr, 0, sizeof ipstr);
    
      inet_ntop(AF_INET,
                &((sockaddr_in*)&d_clientAddr)->sin_addr,
                ipstr,
                sizeof ipstr);

      return ipstr;
    }

    int getClientPort() const
    {
      return ((sockaddr_in*)&d_clientAddr)->sin_port;
    }

    const std::string_view& getFileId() const
    {
      return d_fileId;
    }
    
    bool isValid()
    {
      return d_isValid;
    }

    bool isGreeting()
    {
      assert(isValid() && "Event not valid");
      return d_isGreeting;
    }

    bool isData()
    {
      assert(isValid() && "Event not valid");
      return d_isData;
    }

  };

  typedef std::shared_ptr<DataEvent> DataEventSp;
  typedef std::deque<DataEventSp>    DataQueue;
  typedef std::map<std::string, int> StatData;

  int                     d_sockFd;
                            // server socket descriptor
  
  sockaddr_in             d_servAddr;
                            // server socket data
  
  DataQueue               d_dataQueue;
                            // queue of the incoming messages
  
  std::mutex              d_dataQueueMutex;
                            // mutex to protect message queue
  
  std::condition_variable d_dataQueueCv;
                            // condition variable to organize concurrent access
                            // to the message queue
  
  std::thread             d_storageThread;
                            // thread to handle the message queue and store
                            // data into files
  
  std::time_t             d_lastStatDump;
                            // timestamp of the last dump of statistics
  
  StatData                d_statDataMap;
                            // a map that binds file name and file size to show
                            // server's statistics
  
  std::string getFileName(const DataEventSp& event)
  {
    assert(event);
    
    // Return string of <host_port_fileId.dat>
    std::stringstream str(std::ios_base::out);

    str << event->getClientAddress() << "_" << event->getClientPort()
        << "_" << event->getFileId() << ".dat";

    return str.str();
  }

  void dumpStat()
  {
    auto now = std::time(nullptr);
    if (now - d_lastStatDump >= k_STAT_DUMP_INTERVAL) {
      d_lastStatDump = now;
      
      std::cout << "*** Stats: \n";
        
      for (const auto &item : d_statDataMap) {
        std::cout << "Stored " << item.second << " bytes into " << item.first
                  << std::endl;
      }
      std::cout << "*** Active clients: " << d_statDataMap.size() << std::endl;
      if (d_statDataMap.size() > k_MAX_STAT_ITEMS) {
        d_statDataMap.clear();
      }
    }
  }

  void writeToFile(const std::string& fName, char* data, size_t len)
  {
    std::ofstream file(fName,  std::ios::out
                              |std::ios::binary
                              |std::ios::app);

    if (!file.is_open()) {
      std::cerr << "Unable to open file for writing " << fName << std::endl;
      return;
    }
    if (!file.write(data, len)) {
      std::cerr << "Failed to append data to " << fName << std::endl;
    }
    d_statDataMap[fName] = file.tellp();
  }

  bool handleGreeting(const DataEventSp& event)
  {
    assert(event->isGreeting());

    const size_t greetingLen = sizeof udpdemo::UdpUtil::k_START_SERVER_MSG;
    
    std::cout << "Sending response to new client from "
              << event->getClientAddress() << std::endl;

    int sent = sendto(d_sockFd,
                      udpdemo::UdpUtil::k_START_SERVER_MSG,
                      greetingLen,
                      MSG_CONFIRM,
                      (const sockaddr*)&event->d_clientAddr,
                      sizeof event->d_clientAddr);

    if (sent != greetingLen) {
      std::cout << "Failed to send response to new client\n";
    }
    
    return true;
  }

  void store(const DataEventSp& event)
  {
    assert(event);
    
    std::string   fileName   = getFileName(event);
    char         *payload    = event->d_buffer
                                      + udpdemo::UdpUtil::k_TIMESTAMP_BUF_SIZE;
    const size_t  payloadLen = event->d_dataLen
                                      - udpdemo::UdpUtil::k_TIMESTAMP_BUF_SIZE;

    writeToFile(fileName, payload, payloadLen);
  }

  void addDataEvent(const DataEventSp& event)
  {
    std::unique_lock<std::mutex> guard(d_dataQueueMutex);
    d_dataQueue.push_back(event);
    d_dataQueueCv.notify_all();
  }

  DataEventSp waitDataEvent()
  {
    std::unique_lock<std::mutex> guard(d_dataQueueMutex);
    d_dataQueueCv.wait(guard, [this]{ return !d_dataQueue.empty(); });
    DataEventSp ev = d_dataQueue.front();
    d_dataQueue.pop_front();
    return ev;
  }

  void storageWorker()
  {
    std::cout << "Storage worker started\n";
    for(;;) {
      DataEventSp ev = waitDataEvent();
      if (!ev) {
        // poison pill
        break;
      }
      store(ev);
      dumpStat();
    }
    std::cout << "Storage worker finished\n";
  }

  bool waitClientData()
  {
    pollfd pfds[1];
    pfds[0].fd     = d_sockFd;
    pfds[0].events = POLLIN; // Tell me when ready to read
      
    int numEvents = poll(pfds, 1, k_SERVER_READ_TIMEOUT);
      
    if (numEvents <= 0) {
      return false;
    }
      
    bool pollinHappened = pfds[0].revents & POLLIN;
      
    return pollinHappened;
  }


public:
  UdpServer(int port = udpdemo::UdpUtil::k_DEFAULT_PORT)
    :d_storageThread(&UdpServer::storageWorker, this)
    ,d_lastStatDump(std::time(nullptr))
  {
    // Creating socket file descriptor
    if ( (d_sockFd = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) {
      throw std::runtime_error("socket creation failed");
    }

    memset(&d_servAddr, 0, sizeof d_servAddr);

    // Filling server information
    d_servAddr.sin_family      = AF_INET; // IPv4
    d_servAddr.sin_port        = htons(port);
    d_servAddr.sin_addr.s_addr = INADDR_ANY;

    // Bind the socket with the server address
    if ( bind(d_sockFd,
	      (const struct sockaddr *)&d_servAddr,
	      sizeof d_servAddr) < 0 ) {
      throw std::runtime_error("bind failed");
    }

    // Start storage thread
    
  }

  ~UdpServer()
  {
    close(d_sockFd);
    DataEventSp empty;
    addDataEvent(empty);
    d_storageThread.join();
  }

  // Non copyable
  UdpServer(const UdpServer&)            = delete;
  UdpServer& operator=(const UdpServer&) = delete;

  void run()
  {
    std::cout << "Listen to incoming clients" << std::endl;
    
    while (gSignalStatus == 0) {

      if (!waitClientData()) {
        continue;
      }
      
      auto      event  = std::make_shared<DataEvent>();
      socklen_t socLen = sizeof event->d_clientAddr;  // socLen is value/resuslt

      event->d_dataLen = recvfrom(d_sockFd,
                                  (char*)event->d_buffer,
                                  sizeof event->d_buffer,
                                  MSG_WAITALL,
                                  (sockaddr*)&(event->d_clientAddr),
                                  &socLen);
      event->validate();
      
      if (!event->isValid()) {
        continue;
      }

      if (event->isGreeting()) {
        handleGreeting(event);
        continue;
      }

      addDataEvent(event);
    }
  }
};

int main() {

  // Install a signal handler
  std::signal(SIGINT, signal_handler);

  try {
    
    UdpServer srv;
    srv.run();
    
  } catch(const std::runtime_error& e) {
    std::cerr << "Runtime exception: " << e.what() << std::endl;
    return -1;
  } catch(...) {
    std::cerr << "Unknown exception" << std::endl;
    return -2;
  }

  return 0;
}
