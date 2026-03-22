#include <gtest/gtest.h>

#include "nano_utils/nano_utils.h"

TEST(EBOStorageTest, Basic) {
  class EmptyTest {
    void foo() { std::cout << "foo" << std::endl; }
  };
  class NonEmptyTest {
    int x_{};
    void foo() { std::cout << "foo" << std::endl; }
  };

  class A {
    int x_{};
    EmptyTest a_{};
  };
  class B {
    int x_{};
    NonEmptyTest a_{};
  };

  class C : public NanoCU::EBOStorage<EmptyTest> {
    int x_{};
  };
  class D : public NanoCU::EBOStorage<NonEmptyTest> {
    int x_{};
  };

  EXPECT_EQ(sizeof(A), 8);
  EXPECT_EQ(sizeof(B), 8);

  EXPECT_EQ(sizeof(C), 4);  // EBO
  EXPECT_EQ(sizeof(D), 8);
}
