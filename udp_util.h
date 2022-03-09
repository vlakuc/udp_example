#ifndef _UDP_UTIL_H_
#define _UDP_UTIL_H_

#include <ctime>
#include <limits>

namespace udpdemo {

class UdpUtil {
public:
  static const     size_t k_TIMESTAMP_BUF_SIZE = 20;
  static const     size_t k_DATA_BUF_SIZE      = 512;
  static const     size_t k_PAYLOAD_SIZE       = k_DATA_BUF_SIZE -
                                                          k_TIMESTAMP_BUF_SIZE;
  static const     int    k_DEFAULT_PORT       = 8080;
  static constexpr char   k_DEFAULT_HOST[]     = "localhost";
  static constexpr char   k_START_CLIENT_MSG[] = "START";
  static constexpr char   k_START_SERVER_MSG[] = "OK";

  // Verify timestamp buffer size is enough to store string representation of
  // std::time_t::max() plus end of line symbol
  static_assert(   std::numeric_limits<std::time_t>::digits10 + 1 + 1
                <= k_TIMESTAMP_BUF_SIZE);
};

}; // eof udpdemo namespace

#endif
