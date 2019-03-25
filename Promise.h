#include <functional>
#include <memory>
#include <mutex>
#include <type_traits>

namespace util {

namespace v2 {
namespace detail {

template <class... Ts>
class Promise;

template <typename T>
struct PromisedType {
  using type = void;
};

template <>
struct PromisedType<Promise<>> {
  using type = std::tuple<>;
};

template <typename T>
struct PromisedType<Promise<T>> {
  using type = T;
};

template <typename First, typename... Rest>
struct PromisedType<Promise<First, Rest...>> {
  using type = std::tuple<First, Rest...>;
};

template <typename T>
struct IsPromise {
  static constexpr bool value = false;
};

template <typename... Ts>
struct IsPromise<Promise<Ts...>> {
  static constexpr bool value = true;
};

template <typename T>
struct IsTuple {
  static constexpr bool value = false;
};

template <typename... Ts>
struct IsTuple<std::tuple<Ts...>> {
  static constexpr bool value = true;
};

template <int... T>
struct Sequence {
  template <class Callable, class Tuple>
  static void call(Callable&& callable, Tuple&& d) {
    callable(std::move(std::get<T>(d))...);
  }
};

template <int G>
struct SequenceGenerator {
  template <class Sequence>
  struct Extract;

  template <int... Ints>
  struct Extract<Sequence<Ints...>> {
    using type = Sequence<Ints..., G - 1>;
  };

  using type = typename Extract<typename SequenceGenerator<G - 1>::type>::type;
};

template <>
struct SequenceGenerator<0> {
  using type = Sequence<>;
};

template <class... Values>
struct CreateValue {
  static std::tuple<Values...> call(Values&&... args) { return std::make_tuple(std::move(args)...); }
};

template <class First>
struct CreateValue<First> {
  static First call(First&& r) { return r; }
};

template <class Element>
struct AppendElement;

template <class... Ts>
class Promise {
 public:
  Promise() : data_(std::make_shared<CommonData>()) {}

  template <typename Callable>
  using ReturnType = typename std::result_of<Callable(Ts...)>::type;

  template <class First, class Promise>
  struct Prepend;

  template <class First, class... Args>
  struct Prepend<First, Promise<Args...>> {
    using type = Promise<First, Args...>;
  };

  template <typename Filtered, typename Promise>
  struct Filter;

  template <typename Filtered>
  struct Filter<Filtered, Promise<>> {
    using type = Promise<>;
  };

  template <typename Filtered, typename First, typename... Rest>
  struct Filter<Filtered, Promise<First, Rest...>> {
    using rest = typename Filter<Filtered, Promise<Rest...>>::type;
    using type = typename std::conditional<std::is_same<Filtered, First>::value, rest,
                                           typename Prepend<First, rest>::type>::type;
  };

  template <class T>
  using ResolvedType = typename std::conditional<IsPromise<T>::value, typename PromisedType<T>::type, T>::type;

  template <class T>
  struct PromiseType;

  template <class... T>
  struct PromiseType<std::tuple<T...>> {
    using type = typename Filter<std::tuple<>, Promise<ResolvedType<T>...>>::type;
  };

  template <class T>
  struct ReturnedTuple;

  template <class... T>
  struct ReturnedTuple<Promise<T...>> {
    using type = std::tuple<T...>;
  };

  template <int Index, int ResultIndex, class T>
  struct EvaluateThen;

  template <int Index, int ResultIndex, class... T>
  struct EvaluateThen<Index, ResultIndex, std::tuple<T...>> {
    template <class PromiseType, class ResultTuple>
    static void call(std::tuple<T...>&& d, const PromiseType& p, const std::shared_ptr<ResultTuple>& result) {
      using CurrentType = typename std::tuple_element<Index, std::tuple<T...>>::type;
      auto element = std::move(std::get<Index>(d));
      auto continuation = [d = std::move(d), p, result]() mutable {
        EvaluateThen<Index - 1, ResultIndex - !std::is_same<CurrentType, std::tuple<>>::value, std::tuple<T...>>::call(
            std::move(d), p, result);
      };
      AppendElement<CurrentType>::template call<ResultIndex>(std::move(element), p, result, continuation);
    }
  };

  template <int ResultIndex, class... T>
  struct EvaluateThen<-1, ResultIndex, std::tuple<T...>> {
    template <class Q>
    struct Call;

    template <class... Q>
    struct Call<std::tuple<Q...>> {
      template <class Promise>
      static void call(std::tuple<T...>&& d, const Promise& p, const std::shared_ptr<std::tuple<Q...>>& result) {
        SequenceGenerator<std::tuple_size<std::tuple<Q...>>::value>::type::call(
            [p](Q&&... args) { p.fulfill(std::move(args)...); }, *result);
      }
    };

    template <class Promise, class ResultTuple>
    static auto call(std::tuple<T...>&& d, const Promise& p, const std::shared_ptr<ResultTuple>& result) {
      Call<ResultTuple>::call(std::move(d), p, result);
    }
  };

