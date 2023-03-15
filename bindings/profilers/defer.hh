#pragma once

#include <utility>

namespace details {

struct DeferDummy {};

template <typename F> class DeferHolder {
public:
  DeferHolder(DeferHolder &&) = default;
  DeferHolder(const DeferHolder &) = delete;
  DeferHolder &operator=(DeferHolder &&) = delete;
  DeferHolder &operator=(const DeferHolder &) = delete;

  template <typename T>
  explicit DeferHolder(T &&f) : _func(std::forward<T>(f)) {}

  ~DeferHolder() { reset(); }

  void reset() {
    if (_active) {
      _func();
      _active = false;
    }
  }

  void release() { _active = false; }

private:
  F _func;
  bool _active = true;
};

template <class F> DeferHolder<F> operator*(DeferDummy, F &&f) {
  return DeferHolder<F>{std::forward<F>(f)};
}

} // namespace details

template <class F> details::DeferHolder<F> make_defer(F &&f) {
  return details::DeferHolder<F>{std::forward<F>(f)};
}

#define DEFER_(LINE) zz_defer##LINE
#define DEFER(LINE) DEFER_(LINE)
#define defer                                                                  \
  [[gnu::unused]] const auto &DEFER(__COUNTER__) = details::DeferDummy{} *[&]()
