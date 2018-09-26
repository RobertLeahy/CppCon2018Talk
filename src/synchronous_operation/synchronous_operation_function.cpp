#include <chrono>
#include <cstring>
#include <experimental/net>
#include <iostream>
#include <system_error>
#include <thread>
#include <type_traits>
#include <utility>
#include <unistd.h>
#include <time.h>

namespace std::net {

using namespace experimental::net;

}

void write_all(int sock, const void* ptr, std::size_t size) {
  for (std::size_t written = 0; written != size;) {
    if (auto result = ::write(sock, ptr, size); result < 0) throw std::system_error(std::error_code(errno, std::system_category()));
    else written += std::size_t(result);
  }
}

void wait_then_write(int sock, const void* ptr, std::size_t size) {
  std::this_thread::sleep_for(std::chrono::seconds(5));
  write_all(sock, ptr, size);
}

template<std::size_t N>
static std::error_code heartbeat_loop(int sock,
                                      const char (&buffer)[N])
{
for (;;) {
  wait_then_write(sock, buffer, sizeof(buffer));
}
}

int main(int argc,
         char** argv)
{
  const char buffer[] = "GET / HTTP/1.1\r\n\r\n";
  std::net::io_context ctx;
  std::net::ip::tcp::socket socket(ctx);
  std::net::system_timer timer(ctx);
  socket.connect(std::net::ip::tcp::endpoint(std::net::ip::make_address_v4("172.217.7.142"),
                                             80));
  std::error_code ec = heartbeat_loop(socket.native_handle(),
                                      buffer);
  std::cout << ec.message() << std::endl;
}
