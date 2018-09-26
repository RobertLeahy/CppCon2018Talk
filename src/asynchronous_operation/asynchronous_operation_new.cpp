#include <chrono>
#include <cstddef>
#include <cstring>
#include <experimental/net>
#include <iostream>
#include <system_error>
#include <type_traits>
#include <utility>

namespace std::net {

using namespace experimental::net;

}

using namespace std;
using namespace std::chrono;
using namespace std::net;

struct heartbeat {
  system_timer& timer_;
  ip::tcp::socket& socket_;
  const byte* buffer_;
  size_t size_;
  size_t written_ = size_;
  void write() {
    socket_.async_write_some(buffer(buffer_ + written_, size_ - written_), *this);
  }
  void operator()(error_code ec) {
    if (ec) throw system_error(ec);
    write();
  }
  void operator()(error_code ec, size_t bytes) {
    if (ec) throw system_error(ec);
    written_ += bytes;
    if (written_ != size_) {
      write();
      return;
    }
    written_ = 0;
    timer_.expires_after(seconds(5));
    timer_.async_wait(*this);
  }
};

int main(int argc,
         char** argv)
{
  const char str[] = "GET / HTTP/1.1\r\n\r\n";
  std::byte buffer[sizeof(str) - 1U];
  std::memcpy(buffer,
              str,
              sizeof(buffer));
  std::net::io_context ctx;
  std::net::ip::tcp::socket socket(ctx);
  std::net::system_timer timer(ctx);
  socket.connect(std::net::ip::tcp::endpoint(std::net::ip::make_address_v4("172.217.7.142"),
                                             80));
// ...
heartbeat{timer, socket, buffer, sizeof(buffer)}(std::error_code(), 0);
ctx.run();
}
