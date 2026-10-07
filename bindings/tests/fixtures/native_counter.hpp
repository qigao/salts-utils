#ifndef TEST_NATIVE_COUNTER_HPP
#define TEST_NATIVE_COUNTER_HPP
class NativeCounter {
public:
  int value = 0;
  const int id = 7;
  int add(int left, int right);
  int read() const noexcept;
  void reset();
  int fail() const;
};
#endif
