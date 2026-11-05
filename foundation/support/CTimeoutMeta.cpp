// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2020 钟先耀; net80211/CTimeout integration metadata.
// The original itlwm.cpp supplies this definition alongside its Intel frontend.
// Keep it in the shared-stack integration instead of linking that frontend.
#include <sys/CTimeout.hpp>

OSDefineMetaClassAndStructors(CTimeout, OSObject)
