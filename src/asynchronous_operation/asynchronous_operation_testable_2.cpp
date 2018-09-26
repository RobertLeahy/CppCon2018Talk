#include <cassert>
#include <chrono>
#include <cstring>
#include <experimental/net>
#include <functional>
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
template<typename AsyncWriteStream, typename ConstBufferSequence, typename Handler, typename WaitableTimer>
class write_op : public op_io_object_base<Handler, WaitableTimer> {
  using base = op_io_object_base<Handler, WaitableTimer>;
  AsyncWriteStream& s_; ConstBufferSequence b_;
public:
  template<typename T>
  write_op(AsyncWriteStream& s, ConstBufferSequence b, T&& t, WaitableTimer& timer)
    : base(std::forward<T>(t), timer), s_(s), b_(std::move(b)) {}
  void operator()(std::error_code ec) {
    if (ec) base::h_(ec, 0);
    else async_write(s_, std::move(b_), std::move(base::h_));
  }
};

template<typename WaitableTimer, typename AsyncWriteStream, typename ConstBufferSequence, typename Token>
decltype(auto) async_wait_then_write(WaitableTimer& timer, AsyncWriteStream& s,
  typename WaitableTimer::duration dur, ConstBufferSequence bufs, Token&& t)
{
  using result_type = async_result<std::decay_t<Token>, void(std::error_code, std::size_t)>;
  using handler_type = typename result_type::completion_handler_type;
  handler_type h(std::forward<Token>(t));
  result_type r(h);
  timer.expires_after(dur);
  write_op<AsyncWriteStream,
           ConstBufferSequence,
           handler_type,
           WaitableTimer> op(s, std::move(bufs), std::move(h), timer);
  timer.async_wait(std::move(op));
  return r.get();
}

class mock_async_write_stream {
  io_context::executor_type exec_;
  char*                     ptr_;
  std::size_t               size_;
public:
  mock_async_write_stream(const io_context::executor_type& exec, void* ptr, std::size_t size) noexcept : exec_(exec), ptr_ (static_cast<char*>(ptr)), size_(size) {}
  io_context::executor_type get_executor() noexcept {
    return exec_;
  }
  std::size_t remaining() const noexcept {
    return size_;
  }
  template<typename ConstBufferSequence, typename Token>
  decltype(auto) async_write_some(ConstBufferSequence cb, Token&& t) {
    using result_type = async_result<std::decay_t<Token>, void(std::error_code, std::size_t)>;
    using handler_type = typename result_type::completion_handler_type;
    handler_type h(std::forward<Token>(t));
    result_type r(h);
    std::error_code ec;
    std::size_t bytes = 0;
    if (buffer_size(cb)) {
      bytes = buffer_copy(buffer(ptr_, size_), std::move(cb));
      size_ -= bytes; ptr_ += bytes;
      if (!bytes) { ec = make_error_code(stream_errc::eof); }
    }
    auto exec = get_associated_executor(h, get_executor());
    post(bind_executor(exec, std::bind(std::move(h), ec, bytes)));
    return r.get();
  }
};

class mock_waitable_timer {
private:
  using function_type = std::function<void(std::error_code)>;
public:
  using duration = std::chrono::milliseconds;
  explicit mock_waitable_timer(const io_context::executor_type& exec) noexcept : exec_(exec) {}
  io_context::executor_type get_executor() noexcept {
    return exec_;
  }
  template<typename Token>
  decltype(auto) async_wait(Token&& t) {
    using result_type = async_result<std::decay_t<Token>, void(std::error_code)>;
    using handler_type = typename result_type::completion_handler_type;
    handler_type h(std::forward<Token>(t));
    result_type r(h);
    expire();
    func_ = std::move(h);
    return r.get();
  }
  void expires_after(duration d) {
    expire();
    d_ = d;
  }
  duration expires_after() const noexcept {
    return d_;
  }
  void expire() {
    if (!func_) {
      return;
    }
    function_type func;
    using std::swap;
    swap(func, func_);
    post(bind_executor(exec_, std::bind(std::move(func), std::error_code())));
  }
private:
  io_context::executor_type exec_;
  function_type             func_;
  duration                  d_;
};

int main(int argc,
         char** argv)
{
char out[1024], in[] = "GET / HTTP/1.1\r\n\r\n";
io_context ctx;
mock_waitable_timer timer(ctx.get_executor());
mock_async_write_stream stream(ctx.get_executor(), out, sizeof(out));
bool invoked = false;
std::error_code ec;
std::size_t bytes = 0;
async_wait_then_write(timer, stream, std::chrono::milliseconds(1000), buffer(in), [&] (auto e, auto b) { invoked = true; ec = e; bytes = b; });
assert(!invoked);
std::size_t handlers = ctx.poll();
assert(!handlers);
timer.expire();
ctx.restart();
handlers = ctx.poll();
assert(handlers);
assert(invoked);
assert(!ec);
assert(bytes == sizeof(in));
assert(stream.remaining() == (sizeof(out) - sizeof(in)));
assert(!std::memcmp(out, in, sizeof(in)));
std::cout << "Passed" << std::endl;
}
