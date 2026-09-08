/*
 * SDL2 still includes this header on Apple platforms. Xcode 16 removed the
 * legacy name from the SDK; Availability.h contains the same deployment
 * target macros needed by SDL2's platform header.
 */
#ifndef NV_AVAILABILITY_MACROS_COMPAT_H
#define NV_AVAILABILITY_MACROS_COMPAT_H
#include <Availability.h>
#ifndef MAC_OS_X_VERSION_MIN_REQUIRED
# if defined(__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__)
#  define MAC_OS_X_VERSION_MIN_REQUIRED __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__
# else
#  define MAC_OS_X_VERSION_MIN_REQUIRED 101300
# endif
#endif
#endif
