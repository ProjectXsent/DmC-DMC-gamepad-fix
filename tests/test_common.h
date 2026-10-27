// Tiny dependency-free test helper.
#pragma once
#include <cstdio>
static int g_failures = 0;
static int g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_EQ(a, b) do { ++g_checks; long long _a = (long long)(a), _b = (long long)(b); \
    if (_a != _b) { ++g_failures; \
    std::printf("FAIL %s:%d  %s == %s  (%lld vs %lld)\n", __FILE__, __LINE__, #a, #b, _a, _b); } } while (0)
#define TEST_SUMMARY() (std::printf("%d checks, %d failures\n", g_checks, g_failures), g_failures ? 1 : 0)
