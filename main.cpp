#include "Promise.h"

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
      .then([&result](const std::string& str) {
        std::cerr << "value set\n";
        throw std::system_error();
        result.set_value();
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
      .then([](const std::tuple<>&) { return 5; })
      .then([](int a) {
        std::cerr << a << "\n";
        return send_void(1);
      })
      .then([](const std::tuple<>&) { std::cerr << "ok\n"; });
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
      .error<std::exception>([](const auto& e) { std::cerr << "received exception\n"; });
}

int main() {
  std::promise<void> t1, t2;
  test1(t1);
  test2(t2);
  t1.get_future().get();
  t2.get_future().get();
  return 0;
}
