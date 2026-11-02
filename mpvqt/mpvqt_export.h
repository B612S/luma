/*
 * Luma-specific shim for the export header that upstream mpvqt generates with
 * CMake. Not part of upstream KDE mpvqt.
 *
 * Luma compiles the vendored mpvqt sources directly into its target instead of
 * building mpvqt as a separate library, so no import/export attributes are
 * needed. Distributed under the same license as the vendored mpvqt sources:
 * LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL.
 */

#pragma once

#ifndef MPVQT_EXPORT
#define MPVQT_EXPORT
#endif

#ifndef MPVQT_NO_EXPORT
#define MPVQT_NO_EXPORT
#endif
