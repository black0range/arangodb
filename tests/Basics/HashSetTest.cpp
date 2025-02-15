////////////////////////////////////////////////////////////////////////////////
/// @brief test suite for HashSet class
///
/// @file
///
/// DISCLAIMER
///
/// Copyright 2004-2012 triagens GmbH, Cologne, Germany
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
/// Copyright holder is triAGENS GmbH, Cologne, Germany
///
/// @author Jan Steemann
/// @author Copyright 2007-2012, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#include "Basics/Common.h"
#include "Basics/HashSet.h"

#include "gtest/gtest.h"

/// @brief test size
TEST(HashSetTest, test_size) {
  arangodb::HashSet<size_t> values;

  EXPECT_TRUE(values.size() == 0);
  EXPECT_TRUE(values.empty());

  for (size_t i = 0; i < 1000; ++i) {
    EXPECT_TRUE(values.size() == i);
    values.insert(i);
    EXPECT_TRUE(values.size() == i + 1);
    EXPECT_TRUE(!values.empty());
  }

  // insert same values again
  for (size_t i = 0; i < 1000; ++i) {
    EXPECT_TRUE(values.size() == 1000);
    values.insert(i);
    EXPECT_TRUE(values.size() == 1000);
    EXPECT_TRUE(!values.empty());
  }

  for (size_t i = 0; i < 1000; ++i) {
    EXPECT_TRUE(values.size() == 1000 - i);
    EXPECT_TRUE(!values.empty());
    values.erase(i);
    EXPECT_TRUE(values.size() == 999 - i);
  }

  EXPECT_TRUE(values.size() == 0);
  EXPECT_TRUE(values.empty());

  for (size_t i = 0; i < 1000; ++i) {
    EXPECT_TRUE(values.size() == i);
    values.insert(i);
    EXPECT_TRUE(values.size() == i + 1);
    EXPECT_TRUE(!values.empty());
  }

  values.clear();
  EXPECT_TRUE(values.size() == 0);
  EXPECT_TRUE(values.empty());
}

/// @brief test with int
TEST(HashSetTest, test_int) {
  arangodb::HashSet<int> values;

  EXPECT_TRUE(values.size() == 0);
  EXPECT_TRUE(values.empty());

  for (size_t i = 0; i < 100; ++i) {
    EXPECT_TRUE(values.size() == i);
    values.insert(static_cast<int>(i));
    EXPECT_TRUE(values.size() == i + 1);
    EXPECT_TRUE(!values.empty());
  }

  EXPECT_TRUE(values.size() == 100);
  EXPECT_TRUE(!values.empty());

  for (int i = 0; i < 100; ++i) {
    EXPECT_TRUE(values.find(i) != values.end());
  }

  EXPECT_TRUE(values.find(123) == values.end());
  EXPECT_TRUE(values.find(999) == values.end());
  EXPECT_TRUE(values.find(100) == values.end());
  EXPECT_TRUE(values.find(-1) == values.end());
}

/// @brief test with std::string
TEST(HashSetTest, test_string) {
  arangodb::HashSet<std::string> values;

  EXPECT_TRUE(values.size() == 0);
  EXPECT_TRUE(values.empty());

  for (size_t i = 0; i < 100; ++i) {
    EXPECT_TRUE(values.size() == i);
    values.insert(std::string("test") + std::to_string(i));
    EXPECT_TRUE(values.size() == i + 1);
    EXPECT_TRUE(!values.empty());
  }

  EXPECT_TRUE(values.size() == 100);
  EXPECT_TRUE(!values.empty());

  for (size_t i = 0; i < 100; ++i) {
    std::string value = std::string("test") + std::to_string(i);
    EXPECT_TRUE(values.find(value) != values.end());
  }

  EXPECT_TRUE(values.find(std::string("test")) == values.end());
  EXPECT_TRUE(values.find(std::string("foo")) == values.end());
  EXPECT_TRUE(values.find(std::string("test100")) == values.end());
  EXPECT_TRUE(values.find(std::string("")) == values.end());
}

