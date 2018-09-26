#include <chrono>
#include <cstddef>
#include <cstring>
#include <experimental/net>
#include <iostream>
#include <system_error>
#include <thread>
#include <type_traits>
#include <utility>

namespace std::net {

using namespace experimental::net;

}

using namespace std;
using namespace std::chrono;
using namespace std::net;

template<typename WaitableTimer, typename AsyncWriteStream, typename ConstBufferSequence, typename Handler>
struct write_op {
  WaitableTimer& timer_;
  AsyncWriteStream& sock_;
  ConstBufferSequence cb_;
  Handler h_;
  void operator()(std::error_code ec) { /* ... */
    if (ec) h_(ec, 0);
    else async_write(sock_, std::move(cb_), std::move(h_));
  }
  using executor_type = associated_executor_t<Handler, decltype(std::declval<WaitableTimer>().get_executor())>;
  executor_type get_executor() const noexcept {
    std::cout << "Get executor" << std::endl;
    return get_associated_executor(h_, timer_.get_executor());
  }
};

template<typename WaitableTimer, typename AsyncWriteStream, typename ConstBufferSequence, typename Handler>
void async_wait_then_write(WaitableTimer& timer, AsyncWriteStream& s,
  typename WaitableTimer::duration dur, ConstBufferSequence bufs, Handler&& h)
{
  timer.expires_after(dur);
  write_op<WaitableTimer, AsyncWriteStream, ConstBufferSequence, std::decay_t<Handler>> op{timer, s, std::move(bufs), std::forward<Handler>(h)};
  timer.async_wait(std::move(op));
}

template<typename Executor>
struct heartbeat {
  using executor_type = Executor;
  executor_type get_executor() const noexcept { return ex_; }
  Executor ex_;
  // ...
  system_timer& timer_;
  ip::tcp::socket& socket_;
  const byte* buffer_;
  size_t size_;
  void operator()(error_code ec = error_code(), size_t written = 0) {
    if (ec) throw system_error(ec);
    async_wait_then_write(timer_, socket_, seconds(5), buffer(buffer_, size_), *this);
  }
};

template<typename Executor>
struct process {
  using executor_type = Executor;
  executor_type get_executor() const noexcept { return ex_; }
  Executor ex_;
  // ...
  ip::tcp::socket& socket_;
  std::byte* buffer_;
  std::size_t size_;
  void operator()(std::error_code ec = {}, std::size_t bytes = 0) {
    if (ec) throw std::system_error(ec);
    std::cout << "Read " << bytes << " bytes" << std::endl;
    //  Process bytes received...
    socket_.async_read_some(buffer(buffer_, size_), *this);
  }
};

int main(int argc,
         char** argv)
{
const char str[] = "GET / HTTP/1.1\r\n\r\n";
std::byte write_buffer[sizeof(str) - 1U];
std::byte read_buffer[1024];
std::memcpy(write_buffer,
            str,
            sizeof(write_buffer));
std::net::io_context ctx;
using strand_type = std::net::strand<std::net::io_context::executor_type>;
strand_type strand(ctx.get_executor());
std::net::ip::tcp::socket socket(ctx);
std::net::system_timer timer(ctx);
socket.connect(std::net::ip::tcp::endpoint(std::net::ip::make_address_v4("172.217.7.142"),
                                           80));
// ...
heartbeat<strand_type>{strand, timer, socket, write_buffer, sizeof(write_buffer)}();
process<strand_type>{strand, socket, read_buffer, sizeof(read_buffer)}();
std::thread t([&] () { ctx.run(); });
ctx.run();
t.join();
}
