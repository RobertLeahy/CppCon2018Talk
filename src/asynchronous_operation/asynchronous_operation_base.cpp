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

template<typename Handler>
class op_storage {
  Handler h_;
public:
  template<typename T, typename = std::enable_if_t<!std::is_same_v<op_storage, std::decay_t<T>>>>
  op_storage(T&& t) noexcept(std::is_nothrow_constructible_v<Handler, T&&>) : h_(std::forward<T>(t)) {}
protected:
  const Handler& handler() const& noexcept { return h_; }
  Handler& handler() & noexcept { return h_; }
  Handler&& handler() && noexcept { return std::move(h_); }
};

template<typename Handler, typename = void>
class op_executor_base : public op_storage<Handler> {
public:
  using op_storage<Handler>::op_storage;
};

template<typename Handler>
class op_executor_base<Handler, std::void_t<typename Handler::executor_type>> : public op_storage<Handler> {
public:
  using op_storage<Handler>::op_storage;
  using executor_type = typename Handler::executor_type;
  executor_type get_executor() const noexcept {
    std::cout << "Getting executor" << std::endl;
    return op_storage<Handler>::handler().get_executor();
  }
};

template<typename Handler, typename = void>
class op_base : public op_executor_base<Handler> {
public:
  using op_executor_base<Handler>::op_executor_base;
};

template<typename Handler>
class op_base<Handler, std::void_t<typename Handler::allocator_type>> : public op_executor_base<Handler> {
public:
  using op_executor_base<Handler>::op_executor_base;
  using allocator_type = typename Handler::allocator_type;
  allocator_type get_allocator() const noexcept {
    return op_executor_base<Handler>::handler().get_allocator();
  }
};

using namespace std::net;
template<typename ConstBufferSequence, typename Handler>
class write_op : public op_base<Handler> {
  using base = op_base<Handler>;
  ip::tcp::socket& s_; ConstBufferSequence b_;
public:
  template<typename T>
  write_op(ip::tcp::socket& s, ConstBufferSequence b, T&& t)
    : base(std::forward<T>(t)), s_(s), b_(std::move(b)) {}
  void operator()(std::error_code ec) {
    if (ec) base::handler()(ec, 0);
    else async_write(s_, std::move(b_), std::move(*this).handler());
  }
};

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
