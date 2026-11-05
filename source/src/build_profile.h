/*
 * GBA Rush PSP
 *
 * Copyright (C) 2026 Piero Carrieri
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

/*
 * The build profile, chosen by source/Makefile: make PROFILE=release (the
 * player's EBOOT, the default) or make PROFILE=diagnostic. The diagnostic
 * features are switched on here and nowhere else (scripts/check-profiles.sh),
 * so a release build that asks for one from the command line stops here.
 */

#ifndef BUILD_PROFILE_H
#define BUILD_PROFILE_H

#if defined(GBA_RUSH_PROFILE_RELEASE) && defined(GBA_RUSH_PROFILE_DIAGNOSTIC)
#error "Both build profiles are defined: build with make PROFILE=release or make PROFILE=diagnostic"

#elif defined(GBA_RUSH_PROFILE_DIAGNOSTIC)
#define GBA_RUSH_PROFILE_NAME "diagnostic"
#define ENABLE_DIAGNOSTIC_INPUT_TRACE

#elif defined(GBA_RUSH_PROFILE_RELEASE)
#define GBA_RUSH_PROFILE_NAME "release"
/* Every diagnostic feature, the planned ones included: a new one gets its
   line here before its first #ifdef. */
#if defined(ENABLE_DIAGNOSTIC_INPUT_TRACE)
#error "ENABLE_DIAGNOSTIC_INPUT_TRACE (input trace) is a diagnostic feature: build it with make PROFILE=diagnostic"
#endif
#if defined(ENABLE_DIAGNOSTIC_AUTORUN)
#error "ENABLE_DIAGNOSTIC_AUTORUN (autorun) is a diagnostic feature: build it with make PROFILE=diagnostic"
#endif
#if defined(ENABLE_DIAGNOSTIC_NET_INJECTION)
#error "ENABLE_DIAGNOSTIC_NET_INJECTION (network injection) is a diagnostic feature: build it with make PROFILE=diagnostic"
#endif
#if defined(ENABLE_DIAGNOSTIC_POWER_INJECTION)
#error "ENABLE_DIAGNOSTIC_POWER_INJECTION (power event injection) is a diagnostic feature: build it with make PROFILE=diagnostic"
#endif
#if defined(ENABLE_DIAGNOSTIC_USB)
#error "ENABLE_DIAGNOSTIC_USB (USB pass-through) is a diagnostic feature: build it with make PROFILE=diagnostic"
#endif

#else
#error "No build profile is defined: build with make PROFILE=release or make PROFILE=diagnostic"
#endif

#endif /* BUILD_PROFILE_H */
