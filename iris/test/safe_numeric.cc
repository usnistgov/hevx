#include "iris/safe_numeric.h"
#include "gtest/gtest.h"

using SafeFloat = iris::SafeNumeric<float, struct SafeFloatTag>;
using SafeInt = iris::SafeNumeric<int, struct SafeIntTag>;

TEST(SafeNumeric, add) {
  SafeFloat a(1.f), b(2.f);
  auto const c = a + b;
  EXPECT_FLOAT_EQ(float(c), 3.f);
}

TEST(SafeNumeric, sub) {
  SafeFloat a(1.f), b(2.f);
  auto const c = b - a;
  EXPECT_FLOAT_EQ(float(c), 1.f);
}

TEST(SafeNumeric, mul) {
  SafeFloat a(1.f), b(2.f);
  auto const c = a * b;
  EXPECT_FLOAT_EQ(float(c), 2.f);
}

TEST(SafeNumeric, div) {
  SafeFloat a(1.f), b(2.f);
  auto const c = a / b;
  EXPECT_FLOAT_EQ(float(c), .5f);
}

TEST(SafeNumeric, add_assign) {
  SafeFloat a(1.f), b(2.f);
  a += b;
  EXPECT_FLOAT_EQ(float(a), 3.f);
}

TEST(SafeNumeric, sub_assign) {
  SafeFloat a(1.f), b(2.f);
  b -= a;
  EXPECT_FLOAT_EQ(float(a), 1.f);
}

TEST(SafeNumeric, mul_assign) {
  SafeFloat a(1.f), b(2.f);
  a *= b;
  EXPECT_FLOAT_EQ(float(a), 2.f);
}

TEST(SafeNumeric, div_assign) {
  SafeFloat a(1.f), b(2.f);
  a /= b;
  EXPECT_FLOAT_EQ(float(a), .5f);
}

TEST(SafeNumeric, pre_inc) {
  SafeInt a(1);
  ++a;
  EXPECT_EQ(int(a), 2);
}

TEST(SafeNumeric, pre_dec) {
  SafeInt a(1);
  --a;
  EXPECT_EQ(int(a), 0);
}

TEST(SafeNumeric, post_inc) {
  SafeInt a(1);
  auto b = a++;
  EXPECT_EQ(int(a), 2);
  EXPECT_EQ(int(b), 1);
}

TEST(SafeNumeric, post_dec) {
  SafeInt a(1);
  auto b = a--;
  EXPECT_EQ(int(a), 0);
  EXPECT_EQ(int(b), 1);
}

TEST(SafeNumeric, comparisons) {
  SafeInt a(1), b(2), c(1);

  EXPECT_TRUE(a == c);
  EXPECT_FALSE(a != c);
  EXPECT_TRUE(a < b);
  EXPECT_FALSE(a < c);
  EXPECT_TRUE(a <= b);
  EXPECT_TRUE(a <= c);

  EXPECT_FALSE(a == b);

  EXPECT_FALSE(b == c);
  EXPECT_TRUE(b != c);
  EXPECT_TRUE(b > c);
  EXPECT_TRUE(b >= c);
}

