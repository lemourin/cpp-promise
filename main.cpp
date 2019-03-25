#include "Promise.h"

#include <future>
#include <iostream>
#include <string>

using util::Promise;

Promise<std::string> send(int seconds) {
  Promise<std::string> result;
  std::thread([=] {
    std::this_thread::sleep_for(std::chrono::seconds(seconds));
    result.fulfill(std::string("dupa"));
  })
      .detach();
  return result;
}

Promise<> send_void(int seconds) {
  Promise<> result;
  std::thread([=] {
    std::this_thread::sleep_for(std::chrono::seconds(seconds));
    result.fulfill();
  })
      .detach();
  return result;
}

Promise<std::unique_ptr<std::string>> send_movable(int seconds) {
  Promise<std::unique_ptr<std::string>> result;
  std::thread([=] {
    std::this_thread::sleep_for(std::chrono::seconds(seconds));
    result.fulfill(std::make_unique<std::string>("string"));
  })
      .detach();
  return result;
}

Promise<int, std::unique_ptr<std::string>> send_two(int seconds) {
  Promise<int, std::unique_ptr<std::string>> result;
  std::thread([=] {
    std::this_thread::sleep_for(std::chrono::seconds(seconds));
    result.fulfill(1, std::make_unique<std::string>("test"));
  })
      .detach();
  return result;
}

void test1(std::promise<void>& result) {
  send(1)
      .then([](const std::string& result) {
        std::cerr << "first try\n";
        return std::make_tuple(send(1), send(2), send(2), 2);
      })
      .then([](const std::string& t1, const std::string& t2, const std::string& t3, int t4) {
        std::cerr << "args: " << t1 << " " << t2 << " " << t3 << " " << t4 << "\n";
        return send(1);
      })
      .then([](const std::string& str) {
        std::cerr << "value set\n";
        return send_movable(1);
      })
      .then([&result](std::unique_ptr<std::string>&& e) {
        std::cerr << *e << "\n";
        throw std::system_error(std::error_code());
      })
      .error<std::logic_error>([](const auto& e) { std::cerr << "logic error occurred\n"; })
      .error<std::system_error>([&result](const auto& e) {
        std::cerr << "system error occurred\n";
        result.set_value();
      })
      .error<std::exception>([](const auto& e) { std::cerr << "generic error occurred\n"; });
}

void test2(std::promise<void>& result) {
  send(2)
      .then([](const std::string& d) {
        std::cerr << d << "\n";
        return 5;
      })
      .then([](int d) { std::cerr << d << "\n"; })
      .then([] { return send_void(1); })
      .then([] { return 5; })
      .then([](int a) {
        std::cerr << a << "\n";
        return send_void(1);
      })
      .then([]() { std::cerr << "ok\n"; });
  send(2)
      .then([=](const std::string& d) {
        std::cerr << "got through first\n";
        return send(2);
      })
      .then([](const std::string& d) {
        std::cerr << "2nd " << d << "\n";
        return send(1);
      })
      .then([](const std::string& d) {
        std::cerr << "3rd" << d << "\n";
        return 3;
      })
      .then([](int sleep) {
        std::cerr << "sleeping " << sleep << "\n";
        return send(sleep);
      })
      .then([&](const std::string& d) {
        std::cerr << "got " << d << "\n";
        result.set_value();
      });
  send(1)
      .then([](const std::string&) {
        std::cerr << "sending\n";
        return send(3);
      })
      .then([](const std::string&) {
        std::cerr << "test\n";
        throw std::logic_error("test");
        return 5;
      })
      .then([](int) { std::cerr << "intint\n"; })
      .error<std::logic_error>([](const auto& e) { std::cerr << "received exception\n"; });
}

void test3(std::promise<void>& result) {
  send(1)
      .then([](const std::string& d) { return std::make_tuple(send_two(1), send_void(1), 1); })
      .then([](int, std::unique_ptr<std::string>&& d, int) { std::cerr << "multipromise string " << *d << "\n"; })
      .then([] { throw std::logic_error("test"); })
      .error<std::system_error>([](const auto&) { std::terminate(); })
      .error<std::logic_error>([&result](const auto&) { result.set_value(); });
}

int main() {
  std::promise<void> t1, t2, t3;
  test1(t1);
  test2(t2);
  test3(t3);
  t1.get_future().get();
  t2.get_future().get();
  t3.get_future().get();
  return 0;
}
