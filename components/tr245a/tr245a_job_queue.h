#pragma once

#include <array>
#include <cstddef>

namespace esphome {
namespace tr245a {

template<typename Job, size_t Capacity> class TR245AJobQueue {
  static_assert(Capacity > 0, "TR245A job queue capacity must be positive");

 public:
  class const_iterator {
   public:
    const Job &operator*() const { return queue_->at_offset_(offset_); }
    const_iterator &operator++() {
      ++offset_;
      return *this;
    }
    bool operator!=(const const_iterator &other) const {
      return queue_ != other.queue_ || offset_ != other.offset_;
    }

   private:
    friend class TR245AJobQueue;
    const_iterator(const TR245AJobQueue *queue, size_t offset) : queue_(queue), offset_(offset) {}
    const TR245AJobQueue *queue_;
    size_t offset_;
  };

  size_t size() const { return count_; }
  bool empty() const { return count_ == 0; }
  const Job &front() const { return jobs_[read_index_]; }

  void pop() {
    if (empty()) return;
    read_index_ = increment_(read_index_);
    --count_;
  }

  void clear() {
    read_index_ = 0;
    write_index_ = 0;
    count_ = 0;
  }

  bool push_batch(const Job *jobs, size_t count) {
    if (count > Capacity - count_) return false;
    for (size_t i = 0; i < count; ++i) {
      jobs_[write_index_] = jobs[i];
      write_index_ = increment_(write_index_);
    }
    count_ += count;
    return true;
  }

  const_iterator begin() const { return const_iterator(this, 0); }
  const_iterator end() const { return const_iterator(this, count_); }

 private:
  static size_t increment_(size_t index) { return index + 1 == Capacity ? 0 : index + 1; }
  const Job &at_offset_(size_t offset) const {
    const size_t index = read_index_ + offset;
    return jobs_[index < Capacity ? index : index - Capacity];
  }

  std::array<Job, Capacity> jobs_{};
  size_t read_index_{0};
  size_t write_index_{0};
  size_t count_{0};
};

}  // namespace tr245a
}  // namespace esphome