  template <typename Callable, typename Tuple = ReturnType<Callable>,
            typename ReturnedPromise = typename PromiseType<ReturnType<Callable>>::type,
            typename = typename std::enable_if<IsTuple<Tuple>::value>::type>
  ReturnedPromise then(Callable&& cb) {
    std::unique_lock<std::mutex> lock(data_->mutex_);
    ReturnedPromise promise;
    data_->on_fulfill_ = [promise, cb](Ts&&... args) mutable {
      try {
        using StateTuple = typename ReturnedTuple<ReturnedPromise>::type;
        auto r = cb(std::move(args)...);
        auto common_state = std::make_shared<StateTuple>();
        EvaluateThen<static_cast<int>(std::tuple_size<Tuple>::value) - 1,
                     static_cast<int>(std::tuple_size<StateTuple>::value) - 1, Tuple>::call(std::move(r), promise,
                                                                                            common_state);
      } catch (const std::exception&) {
        promise.reject(std::current_exception());
      }
    };
    data_->on_reject_ = [promise](std::exception_ptr&& e) { promise.reject(std::move(e)); };
    if (data_->error_ready_) {
      lock.unlock();
      data_->on_reject_(std::move(data_->exception_));
    } else if (data_->ready_) {
      lock.unlock();
      SequenceGenerator<std::tuple_size<std::tuple<Ts...>>::value>::type::call(data_->on_fulfill_, data_->value_);
    }
    return promise;
  }

  template <typename Callable, typename = typename std::enable_if<std::is_void<ReturnType<Callable>>::value>::type>
  Promise<> then(Callable&& cb) {
    return then([cb](Ts&&... args) {
      cb(std::move(args)...);
      return std::make_tuple();
    });
  }

  template <typename Callable,
            typename = typename std::enable_if<!IsTuple<ReturnType<Callable>>::value &&
                                               !std::is_void<ReturnType<Callable>>::value>::type,
            typename ReturnedPromise = typename PromiseType<std::tuple<ReturnType<Callable>>>::type>
  ReturnedPromise then(Callable&& cb) {
    return then([cb](Ts&&... args) mutable { return std::make_tuple(cb(std::move(args)...)); });
  }

  template <typename Exception, typename Callable>
  Promise<Ts...> error(Callable&& e) {
    std::unique_lock<std::mutex> lock(data_->mutex_);
    Promise<Ts...> promise;
    data_->on_reject_ = [promise, cb = std::move(e)](std::exception_ptr&& e) {
      try {
        std::rethrow_exception(std::move(e));
      } catch (Exception& exception) {
        cb(std::move(exception));
      } catch (const std::exception&) {
        promise.reject(std::current_exception());
      }
    };
    data_->on_fulfill_ = [promise](Ts&&... args) { promise.fulfill(std::move(args)...); };
    if (data_->error_ready_) {
      lock.unlock();
      data_->on_reject_(std::move(data_->exception_));
    }
    return promise;
  }

  template <typename Callable>
  Promise<Ts...> error_ptr(Callable&& e) {
    std::unique_lock<std::mutex> lock(data_->mutex_);
    Promise<Ts...> promise;
    data_->on_reject_ = e;
    data_->on_fulfill_ = [promise](Ts&&... args) { promise.fulfill(std::move(args)...); };
    if (data_->error_ready_) {
      lock.unlock();
      data_->on_reject_(std::move(data_->exception_));
    }
    return promise;
  }

  void fulfill(Ts&&... value) const {
    std::unique_lock<std::mutex> lock(data_->mutex_);
    data_->ready_ = true;
    if (data_->on_fulfill_) {
      lock.unlock();
      data_->on_fulfill_(std::move(value)...);
      data_->on_fulfill_ = nullptr;
    } else {
      data_->value_ = std::make_tuple(std::move(value)...);
    }
  }

  template <class Exception, typename = typename std::enable_if<std::is_base_of<
                                 std::exception, typename std::remove_reference<Exception>::type>::value>::type>
  void reject(Exception&& e) const {
    reject(std::make_exception_ptr(std::move(e)));
  }

  void reject(std::exception_ptr&& e) const {
    std::unique_lock<std::mutex> lock(data_->mutex_);
    data_->error_ready_ = true;
    if (data_->on_reject_) {
      lock.unlock();
      data_->on_reject_(std::move(e));
      data_->on_reject_ = nullptr;
    } else {
      data_->exception_ = std::move(e);
    }
  }

 private:
  template <class... T>
  friend class Promise;

  struct CommonData {
    std::mutex mutex_;
    bool ready_ = false;
    bool error_ready_ = false;
    std::function<void(Ts&&...)> on_fulfill_;
    std::function<void(std::exception_ptr&&)> on_reject_;
    std::tuple<Ts...> value_;
    std::exception_ptr exception_;
  };
  std::shared_ptr<CommonData> data_;
};

template <class Element>
struct AppendElement {
  template <int Index, class PromiseType, class ResultTuple, class Callable>
  static void call(Element&& result, const PromiseType& p, const std::shared_ptr<ResultTuple>& output,
                   Callable&& callable) {
    std::get<Index>(*output) = std::move(result);
    callable();
  }
};

template <class... PromisedType>
struct AppendElement<Promise<PromisedType...>> {
  template <int Index, class PromiseType, class ResultTuple, class Callable>
  static void call(Promise<PromisedType...>&& d, const PromiseType& p, const std::shared_ptr<ResultTuple>& output,
                   Callable&& callable) {
    d.then([output, callable = std::move(callable)](PromisedType&&... args) mutable {
       std::get<Index>(*output) = CreateValue<PromisedType...>::call(std::move(args)...);
       callable();
       return std::make_tuple();
     })
        .error_ptr([p](std::exception_ptr&& e) { p.reject(std::move(e)); });
  }
};

template <>
struct AppendElement<Promise<>> {
  template <int Index, class PromiseType, class ResultTuple, class Callable>
  static void call(Promise<>&& d, const PromiseType& p, const std::shared_ptr<ResultTuple>& output,
                   Callable&& callable) {
    d.then([output, callable = std::move(callable)]() mutable {
       callable();
       return std::make_tuple();
     })
        .error_ptr([p](std::exception_ptr&& e) { p.reject(std::move(e)); });
  }
};

}  // namespace detail
}  // namespace v2

using v2::detail::Promise;
}  // namespace util