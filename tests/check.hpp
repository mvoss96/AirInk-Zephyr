#pragma once
#include <cstdio>

/*
 * Two macros and a counter. These tests cover pure integer logic that would otherwise
 * only be reachable by flashing a board: the EMA filter, the Li-Ion curve, and the
 * display rounding. Anything needing Zephyr, LVGL or the panel is out of scope here --
 * that is what sim/ and bench/ are for.
 *
 * Not ztest: nothing under test includes a Zephyr header, so twister and a native_sim
 * build would only add ceremony. Revisit once radio or storage code needs covering.
 */
inline int g_checks = 0;
inline int g_failures = 0;

/* Variadic so a template argument list's comma -- CHECK(settle<Ema<5, 3>>(..)) -- is
 * not mistaken for a macro argument separator. */
#define CHECK(...)                                                              \
	do                                                                          \
	{                                                                           \
		g_checks++;                                                             \
		if (!(__VA_ARGS__))                                                     \
		{                                                                       \
			std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #__VA_ARGS__); \
			g_failures++;                                                       \
		}                                                                       \
	} while (0)

#define CHECK_EQ(actual, expected)                                           \
	do                                                                       \
	{                                                                        \
		g_checks++;                                                          \
		const long long a_ = (long long)(actual);                            \
		const long long e_ = (long long)(expected);                          \
		if (a_ != e_)                                                        \
		{                                                                    \
			std::printf("  FAIL %s:%d  %s == %lld, expected %lld\n",         \
						__FILE__, __LINE__, #actual, a_, e_);                \
			g_failures++;                                                    \
		}                                                                    \
	} while (0)
