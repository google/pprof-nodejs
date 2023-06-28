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

namespace dd {
template <class T>
class RingBuffer {
 public:
  explicit RingBuffer(size_t capacity)
      : buffer(std::make_unique<T[]>(capacity)),
        capacity_(capacity),
        size_(0),
        back_index_(0),
        front_index_(0) {}

  size_t capacity() const { return capacity_; }
  bool full() const { return size_ == capacity_; }
  bool empty() const { return size_ == 0; }
  size_t size() const { return size_; }

  T& front() { return buffer[front_index_]; }
  const T& front() const { return buffer[front_index_]; }

  void push_back(const T& t) { push_back_(t); }
  void push_back(T&& t) { push_back_(std::move(t)); }

  void clear() {
    while (!empty()) {
      pop_front();
    }
  }

  T pop_front() {
    auto idx = front_index_;
    increment(front_index_);
    --size_;
    return std::move(buffer[idx]);
  }

 private:
  template <typename U>
  void push_back_(U&& t) {
    const bool is_full = full();

    if (is_full && empty()) {
      return;
    }
    buffer[back_index_] = std::forward<U>(t);
    increment(back_index_);

    if (is_full) {
      // move buffer head
      front_index_ = back_index_;
    } else {
      ++size_;
    }
  }

  void increment(size_t& idx) const {
    idx = idx + 1 == capacity_ ? 0 : idx + 1;
  }
  void decrement(size_t& idx) const {
    idx = idx == 0 ? capacity_ - 1 : idx - 1;
  }

  std::unique_ptr<T[]> buffer;
  size_t capacity_;
  size_t size_;
  size_t back_index_;
  size_t front_index_;
};
}  // namespace dd
