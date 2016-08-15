/*
ciCacheProfiles.cpp * Copyright (c) 2015, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 */

#ifndef SHARE_VM_CI_CICACHEPROFILES_HPP
#define SHARE_VM_CI_CICACHEPROFILES_HPP

#include "ci/ciMethod.hpp"
//#include "libadt/dict.hpp"

// -------------------
// ciCacheProfiles & ciCacheReplay
// -------------------
//
// Cache profiling information of a java method to disk and retrieve this data
// in further executions of the JVM.
//
// NOTE: this functionality is only enabled in "experimental mode"
// (flag: -XX:+UnlockExperimentalVMOptions)
//
// -------------------
// Dump profile data.
// -------------------
//
// Use the flag -XX:+DumpProfiles to dump all compiled methods to disk (cached_profiles.dat)
//
// One can also specify the flags DumpProfile or IgnoreDumpProfile as CompileCommand
// for specific method inclusions / exclusions
// e.g. -XX:CompileCommand=option,Benchmark::test,DumpProfile
//
// -------------------
// Use profile data.
// -------------------
//
// The flag -XX:+CacheProfiles tells the VM to use cached profiles when available
// Before any compilation happen this class scans through the txt file and builds
// the datastructure (consider the linear overhead).
// One can also specify a profiles file with -XX:CacheProfilesFile=foo.txt
//
// CacheProfiles can be used in 3 different modes:
// with flag -XX:CacheProfilesMode={0,1,2}
//
// Mode 0: lower the compilation threshold scaling of cached methods automatically
//         to 0.01 of cached methods automatically so they get compiled
//         earlier and with the highest available profile (usually C2)
//         One can use a different value than 0.01 by using the flag
//         -XX:CacheProfilesMode0ThresholdScaling=x.xx
// Mode 1: do not lower the thresholds but once a compilation is triggered
//         use highest available profile (usually C2)
// Mode 2: do not lower the thresholds but use compile level 2 (limited profiles)
//         instead of compile level 3 (full profiles) when a cached method gets
//         compiled with C1.
//         The idea is that this method already has enough profiles (on disk)
//         available so we get a faster C1 phase.
//         C2 will still use the cached profile like in other modes.
//
// -------------------
// Debug flags.
// -------------------
//
// -XX:+PrintCacheProfiles - prints debug information about cache profiles thread
// -XX:+PrintDeoptimizationCount - prints deoptimization count after JVM execution
// -XX:+PrintDeoptimizationCountVerbose - prints deopt count after each compilation
// -XX:+PrintCompileQueueSize - print size of compile queue after each compilation
//
// This class contains the functionality of CacheProfiles that interact
// with the CompileBroker when cached profiles are loaded.
// ciCacheReplay is basically an extended and modified copy of ciReplay
class ciCacheProfiles : public AllStatic {
  CI_PACKAGE_ACCESS
  friend class ciCacheReplay;
  friend class CacheCompileReplay;
private:

  static bool had_error();
  static void report_error(const char* msg);

  static int get_line(int c);
  static int parse_int(const char* label);
  static void skip_ws();
  static char* scan_and_terminate(char delim);
  // new
  static void unescape_string(char* value);
  static char* parse_quoted_string();
  static const char* parse_escaped_string();
  // notnew
  static char* parse_string();

  static bool can_replay();

  static const char* error_message();

  static int replay_impl(TRAPS, Method* method, int osr_bci, bool blocked);
  static int replay_method(TRAPS, Method* method, int osr_bci, bool blocked);

  static void process_file(TRAPS);

  static char* get_key(Method* method);
  static char* get_key(methodHandle method);
  static char* get_key(const char* klass_name, const char* method_name, const char* signature);
  static int is_cached(char* key);

  static const char* _error_message;
  static const char* _CMD_COMPILE;

  static FILE*   _stream;
  static Thread* _thread;

  static char* _bufptr;
  static char* _buffer;
  static int   _buffer_length;
  static int   _buffer_pos;

  static bool _initialized;
  static bool CacheIgnoreInitErrors;

  static Dict* _compile_records_dictionary;
  static Dict* _compile_records;

public:
  // Replay specified compilation
  static int replay(TRAPS, Method* method, int osr_bci, bool blocked);

  static void initialize(TRAPS);

  static bool is_initialized();
  static void is_initialized(bool flag);

  static int is_cached(methodHandle method);
  static int is_cached(Method* method);

  static void clear_cache(Method* method);
};

#endif // SHARE_VM_CI_CICACHEPROFILES_HPP
