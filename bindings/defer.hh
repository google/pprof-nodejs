/*
 * Copyright 2023 Datadog, Inc
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <utility>

namespace details {

struct DeferDummy {};

template <typename F>
class DeferHolder {
 public:
  DeferHolder(DeferHolder&&) = default;
  DeferHolder(const DeferHolder&) = delete;
  DeferHolder& operator=(DeferHolder&&) = delete;
  DeferHolder& operator=(const DeferHolder&) = delete;

  template <typename T>
  explicit DeferHolder(T&& f) : _func(std::forward<T>(f)) {}

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

template <class F>
DeferHolder<F> operator*(DeferDummy, F&& f) {
  return DeferHolder<F>{std::forward<F>(f)};
}

}  // namespace details

template <class F>
details::DeferHolder<F> make_defer(F&& f) {
  return details::DeferHolder<F>{std::forward<F>(f)};
}

#define DEFER_(LINE) zz_defer##LINE
#define DEFER(LINE) DEFER_(LINE)
#define defer                                                                  \
  [[gnu::unused]] const auto& DEFER(__COUNTER__) = details::DeferDummy{}* [&]()