/// @brief test with std::string
TEST(HashSetTest, test_long_string) {
  arangodb::HashSet<std::string> values;

  EXPECT_TRUE(values.size() == 0);
  EXPECT_TRUE(values.empty());

  for (size_t i = 0; i < 100; ++i) {
    EXPECT_TRUE(values.size() == i);
    values.insert(
        std::string("test-this-will-hopefully-disable-sso-everywhere") + std::to_string(i));
    EXPECT_TRUE(values.size() == i + 1);
    EXPECT_TRUE(!values.empty());
  }

  EXPECT_TRUE(values.size() == 100);
  EXPECT_TRUE(!values.empty());

  for (size_t i = 0; i < 100; ++i) {
    std::string value =
        std::string("test-this-will-hopefully-disable-sso-everywhere") + std::to_string(i);
    EXPECT_TRUE(values.find(value) != values.end());
  }

  EXPECT_TRUE(values.find(std::string("test")) == values.end());
  EXPECT_TRUE(values.find(std::string("foo")) == values.end());
  EXPECT_TRUE(values.find(std::string("test100")) == values.end());
  EXPECT_TRUE(values.find(std::string("")) == values.end());
}

/// @brief test with std::string
TEST(HashSetTest, test_string_duplicates) {
  arangodb::HashSet<std::string> values;

  EXPECT_TRUE(values.size() == 0);
  EXPECT_TRUE(values.empty());

  for (size_t i = 0; i < 100; ++i) {
    EXPECT_TRUE(values.size() == i);
    auto res = values.emplace(std::string("test") + std::to_string(i));
    EXPECT_TRUE(res.first != values.end());
    EXPECT_TRUE(res.second);
    EXPECT_TRUE(values.size() == i + 1);
    EXPECT_TRUE(!values.empty());
  }

  EXPECT_TRUE(values.size() == 100);
  EXPECT_TRUE(!values.empty());

  for (size_t i = 0; i < 100; ++i) {
    EXPECT_TRUE(values.size() == 100);
    auto res = values.emplace(std::string("test") + std::to_string(i));
    EXPECT_TRUE(res.first != values.end());
    EXPECT_TRUE(!res.second);
    EXPECT_TRUE(values.size() == 100);
    EXPECT_TRUE(!values.empty());
  }

  for (size_t i = 0; i < 100; ++i) {
    std::string value = std::string("test") + std::to_string(i);
    EXPECT_TRUE(values.find(value) != values.end());
  }

  EXPECT_TRUE(values.find(std::string("test")) == values.end());
  EXPECT_TRUE(values.find(std::string("foo")) == values.end());
  EXPECT_TRUE(values.find(std::string("test100")) == values.end());
  EXPECT_TRUE(values.find(std::string("")) == values.end());
}

/// @brief test erase
TEST(HashSetTest, test_erase) {
  arangodb::HashSet<int> values;

  EXPECT_TRUE(values.size() == 0);
  EXPECT_TRUE(values.empty());

  EXPECT_TRUE(values.erase(1234) == 0);
  EXPECT_TRUE(values.erase(0) == 0);

  for (int i = 0; i < 1000; ++i) {
    values.insert(i);
  }

  EXPECT_TRUE(values.erase(1234) == 0);
  EXPECT_TRUE(values.erase(0) == 1);

  EXPECT_TRUE(values.find(0) == values.end());
  for (int i = 1; i < 100; ++i) {
    EXPECT_TRUE(values.find(i) != values.end());
    EXPECT_TRUE(values.erase(i) == 1);
    EXPECT_TRUE(values.find(i) == values.end());
  }

  EXPECT_TRUE(values.size() == 900);

  for (int i = 100; i < 1000; ++i) {
    EXPECT_TRUE(values.find(i) != values.end());
    EXPECT_TRUE(values.erase(i) == 1);
    EXPECT_TRUE(values.find(i) == values.end());
  }

  EXPECT_TRUE(values.size() == 0);
  EXPECT_TRUE(values.empty());
}

