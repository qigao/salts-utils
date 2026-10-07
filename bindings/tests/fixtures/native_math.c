#include "native_math.h"

double native_sum(double left, double right) { return left + right; }
int native_add(int left, int right) { return left + right; }
int native_sum3(int a, int b, int c) { return a + b + c; }
static int native_stored_value;
void native_store(int value) { native_stored_value = value; }
int native_load(void) { return native_stored_value; }
