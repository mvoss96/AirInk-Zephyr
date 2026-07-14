#pragma once

/** @file
 * The firmware version, shown on the boot splash and in the console line at start-up.
 *
 * The number itself is NOT here. It lives in <repo>/VERSION -- once, for both applications, for the
 * boot banner, and (in the Matter build) for the software version a controller reports under Basic
 * Information. Zephyr turns that file into APP_VERSION_STRING; cmake/airink_version.cmake is what
 * points it at the shared file rather than at a per-app copy.
 *
 * It used to be written here as well, as a literal, next to a VERSION file that said the same thing.
 * Two numbers that must agree but need not are a bug waiting for a release.
 *
 * The host preview (sim/) has no Zephyr and so no generated header: sim/build.ps1 reads the same
 * VERSION file and passes the string in with -D. Same source, two roads.
 */

#ifdef __ZEPHYR__
#include <zephyr/app_version.h>
#define AIRINK_VERSION APP_VERSION_STRING
#elif !defined(AIRINK_VERSION)
#error "Host build: AIRINK_VERSION must come from <repo>/VERSION -- see sim/build.ps1."
#endif