/// @brief test reserve
TEST(HashSetTest, test_reserve) {
  arangodb::HashSet<size_t> values;

  EXPECT_TRUE(values.size() == 0);
  EXPECT_TRUE(values.empty());

  values.reserve(10000);
  EXPECT_TRUE(values.size() == 0);
  EXPECT_TRUE(values.empty());

  for (size_t i = 0; i < 32; ++i) {
    values.insert(i);
  }

  EXPECT_TRUE(values.size() == 32);
  EXPECT_TRUE(!values.empty());

  values.reserve(10);
  EXPECT_TRUE(values.size() == 32);
  EXPECT_TRUE(!values.empty());

  values.reserve(20000);
  EXPECT_TRUE(values.size() == 32);
  EXPECT_TRUE(!values.empty());

  for (size_t i = 0; i < 32; ++i) {
    EXPECT_TRUE(values.find(i) != values.end());
  }
}

/// @brief test few values
TEST(HashSetTest, test_few) {
  arangodb::HashSet<size_t> values;

  EXPECT_TRUE(values.size() == 0);
  EXPECT_TRUE(values.empty());

  for (size_t i = 0; i < 32; ++i) {
    EXPECT_TRUE(values.size() == i);
    values.insert(i);
    EXPECT_TRUE(values.size() == i + 1);
    EXPECT_TRUE(!values.empty());
  }

  EXPECT_TRUE(values.size() == 32);
  EXPECT_TRUE(!values.empty());

  for (size_t i = 0; i < 32; ++i) {
    EXPECT_TRUE(values.find(i) != values.end());
  }
}

/// @brief test many values
TEST(HashSetTest, test_many) {
  arangodb::HashSet<size_t> values;

  EXPECT_TRUE(values.size() == 0);
  EXPECT_TRUE(values.empty());

  for (size_t i = 0; i < 200000; ++i) {
    EXPECT_TRUE(values.size() == i);
    values.insert(i);
    EXPECT_TRUE(values.size() == i + 1);
    EXPECT_TRUE(!values.empty());
  }

  EXPECT_TRUE(values.size() == 200000);
  EXPECT_TRUE(!values.empty());

  for (size_t i = 0; i < 200000; ++i) {
    EXPECT_TRUE(values.find(i) != values.end());
  }
}

/// @brief test copying
TEST(HashSetTest, test_copy_construct_local) {
  arangodb::HashSet<int> values;

  EXPECT_TRUE(values.size() == 0);
  EXPECT_TRUE(values.empty());

  for (int i = 0; i < 2; ++i) {
    values.insert(i);
  }

  // copy
  arangodb::HashSet<int> copy(values);

  EXPECT_TRUE(values.size() == 2);
  EXPECT_TRUE(!values.empty());

  EXPECT_TRUE(copy.size() == 2);
  EXPECT_TRUE(!copy.empty());

  for (int i = 0; i < 2; ++i) {
    EXPECT_TRUE(values.find(i) != values.end());
    EXPECT_TRUE(copy.find(i) != copy.end());
  }

  values.clear();

  EXPECT_TRUE(values.size() == 0);
  EXPECT_TRUE(values.empty());
  EXPECT_TRUE(copy.size() == 2);
  EXPECT_TRUE(!copy.empty());

  for (int i = 0; i < 2; ++i) {
    EXPECT_TRUE(values.find(i) == values.end());
    EXPECT_TRUE(copy.find(i) != copy.end());
  }
}

/// @brief test copying
TEST(HashSetTest, test_copy_construct_heap) {
  arangodb::HashSet<int> values;

  EXPECT_TRUE(values.size() == 0);
  EXPECT_TRUE(values.empty());

  for (int i = 0; i < 100; ++i) {
    values.insert(i);
  }

  // copy
  arangodb::HashSet<int> copy(values);

  EXPECT_TRUE(values.size() == 100);
  EXPECT_TRUE(!values.empty());

  EXPECT_TRUE(copy.size() == 100);
  EXPECT_TRUE(!copy.empty());

  for (int i = 0; i < 100; ++i) {
    EXPECT_TRUE(values.find(i) != values.end());
    EXPECT_TRUE(copy.find(i) != copy.end());
  }

  values.clear();

  EXPECT_TRUE(values.size() == 0);
  EXPECT_TRUE(values.empty());
  EXPECT_TRUE(copy.size() == 100);
  EXPECT_TRUE(!copy.empty());

  for (int i = 0; i < 100; ++i) {
    EXPECT_TRUE(values.find(i) == values.end());
    EXPECT_TRUE(copy.find(i) != copy.end());
  }
}

