#include "Promise.h"

#include <string>
#include <iostream>

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

Promise<void> send_void(int seconds) {
  Promise<void> result;
  std::thread([=] {
    std::this_thread::sleep_for(std::chrono::seconds(seconds));
    result.fulfill();
  })
      .detach();
  return result;
}

int main() {
  std::promise<int> result;
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
      .then([] { std::cerr << "ok\n"; });
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
        result.set_value(0);
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
      .error(
          [](const std::exception& e) { std::cerr << "received exception\n"; });

  return result.get_future().get();
}
