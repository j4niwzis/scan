// ctre's own module, carried here rather than reached for.
//
// Upstream keeps this file at the root of its tree and the headers it names
// under `include/`, so it compiles wherever the include path says -- which is
// what linking ctre::ctre gives it. What it cannot survive is being named by a
// path into somebody else's source directory: a port answered from a prefix,
// from the machine, or from anything that leaves only what was installed has
// no source directory at all, and the name comes out empty.
//
// Copied from compile-time-regular-expressions v3.10.0, which is the tag the
// port pins, under Apache-2.0. Raise the two together.
module;

#ifdef _MSVC_LANG
#pragma warning( disable : 5202 )
#endif

import std;

export module ctre;

#define CTRE_IN_A_MODULE
#define CTLL_IN_A_MODULE
#define UNICODE_DB_IN_A_MODULE

using std::int16_t;
using std::int32_t;
using std::int64_t;
using std::int8_t;
using std::size_t;
using std::uint16_t;
using std::uint32_t;
using std::uint64_t;
using std::uint8_t;

#include "ctre.hpp"
#include "unicode-db.hpp"
