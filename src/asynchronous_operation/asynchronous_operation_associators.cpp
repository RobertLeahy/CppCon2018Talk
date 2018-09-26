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
class write_op {
  ip::tcp::socket& s_; ConstBufferSequence b_; Handler h_;
public:
  template<typename T>
  write_op(ip::tcp::socket& s, ConstBufferSequence b, T&& t)
    : s_(s), b_(std::move(b)), h_(std::forward<T>(t)) {}
  const Handler& handler() const noexcept { return h_; }
  void operator()(std::error_code ec) {
    if (ec) h_(ec, 0);
    else async_write(s_, std::move(b_), std::move(h_));
  }
};

namespace std::experimental::net {

template<typename C, typename H, typename Executor>
struct associated_executor<write_op<C, H>, Executor>
  : public associated_executor<H, Executor>
{
  static auto get(const write_op<C, H>& op,
    const Executor& e = Executor()) noexcept
  {
    std::cout << "Getting associated executor" << std::endl;
    return associated_executor<H, Executor>::get(op.handler(), e);
  }
};

template<typename ConstBufferSequence,
         typename Handler,
         typename ProtoAllocator>
struct associated_allocator<write_op<ConstBufferSequence,
                                     Handler>,
                            ProtoAllocator> : public associated_allocator<Handler,
                                                                          ProtoAllocator>
{
public:
  static auto get(const write_op<ConstBufferSequence,
                                 Handler>& op,
                  const ProtoAllocator& a = ProtoAllocator()) noexcept
  {
    return associated_allocator<Handler,
                                ProtoAllocator>::get(op.handler(),
                                                     a);
  }
};

}

using namespace std::net;
template<typename ConstBufferSequence, typename Handler>
void async_wait_then_write(system_timer& timer, ip::tcp::socket& sock,
  system_timer::duration dur, ConstBufferSequence bufs, Handler&& h)
{
  timer.expires_after(dur);
  write_op<ConstBufferSequence,
           Handler> op(sock, std::move(bufs), std::forward<Handler>(h));
  timer.async_wait(std::move(op));
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
                                               std::net::bind_executor(std::net::strand<std::net::io_context::executor_type>(ctx.get_executor()), [](std::error_code ec,
                                                  std::size_t bytes)
                                               {
                                                 if (ec) {
                                                   std::cout << ec.message() << std::endl;
                                                   return;
                                                 }
                                                 std::cout << "Wrote " << bytes << " bytes" << std::endl;
                                               }));
                       });
  ctx.run();
}
