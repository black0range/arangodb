////////////////////////////////////////////////////////////////////////////////
/// DISCLAIMER
///
/// Copyright 2016 by EMC Corporation, All Rights Reserved
///
/// Licensed under the Apache License, Version 2.0 (the "License");
/// you may not use this file except in compliance with the License.
/// You may obtain a copy of the License at
///
///     http://www.apache.org/licenses/LICENSE-2.0
///
/// Unless required by applicable law or agreed to in writing, software
/// distributed under the License is distributed on an "AS IS" BASIS,
/// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
/// See the License for the specific language governing permissions and
/// limitations under the License.
///
/// Copyright holder is EMC Corporation
///
/// @author Andrey Abramov
/// @author Vasiliy Nabatchikov
////////////////////////////////////////////////////////////////////////////////

#ifndef IRESEARCH_BITSET_H
#define IRESEARCH_BITSET_H

#include <cstring>
#include <memory>

#include "shared.hpp"
#include "bit_utils.hpp"
#include "math_utils.hpp"
#include "noncopyable.hpp"
#include "memory.hpp"

NS_ROOT

template<typename Alloc>
class dynamic_bitset_base : irs::compact<0, Alloc>, util::noncopyable {
 public:
  typedef size_t word_t;
  typedef typename std::allocator_traits<Alloc>::template rebind_alloc<word_t> allocator_type;

 protected:
  typedef memory::allocator_array_deallocator<allocator_type> word_ptr_deleter_t;
  typedef std::unique_ptr<word_t[], word_ptr_deleter_t> word_ptr_t;

  dynamic_bitset_base(const Alloc& alloc = Alloc())
    : compact<0, allocator_type>(alloc) {
  }

  allocator_type& allocator() NOEXCEPT {
    return compact<0, allocator_type>::get();
  }
}; // bitset_base

template<typename Alloc>
class dynamic_bitset : public dynamic_bitset_base<Alloc> {
 public:
  typedef dynamic_bitset_base<Alloc> base_t;

  using typename base_t::word_ptr_t;
  using typename base_t::word_ptr_deleter_t;
  using typename base_t::word_t;

  typedef size_t index_t;

  // returns corresponding bit index within a word for the
  // specified offset in bits
  CONSTEXPR FORCE_INLINE static size_t bit(size_t i) NOEXCEPT {
    return i % bits_required<word_t>();
  }

  // returns corresponding word index specified offset in bits
  CONSTEXPR FORCE_INLINE static size_t word(size_t i) NOEXCEPT {
    return i / bits_required<word_t>();
  }

  // returns corresponding offset in bits for the specified word index
  CONSTEXPR FORCE_INLINE static size_t bit_offset(size_t i) NOEXCEPT {
    return i * bits_required<word_t>();
  }

  dynamic_bitset(const Alloc& alloc = Alloc())
    : base_t(alloc),
      data_(nullptr, word_ptr_deleter_t(this->allocator(), 0)) {
  }

  explicit dynamic_bitset(size_t bits, const Alloc& alloc = Alloc())
    : dynamic_bitset(alloc) {
    reset(bits);
  }

  dynamic_bitset(dynamic_bitset&& rhs) NOEXCEPT
    : bits_(rhs.bits_),
      words_(rhs.words_),
      data_(std::move(rhs.data_)) {
    rhs.bits_ = 0;
    rhs.words_ = 0;
  }

  dynamic_bitset& operator=(dynamic_bitset&& rhs) NOEXCEPT {
    if (this != &rhs) {
      bits_ = rhs.bits_;
      words_ = rhs.words_;
      data_ = std::move(rhs.data_);
      rhs.bits_ = 0;
      rhs.words_ = 0;
    }

    return *this;
  }

  void reset(size_t bits) {
    const auto words = bit_to_words(bits);

    if (words > words_) {
      data_ = memory::allocate_unique<word_t[]>(
        this->allocator(), words, memory::allocate_only
      );
    }

    words_ = words;
    bits_ = bits;
    clear();
  }

  bool operator==(const dynamic_bitset& rhs) const NOEXCEPT {
    if (this->size() != rhs.size()) {
      return false;
    }

    return 0 == std::memcmp(this->begin(), rhs.begin(), this->size());
  }

  bool operator!=(const dynamic_bitset& rhs) const NOEXCEPT {
    return !(*this == rhs);
  }

  // number of bits in bitset
  size_t size() const NOEXCEPT { return bits_; }

  // capacity in bits
  size_t capacity() const NOEXCEPT {
    return bits_required<word_t>()*words_;
  }

  size_t words() const NOEXCEPT { return words_; }

  const word_t* data() const NOEXCEPT { return data_.get(); }

  const word_t* begin() const NOEXCEPT { return data(); }
  const word_t* end() const NOEXCEPT { return data() + words_; }

  template<typename T>
  void memset(const T& value) NOEXCEPT {
    memset(&value, sizeof(value));
  }

  void memset(const void* src, size_t size) NOEXCEPT {
    std::memcpy(data_.get(), src, std::min(size, words()*sizeof(word_t)));
    sanitize();
  }

  void set(size_t i) NOEXCEPT {
    set_bit(data_[word(i)], bit(i));
  }

  void unset(size_t i) NOEXCEPT {
    unset_bit(data_[word(i)], bit(i));
  }

  void reset(size_t i, bool set) NOEXCEPT {
    set_bit(data_[word(i)], bit(i), set);
  }

  bool test(size_t i) const NOEXCEPT {
    return check_bit(data_[word(i)], bit(i));
  }

  bool any() const NOEXCEPT {
    return std::any_of(
      begin(), end(),
      [] (word_t w) { return w != 0; }
    );
  }

  bool none() const NOEXCEPT {
    return !any();
  }

  bool all() const NOEXCEPT {
    return (count() == size());
  }

  void clear() NOEXCEPT {
    if (data_) {
      // passing nullptr to `std::memset` is undefined behavior
      std::memset(data_.get(), 0, sizeof(word_t)*words_);
    }
  }

  // counts bits set
  word_t count() const NOEXCEPT {
    return std::accumulate(
      begin(), end(), word_t(0),
      [] (word_t v, word_t w) {
        return v + math::math_traits<word_t>::pop(w);
    });
  }

 private:
  FORCE_INLINE static size_t bit_to_words(size_t bits) NOEXCEPT {
    static const size_t EXTRA[] { 1, 0 };

    return bits / bits_required<word_t>()
        + EXTRA[0 == (bits % bits_required<word_t>())];
  }

  void sanitize() NOEXCEPT {
    assert(bits_ <= capacity());
    auto last_word_bits = bits_ % bits_required<word_t>();

    if (!last_word_bits) {
      return; // no words or last word has all bits set
    }

    const auto mask = ~(~word_t(0) << (bits_ % bits_required<word_t>()));

    data_[words_ - 1] &= mask;
  }

  size_t bits_{};   // number of bits in a bitset
  size_t words_{};  // number of words used for storing data
  word_ptr_t data_; // words array
}; // dynamic_bitset

typedef dynamic_bitset<std::allocator<size_t>> bitset;

NS_END

#endif
