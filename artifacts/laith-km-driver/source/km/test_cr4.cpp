#include <ntddk.h>
#include <intrin.h>
void test() { ULONG_PTR cr4 = __readcr4(); __writecr4(cr4 & ~(1ULL << 21)); }
