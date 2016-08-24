/*
 * Copyright (c) 2012, Oracle and/or its affiliates. All rights reserved.
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

#ifndef SHARE_VM_CI_CICACHEREPLAY_HPP
#define SHARE_VM_CI_CICACHEREPLAY_HPP

#include "ci/ciMethod.hpp"

// ciReplay

//
// Replay compilation of a java method by using an information in replay file.
// Replay inlining decisions during compilation by using an information in inline file.
//
// NOTE: these replay functions only exist in debug version of VM.
//
// Replay compilation.
// -------------------
//
// Replay data file replay.txt can be created by Serviceability Agent
// from a core file, see agent/doc/cireplay.html
//
// $ java -cp <jdk>/lib/sa-jdi.jar sun.jvm.hotspot.CLHSDB
// hsdb> attach <jdk>/bin/java ./core
// hsdb> threads
// t@10 Service Thread
// t@9 C2 CompilerThread0
// t@8 Signal Dispatcher
// t@7 Finalizer
// t@6 Reference Handler
// t@2 main
// hsdb> dumpreplaydata t@9 > replay.txt
// hsdb> quit
//
// (Note: SA could be also used to extract app.jar and boot.jar files
//  from core file to replay compilation if only core file is available)
//
// Replay data file replay_pid%p.log is also created when VM crashes
// in Compiler thread during compilation. It is controlled by
// DumpReplayDataOnError flag which is ON by default.
//
// Replay file replay_pid%p_compid%d.log can be created
// for the specified java method during normal execution using
// CompileCommand option DumpReplay:
//
// -XX:CompileCommand=option,Benchmark::test,DumpReplay
//
// In this case the file name has additional compilation id "_compid%d"
// because the method could be compiled several times.
//
// To replay compilation the replay file should be specified:
//
// -XX:+ReplayCompiles -XX:ReplayDataFile=replay_pid2133.log
//
// VM thread reads data from the file immediately after VM initialization
// and puts the compilation task on compile queue. After that it goes into
// wait state (BackgroundCompilation flag is set to false) since there is no
// a program to execute. VM exits when the compilation is finished.
//
//
// Replay inlining.
// ----------------
//
// Replay inlining file inline_pid%p_compid%d.log is created for
// a specific java method during normal execution of a java program
// using CompileCommand option DumpInline:
//
// -XX:CompileCommand=option,Benchmark::test,DumpInline
//
// To replay inlining the replay file and the method should be specified:
//
// -XX:CompileCommand=option,Benchmark::test,ReplayInline -XX:InlineDataFile=inline_pid3244_compid6.log
//
// The difference from replay compilation is that replay inlining
// is performed during normal java program execution.
//
class ciMethodDataRecord {
public:
  const char* _klass_name;
  const char* _method_name;
  const char* _signature;

  int _state;
  int _current_mileage;

  intptr_t* _data;
  char*     _orig_data;
  Klass**   _classes;
  Method**  _methods;
  int*      _classes_offsets;
  int*      _methods_offsets;
  int       _data_length;
  int       _orig_data_length;
  int       _classes_length;
  int       _methods_length;
};

class ciMethodRecord {
public:
  const char* _klass_name;
  const char* _method_name;
  const char* _signature;

  int _instructions_size;
  int _interpreter_invocation_count;
  int _interpreter_throwout_count;
  int _invocation_counter;
  int _backedge_counter;
};

class ciInlineRecord {
public:
  const char* _klass_name;
  const char* _method_name;
  const char* _signature;

  int _inline_depth;
  int _inline_bci;
};

class CacheReplayState : public CHeapObj<mtCompiler> {
 private:
  char*   _stream;
  int     _stream_index;
  Thread* _thread;
  Handle  _protection_domain;
  Handle  _loader;

  GrowableArray<ciMethodRecord*>*     _ci_method_records;
  GrowableArray<ciMethodDataRecord*>* _ci_method_data_records;

  // Use pointer because we may need to return inline records
  // without destroying them.
  GrowableArray<ciInlineRecord*>*    _ci_inline_records;

  const char* _error_message;

  char* _bufptr;
  char* _buffer;
  int   _buffer_length;
  int   _buffer_pos;

  // "compile" data
  ciKlass* _iklass;
  Method*  _imethod;
  int      _entry_bci;
  int      _comp_level;
  int      _osr_bci;
  bool     _blocked;

 public:
  CacheReplayState(char* unparsed_data, TRAPS, int osr_bci, bool blocked);
  ~CacheReplayState();

  bool had_error();
    bool can_replay();
    void report_error(const char* msg);
    int parse_int(const char* label);
    intptr_t parse_intptr_t(const char* label);
    void skip_ws();
    char* scan_and_terminate(char delim);
    char* parse_string();
    char* parse_quoted_string();
    const char* parse_escaped_string();
    bool parse_tag_and_count(const char* tag, int& length);
    char* parse_data(const char* tag, int& length);
    intptr_t* parse_intptr_data(const char* tag, int& length);
    Symbol* parse_symbol(TRAPS);
    Klass* parse_klass(TRAPS);
    Klass* resolve_klass(const char* klass, TRAPS);
    Method* parse_method(TRAPS);
    int get_line(int c);
    void process(TRAPS);
    void process_command(TRAPS);
    bool is_valid_comp_level(int comp_level);
    void* process_inline(ciMethod* imethod, Method* m, int entry_bci, int comp_level, TRAPS);
    void process_compile(TRAPS);
    void process_ciMethod(TRAPS);
    void process_ciMethodData(TRAPS);
    void process_instanceKlass(TRAPS);
    void process_ciInstanceKlass(TRAPS);
    void process_staticfield(TRAPS);
    void process_JvmtiExport(TRAPS);
    ciMethodRecord* new_ciMethodRecord(Method* method);
    ciMethodRecord* find_ciMethodRecord(Method* method);
    ciMethodDataRecord* new_ciMethodDataRecord(Method* method);
    ciMethodDataRecord* find_ciMethodDataRecord(Method* method);
    ciInlineRecord* new_ciInlineRecord(Method* method, int bci, int depth);
    ciInlineRecord* find_ciInlineRecord(Method* method, int bci, int depth);
    ciInlineRecord* find_ciInlineRecord(GrowableArray<ciInlineRecord*>*  records,
                                        Method* method, int bci, int depth);
    const char* error_message();
    void reset();
    void unescape_string(char* value);
};
class ciCacheReplay {
  CI_PACKAGE_ACCESS

//#ifndef PRODUCT
 private:

 public:
  // Replay specified cached compilation and do NOT exit VM.
  static CacheReplayState* replay_cached(TRAPS, char* replay_data, int osr_bci, bool blocked);
  // Load inlining decisions from file and use them
  // during compilation of specified method.
  //void* load_inline_data(ciMethod* method, int entry_bci, int comp_level);

  // These are used by the CI to fill in the cached data from the
  // replay file when replaying compiles.
  static  void initialize(CacheReplayState* replay_state, ciMethodData* method);
  static void initialize(CacheReplayState* replay_state, ciMethod* method);

  static bool is_loaded(CacheReplayState* replay_state, Method* method);
  static bool is_loaded(CacheReplayState* replay_state, Klass* klass); // this is not used currently

  static bool should_not_inline(CacheReplayState* replay_state, ciMethod* method);
  static bool should_inline(CacheReplayState* replay_state, ciMethod* method, int bci, int inline_depth);
  static bool should_not_inline(CacheReplayState* replay_state, ciMethod* method, int bci, int inline_depth);

//#endif
};

#endif // SHARE_VM_CI_CICACHEREPLAY_HPP