/// @brief test copying
TEST(HashSetTest, test_copy_construct_heap_huge) {
  arangodb::HashSet<std::string> values;

  EXPECT_TRUE(values.size() == 0);
  EXPECT_TRUE(values.empty());

  for (int i = 0; i < 100; ++i) {
    values.insert(
        std::string("test-this-will-hopefully-disable-sso-everywhere") + std::to_string(i));
  }

  // copy
  arangodb::HashSet<std::string> copy(values);

  EXPECT_TRUE(values.size() == 100);
  EXPECT_TRUE(!values.empty());

  EXPECT_TRUE(copy.size() == 100);
  EXPECT_TRUE(!copy.empty());

  for (int i = 0; i < 100; ++i) {
    EXPECT_TRUE(
        values.find(
            std::string("test-this-will-hopefully-disable-sso-everywhere") +
            std::to_string(i)) != values.end());
    EXPECT_TRUE(
        copy.find(
            std::string("test-this-will-hopefully-disable-sso-everywhere") +
            std::to_string(i)) != copy.end());
  }

  values.clear();

  EXPECT_TRUE(values.size() == 0);
  EXPECT_TRUE(values.empty());
  EXPECT_TRUE(copy.size() == 100);
  EXPECT_TRUE(!copy.empty());

  for (int i = 0; i < 100; ++i) {
    EXPECT_TRUE(
        values.find(
            std::string("test-this-will-hopefully-disable-sso-everywhere") +
            std::to_string(i)) == values.end());
    EXPECT_TRUE(
        copy.find(
            std::string("test-this-will-hopefully-disable-sso-everywhere") +
            std::to_string(i)) != copy.end());
  }
}

/// @brief test copying
TEST(HashSetTest, test_copy_assign_local) {
  arangodb::HashSet<int> values;

  EXPECT_TRUE(values.size() == 0);
  EXPECT_TRUE(values.empty());

  for (int i = 0; i < 2; ++i) {
    values.insert(i);
  }

  // copy
  arangodb::HashSet<int> copy = values;

  EXPECT_TRUE(values.size() == 2);
  EXPECT_TRUE(!values.empty());

  EXPECT_TRUE(copy.size() == 2);
  EXPECT_TRUE(!copy.empty());

  for (int i = 0; i < 2; ++i) {
    EXPECT_TRUE(values.find(i) != values.end());
    EXPECT_TRUE(copy.find(i) != copy.end());
  }

  values.clear();

  EXPECT_TRUE(values.size() == 0);
  EXPECT_TRUE(values.empty());
  EXPECT_TRUE(copy.size() == 2);
  EXPECT_TRUE(!copy.empty());

  for (int i = 0; i < 2; ++i) {
    EXPECT_TRUE(values.find(i) == values.end());
    EXPECT_TRUE(copy.find(i) != copy.end());
  }
}

/// @brief test copying
TEST(HashSetTest, test_copy_assign_heap) {
  arangodb::HashSet<int> values;

  EXPECT_TRUE(values.size() == 0);
  EXPECT_TRUE(values.empty());

  for (int i = 0; i < 100; ++i) {
    values.insert(i);
  }

  // copy
  arangodb::HashSet<int> copy = values;

  EXPECT_TRUE(values.size() == 100);
  EXPECT_TRUE(!values.empty());

  EXPECT_TRUE(copy.size() == 100);
  EXPECT_TRUE(!copy.empty());

  for (int i = 0; i < 100; ++i) {
    EXPECT_TRUE(values.find(i) != values.end());
    EXPECT_TRUE(copy.find(i) != copy.end());
  }

  values.clear();

  EXPECT_TRUE(values.size() == 0);
  EXPECT_TRUE(values.empty());
  EXPECT_TRUE(copy.size() == 100);
  EXPECT_TRUE(!copy.empty());

  for (int i = 0; i < 100; ++i) {
    EXPECT_TRUE(values.find(i) == values.end());
    EXPECT_TRUE(copy.find(i) != copy.end());
  }
}

