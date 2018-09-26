#include <chrono>
#include <experimental/net>
#include <iostream>
#include <system_error>
#include <type_traits>
#include <utility>

namespace std::net {

using namespace experimental::net;

}

using namespace std::net;
template<typename ConstBufferSequence, typename Handler>
void async_wait_then_write(system_timer& timer, ip::tcp::socket& sock,
  system_timer::duration dur, ConstBufferSequence bufs, Handler&& h)
{
  timer.expires_after(dur);
  auto f = [&sock, bufs = std::move(bufs), h = std::forward<Handler>(h)]
    (std::error_code ec) mutable
  {
    if (ec) h(ec, 0);
    else std::net::async_write(sock, std::move(bufs), std::move(h));
  };
  timer.async_wait(std::move(f));
}

int main(int argc,
         char** argv)
{
  const char buffer[] = "GET / HTTP/1.1\r\n\r\n";
  std::net::io_context ctx;
  std::net::ip::tcp::socket socket(ctx);
  std::net::system_timer timer(ctx);
  socket.async_connect(std::net::ip::tcp::endpoint(std::net::ip::make_address_v4("172.217.7.142"),
                                                   80),
                       [&](std::error_code ec) {
                         if (ec) {
                           std::cout << ec.message() << std::endl;
                           return;
                         }
                         async_wait_then_write(timer,
                                               socket,
                                               std::chrono::duration_cast<std::net::system_timer::duration>(std::chrono::seconds(5)),
                                               std::net::buffer(buffer),
                                               [](std::error_code ec,
                                                  std::size_t bytes)
                                               {
                                                 if (ec) {
                                                   std::cout << ec.message() << std::endl;
                                                   return;
                                                 }
                                                 std::cout << "Wrote " << bytes << " bytes" << std::endl;
                                               });
                       });
  ctx.run();
}
