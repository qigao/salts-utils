#include "native_counter.hpp"
#include <stdexcept>
int NativeCounter::add(int left, int right) { value += left + right; return value; }
int NativeCounter::read() const noexcept { return value; }
void NativeCounter::reset() { value = 0; }
int NativeCounter::fail() const { throw std::runtime_error("native failure"); }