/// @brief test copying
TEST(HashSetTest, test_copy_assign_heap_huge) {
  arangodb::HashSet<std::string> values;

  EXPECT_TRUE(values.size() == 0);
  EXPECT_TRUE(values.empty());

  for (int i = 0; i < 100; ++i) {
    values.insert(
        std::string("test-this-will-hopefully-disable-sso-everywhere") + std::to_string(i));
  }

  // copy
  arangodb::HashSet<std::string> copy = values;

  EXPECT_TRUE(values.size() == 100);
  EXPECT_TRUE(!values.empty());

  EXPECT_TRUE(copy.size() == 100);
  EXPECT_TRUE(!copy.empty());

  for (int i = 0; i < 100; ++i) {
    EXPECT_TRUE(
        values.find(
            std::string("test-this-will-hopefully-disable-sso-everywhere") +
            std::to_string(i)) != values.end());
    EXPECT_TRUE(
        copy.find(
            std::string("test-this-will-hopefully-disable-sso-everywhere") +
            std::to_string(i)) != copy.end());
  }

  values.clear();

  EXPECT_TRUE(values.size() == 0);
  EXPECT_TRUE(values.empty());
  EXPECT_TRUE(copy.size() == 100);
  EXPECT_TRUE(!copy.empty());

  for (int i = 0; i < 100; ++i) {
    EXPECT_TRUE(
        values.find(
            std::string("test-this-will-hopefully-disable-sso-everywhere") +
            std::to_string(i)) == values.end());
    EXPECT_TRUE(
        copy.find(
            std::string("test-this-will-hopefully-disable-sso-everywhere") +
            std::to_string(i)) != copy.end());
  }
}

/// @brief test moving
TEST(HashSetTest, test_move_construct_local) {
  arangodb::HashSet<int> values;

  EXPECT_TRUE(values.size() == 0);
  EXPECT_TRUE(values.empty());

  for (int i = 0; i < 2; ++i) {
    values.insert(i);
  }

  // move
  arangodb::HashSet<int> copy(std::move(values));

  EXPECT_TRUE(values.size() == 0);
  EXPECT_TRUE(values.empty());

  EXPECT_TRUE(copy.size() == 2);
  EXPECT_TRUE(!copy.empty());

  for (int i = 0; i < 2; ++i) {
    EXPECT_TRUE(values.find(i) == values.end());
    EXPECT_TRUE(copy.find(i) != copy.end());
  }
}

/// @brief test moving
TEST(HashSetTest, test_move_construct_heap) {
  arangodb::HashSet<int> values;

  EXPECT_TRUE(values.size() == 0);
  EXPECT_TRUE(values.empty());

  for (int i = 0; i < 100; ++i) {
    values.insert(i);
  }

  // move
  arangodb::HashSet<int> copy(std::move(values));

  EXPECT_TRUE(values.size() == 0);
  EXPECT_TRUE(values.empty());

  EXPECT_TRUE(copy.size() == 100);
  EXPECT_TRUE(!copy.empty());

  for (int i = 0; i < 100; ++i) {
    EXPECT_TRUE(values.find(i) == values.end());
    EXPECT_TRUE(copy.find(i) != copy.end());
  }
}

/// @brief test moving
TEST(HashSetTest, test_move_construct_heap_huge) {
  arangodb::HashSet<std::string> values;

  EXPECT_TRUE(values.size() == 0);
  EXPECT_TRUE(values.empty());

  for (int i = 0; i < 100; ++i) {
    values.insert(
        std::string("test-this-will-hopefully-disable-sso-everywhere") + std::to_string(i));
  }

  // move
  arangodb::HashSet<std::string> copy(std::move(values));

  EXPECT_TRUE(values.size() == 0);
  EXPECT_TRUE(values.empty());

  EXPECT_TRUE(copy.size() == 100);
  EXPECT_TRUE(!copy.empty());

  for (int i = 0; i < 100; ++i) {
    EXPECT_TRUE(
        values.find(
            std::string("test-this-will-hopefully-disable-sso-everywhere") +
            std::to_string(i)) == values.end());
    EXPECT_TRUE(
        copy.find(
            std::string("test-this-will-hopefully-disable-sso-everywhere") +
            std::to_string(i)) != copy.end());
  }
}

