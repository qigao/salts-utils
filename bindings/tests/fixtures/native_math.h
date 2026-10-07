#ifndef TEST_NATIVE_MATH_H
#define TEST_NATIVE_MATH_H
#ifdef __cplusplus
extern "C" {
#endif
double native_sum(double left, double right);
int native_add(int left, int right);
int native_sum3(int a, int b, int c);
void native_store(int value);
int native_load(void);
typedef struct native_point { int x; int y; } native_point;
#ifdef __cplusplus
}
#endif
#endif
