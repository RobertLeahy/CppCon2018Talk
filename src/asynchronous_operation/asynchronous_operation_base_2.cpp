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

template<typename Handler, typename Executor> class op_base {
  Executor ex_;
public:
  template<typename T> op_base(T&& t, const Executor& ex)
    noexcept(std::is_nothrow_constructible_v<Handler, T&&>) : ex_(ex), h_(std::forward<T>(t)) {}
  using executor_type = associated_executor_t<Handler, Executor>;
  executor_type get_executor() const noexcept {
    return get_associated_executor(h_, ex_); }
  using allocator_type = associated_allocator_t<Handler>;
  allocator_type get_allocator() const noexcept {
    return get_associated_allocator(h_); }
protected:
  Handler h_;
};

template<typename IoObject> using io_object_executor_t =
  decltype(std::declval<IoObject>().get_executor());

template<typename Handler, typename IoObject>
class op_io_object_base : public op_base<Handler,
  io_object_executor_t<IoObject>> {
public:
  template<typename T>
  op_io_object_base(T&& t, IoObject& io_object) noexcept(std::is_nothrow_constructible_v<Handler, T&&>)
    : op_base<Handler, io_object_executor_t<IoObject>>
        (std::forward<T>(t), io_object.get_executor())
  {}
};

using namespace std::net;
template<typename ConstBufferSequence, typename Handler>
class write_op : public op_io_object_base<Handler, system_timer> {
  using base = op_io_object_base<Handler, system_timer>;
  ip::tcp::socket& s_; ConstBufferSequence b_;
public:
  template<typename T>
  write_op(ip::tcp::socket& s, ConstBufferSequence b, T&& t, system_timer& timer)
    : base(std::forward<T>(t), timer), s_(s), b_(std::move(b)) {}
  void operator()(std::error_code ec) {
    if (ec) base::h_(ec, 0);
    else async_write(s_, std::move(b_), std::move(base::h_));
  }
};

template<typename ConstBufferSequence, typename Handler>
void async_wait_then_write(system_timer& timer, ip::tcp::socket& sock,
  system_timer::duration dur, ConstBufferSequence bufs, Handler&& h)
{
  timer.expires_after(dur);
  write_op<ConstBufferSequence,
           Handler> op(sock, std::move(bufs), std::forward<Handler>(h), timer);
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