/// @brief test moving
TEST(HashSetTest, test_move_assign_local) {
  arangodb::HashSet<int> values;

  EXPECT_TRUE(values.size() == 0);
  EXPECT_TRUE(values.empty());

  for (int i = 0; i < 2; ++i) {
    values.insert(i);
  }

  // move
  arangodb::HashSet<int> copy = std::move(values);

  EXPECT_TRUE(values.size() == 0);
  EXPECT_TRUE(values.empty());

  EXPECT_TRUE(copy.size() == 2);
  EXPECT_TRUE(!copy.empty());

  for (int i = 0; i < 2; ++i) {
    EXPECT_TRUE(values.find(i) == values.end());
    EXPECT_TRUE(copy.find(i) != copy.end());
  }
}

/// @brief test moving
TEST(HashSetTest, test_move_assign_heap) {
  arangodb::HashSet<int> values;

  EXPECT_TRUE(values.size() == 0);
  EXPECT_TRUE(values.empty());

  for (int i = 0; i < 100; ++i) {
    values.insert(i);
  }

  // move
  arangodb::HashSet<int> copy = std::move(values);

  EXPECT_TRUE(values.size() == 0);
  EXPECT_TRUE(values.empty());

  EXPECT_TRUE(copy.size() == 100);
  EXPECT_TRUE(!copy.empty());

  for (int i = 0; i < 100; ++i) {
    EXPECT_TRUE(values.find(i) == values.end());
    EXPECT_TRUE(copy.find(i) != copy.end());
  }
}

/// @brief test moving
TEST(HashSetTest, test_move_assign_heap_huge) {
  arangodb::HashSet<std::string> values;

  EXPECT_TRUE(values.size() == 0);
  EXPECT_TRUE(values.empty());

  for (int i = 0; i < 100; ++i) {
    values.insert(
        std::string("test-this-will-hopefully-disable-sso-everywhere") + std::to_string(i));
  }

  // move
  arangodb::HashSet<std::string> copy = std::move(values);

  EXPECT_TRUE(values.size() == 0);
  EXPECT_TRUE(values.empty());

  EXPECT_TRUE(copy.size() == 100);
  EXPECT_TRUE(!copy.empty());

  for (int i = 0; i < 100; ++i) {
    EXPECT_TRUE(
        values.find(
            std::string("test-this-will-hopefully-disable-sso-everywhere") +
            std::to_string(i)) == values.end());
    EXPECT_TRUE(
        copy.find(
            std::string("test-this-will-hopefully-disable-sso-everywhere") +
            std::to_string(i)) != copy.end());
  }
}

/// @brief test iterator
TEST(HashSetTest, test_iterator) {
  arangodb::HashSet<int> values;

  EXPECT_TRUE(values.begin() == values.end());

  for (int i = 0; i < 1000; ++i) {
    values.insert(i);
    EXPECT_TRUE(values.begin() != values.end());
    EXPECT_TRUE(values.find(i) != values.end());
    EXPECT_TRUE(values.find(i + 1000) == values.end());
  }

  size_t count;

  count = 0;
  for (auto const& it : values) {
    EXPECT_TRUE(it >= 0);
    EXPECT_TRUE(it < 1000);
    ++count;
  }
  EXPECT_TRUE(count == 1000);

  count = 0;
  for (auto it = values.begin(); it != values.end(); ++it) {
    EXPECT_TRUE(it != values.end());
    auto i = (*it);
    EXPECT_TRUE(i >= 0);
    EXPECT_TRUE(i < 1000);
    ++count;
  }
  EXPECT_TRUE(count == 1000);

  count = 0;
  for (auto it = values.cbegin(); it != values.cend(); ++it) {
    EXPECT_TRUE(it != values.end());
    auto i = (*it);
    EXPECT_TRUE(i >= 0);
    EXPECT_TRUE(i < 1000);
    ++count;
  }
  EXPECT_TRUE(count == 1000);
}
