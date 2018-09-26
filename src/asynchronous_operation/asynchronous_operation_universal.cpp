#include <chrono>
#include <experimental/net>
#include <future>
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

template<typename ConstBufferSequence, typename Token>
decltype(auto) async_wait_then_write(system_timer& timer, ip::tcp::socket& sock,
  system_timer::duration dur, ConstBufferSequence bufs, Token&& t)
{
  using result_type = async_result<std::decay_t<Token>, void(std::error_code, std::size_t)>;
  using handler_type = typename result_type::completion_handler_type;
  handler_type h(std::forward<Token>(t));
  result_type r(h);
  timer.expires_after(dur);
  write_op<ConstBufferSequence,
           handler_type> op(sock, std::move(bufs), std::move(h), timer);
  timer.async_wait(std::move(op));
  return r.get();
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
  auto f = async_wait_then_write(timer,
                                 socket,
                                 std::chrono::duration_cast<std::net::system_timer::duration>(std::chrono::seconds(5)),
                                 std::net::buffer(buffer),
                                 std::net::use_future);
  ctx.run();
  std::cout << "Wrote " << f.get() << " bytes" << std::endl;
}
