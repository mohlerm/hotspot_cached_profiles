/* Copyright (c) 2015, Oracle and/or its affiliates. All rights reserved.
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

#include "precompiled.hpp"
#include "ci/ciMethodData.hpp"
#include "ci/ciCacheProfiles.hpp"
#include "ci/ciCacheReplay.hpp"
#include "ci/ciSymbol.hpp"
#include "ci/ciKlass.hpp"
#include "ci/ciUtilities.hpp"
#include "compiler/compileBroker.hpp"
#include "compiler/compilerOracle.hpp"
#include "memory/allocation.inline.hpp"
#include "memory/oopFactory.hpp"
#include "memory/resourceArea.hpp"
#include "oops/oop.inline.hpp"
#include "utilities/copy.hpp"
#include "utilities/macros.hpp"

const char* ciCacheProfiles::_error_message;
const char* ciCacheProfiles::_CMD_COMPILE = "compile";

FILE*   ciCacheProfiles::_stream = NULL;
Thread* ciCacheProfiles::_thread = NULL;
char* ciCacheProfiles::_bufptr = NULL;
char* ciCacheProfiles::_buffer = NULL;
int   ciCacheProfiles::_buffer_length = 0;
int   ciCacheProfiles::_buffer_pos = 0;

Dict* ciCacheProfiles::_compile_records_dictionary = NULL;
Dict* ciCacheProfiles::_compile_records  = NULL;

bool ciCacheProfiles::CacheIgnoreInitErrors = false;
bool ciCacheProfiles::_initialized = false;

bool ciCacheProfiles::had_error() {
  return _error_message != NULL || _thread->has_pending_exception();
}

void ciCacheProfiles::report_error(const char* msg) {
  _error_message = msg;
  // Restore the _buffer contents for error reporting
  for (int i = 0; i < _buffer_pos; i++) {
    if (_buffer[i] == '\0') _buffer[i] = ' ';
  }
}

int ciCacheProfiles::get_line(int c) {
  while(c != EOF) {
    if (_buffer_pos + 1 >= _buffer_length) {
      int new_length = _buffer_length * 2;
      // Next call will throw error in case of OOM.
      _buffer = REALLOC_RESOURCE_ARRAY(char, _buffer, _buffer_length, new_length);
      _buffer_length = new_length;
    }
    if (c == '\n') {
      _buffer[_buffer_pos++] = c;
      c = getc(_stream); // get next char
      break;
    } else if (c == '\r') {
      // skip LF
    } else {
      _buffer[_buffer_pos++] = c;
    }
    c = getc(_stream);
  }
  // null terminate it, reset the pointer
  _buffer[_buffer_pos] = '\0'; // NL or EOF
  _buffer_pos = 0;
  _bufptr = _buffer;
  return c;
}

int ciCacheProfiles::parse_int(const char* label) {
  if (had_error()) {
    return 0;
  }

  int v = 0;
  int read;
  if (sscanf(_bufptr, "%i%n", &v, &read) != 1) {
    report_error(label);
  } else {
    _bufptr += read;
  }
  return v;
}

void ciCacheProfiles::skip_ws() {
  // Skip any leading whitespace
  while (*_bufptr == ' ' || *_bufptr == '\t') {
    _bufptr++;
  }
}

char* ciCacheProfiles::scan_and_terminate(char delim) {
  char* str = _bufptr;
  while (*_bufptr != delim && *_bufptr != '\0') {
    _bufptr++;
  }
  if (*_bufptr != '\0') {
    *_bufptr++ = '\0';
  }
  if (_bufptr == str) {
    // nothing here
    return NULL;
  }
  return str;
}
// Take an ascii string contain \u#### escapes and convert it to utf8
  // in place.
void ciCacheProfiles::unescape_string(char* value) {
char* from = value;
char* to = value;
while (*from != '\0') {
  if (*from != '\\') {
	*from++ = *to++;
  } else {
	switch (from[1]) {
	  case 'u': {
		from += 2;
		jchar value=0;
		for (int i=0; i<4; i++) {
		  char c = *from++;
		  switch (c) {
			case '0': case '1': case '2': case '3': case '4':
			case '5': case '6': case '7': case '8': case '9':
			  value = (value << 4) + c - '0';
			  break;
			case 'a': case 'b': case 'c':
			case 'd': case 'e': case 'f':
			  value = (value << 4) + 10 + c - 'a';
			  break;
			case 'A': case 'B': case 'C':
			case 'D': case 'E': case 'F':
			  value = (value << 4) + 10 + c - 'A';
			  break;
			default:
			  ShouldNotReachHere();
		  }
		}
		UNICODE::convert_to_utf8(&value, 1, to);
		to++;
		break;
	  }
	  case 't': *to++ = '\t'; from += 2; break;
	  case 'n': *to++ = '\n'; from += 2; break;
	  case 'r': *to++ = '\r'; from += 2; break;
	  case 'f': *to++ = '\f'; from += 2; break;
	  default:
		ShouldNotReachHere();
	}
  }
}
*from = *to;
}
char* ciCacheProfiles::parse_quoted_string() {
  if (had_error()) return NULL;

  skip_ws();

  if (*_bufptr == '"') {
    _bufptr++;
    return scan_and_terminate('"');
  } else {
    return scan_and_terminate(' ');
  }
}

const char* ciCacheProfiles::parse_escaped_string() {
  char* result = parse_quoted_string();
  if (result != NULL) {
    unescape_string(result);
  }
  return result;
}

char* ciCacheProfiles::parse_string() {
  if (had_error()) return NULL;

  skip_ws();
  return scan_and_terminate(' ');
}

bool ciCacheProfiles::can_replay() {
  return !(_stream == NULL || had_error());
}
const char* ciCacheProfiles::error_message() {
  return _error_message;
}

int ciCacheProfiles::replay(TRAPS, Method* method, int osr_bci, bool blocked) {
  _thread = THREAD;
  int exit_code = replay_impl(THREAD, method, osr_bci, blocked);
  return exit_code;
}

int ciCacheProfiles::replay_impl(TRAPS, Method* method, int osr_bci, bool blocked) {
  HandleMark hm;
  ResourceMark rm;

  int exit_code = 0;
  if (can_replay()) {
    exit_code = replay_method(THREAD, method, osr_bci, blocked);
  } else {
    exit_code = 1;
    return exit_code;
  }

  if (HAS_PENDING_EXCEPTION) {
    oop throwable = PENDING_EXCEPTION;
    CLEAR_PENDING_EXCEPTION;
    java_lang_Throwable::print(throwable, tty);
    tty->cr();
    java_lang_Throwable::print_stack_trace(throwable, tty);
    tty->cr();
    exit_code = 2;
  }

  if (had_error()) {
    tty->print_cr("Failed on %s", error_message());
    exit_code = 1;
  }
  return exit_code;
}

int ciCacheProfiles::replay_method(TRAPS, Method* method, int osr_bci, bool blocked) {
	char* key = get_key(method);
	char* rec = (char*) (*_compile_records_dictionary)[key];
	if(rec!=NULL) {
		if(PrintCacheProfiles) tty->print_cr("Found method %s in dictionary", key);
		return ciCacheReplay::replay_cached(THREAD, rec, osr_bci, blocked);
	}  else {
	    if(PrintCacheProfiles) tty->print_cr("Could not find method %s in dictionary.", key);
	    return 1;
	}
}

// initialize the cache profiler and parse the profile file
// to save methods in the ciCompileRecords array
void ciCacheProfiles::initialize(TRAPS) {
  if (!is_initialized()) {
    HandleMark hm;
    ResourceMark rm;
    if (FLAG_IS_DEFAULT(CacheProfilesFile)) {
      tty->print_cr("NOTE: no explicit compiler cache profiles file specified, uses -XX:CacheProfilesFile=cached_profiles.dat.");
      CacheProfilesFile = "cached_profiles.dat";
    }

    // Load and parse the replay data
    // initialize variables (these were part of the cache before)
    _thread = THREAD;

    _stream = fopen(CacheProfilesFile, "rt");
    if (_stream == NULL) {
      fprintf(stderr, "ERROR: Can't open cache profile %s\n", CacheProfilesFile);
    }

    _error_message = NULL;

    _buffer_length = 32;
    _buffer =  NEW_RESOURCE_ARRAY(char, _buffer_length);
    _bufptr = _buffer;
    _buffer_pos = 0;

    Arena* shared_type_arena = new (mtCompiler)Arena(mtCompiler);
    Arena* shared_type_arena2 = new (mtCompiler)Arena(mtCompiler);
    _compile_records = new (shared_type_arena) Dict(cmpstr, hashstr, shared_type_arena);
    _compile_records_dictionary = new (shared_type_arena2) Dict(cmpstr, hashstr, shared_type_arena2);

    if (can_replay()) {
      process_file(THREAD);
    }

    if (HAS_PENDING_EXCEPTION) {
      oop throwable = PENDING_EXCEPTION;
      CLEAR_PENDING_EXCEPTION;
      java_lang_Throwable::print(throwable, tty);
      tty->cr();
      java_lang_Throwable::print_stack_trace(throwable, tty);
      tty->cr();
    }

    if (had_error()) {
      tty->print_cr("Processing failed on %s", error_message());
    }
  }
  is_initialized(true);
  if (PrintCacheProfiles) {
    tty->print_cr("CacheProfiles: CachedProfiles initialized!");
  }
}

bool ciCacheProfiles::is_initialized() {
  return _initialized;
}

void ciCacheProfiles::is_initialized(bool flag) {
  _initialized = flag;
}

char* ciCacheProfiles::get_key(Method* method) {
	char* klass_name =  method->method_holder()->name()->as_utf8();
	char* method_name = method->name()->as_utf8();
	char* signature = method->signature()->as_utf8();
	char* key = get_key(klass_name, method_name, signature);
	return key;
}

char* ciCacheProfiles::get_key(methodHandle method) {
	char* klass_name =  method->method_holder()->name()->as_utf8();
	char* method_name = method->name()->as_utf8();
	char* signature = method->signature()->as_utf8();
	char* key = get_key(klass_name, method_name, signature);
	return key;
}

char* ciCacheProfiles::get_key(const char* klass_name, const char* method_name, const char* signature) {
	char* key = NEW_C_HEAP_ARRAY(char, (unsigned)strlen(klass_name)+(unsigned)strlen(method_name)+(unsigned)strlen(signature)+3, mtCompiler);
	strcpy(key, klass_name);
	strcpy(key+(unsigned)strlen(klass_name), "::");
	strcpy(key+(unsigned)strlen(klass_name)+2, method_name);
	strcpy(key+(unsigned)strlen(klass_name)+2+(unsigned)strlen(method_name), signature);
	return key;
}

// returns the complevel if cached, else 0
int ciCacheProfiles::is_cached(Method* method) {
	if (!is_initialized()) {
	return 0;
	}
	//VM_ENTRY_MARK;
	ASSERT_IN_VM;
	ResourceMark rm;
	char* key = get_key(method);
	char* rec = (char*) (*_compile_records)[key];
	if(rec == NULL) {
		return 0;
	} else {
		return atoi(rec);
	}
}

// same function for a method holder
int ciCacheProfiles::is_cached(methodHandle method) {
	if (!is_initialized()) {
		return 0;
	}
	//VM_ENTRY_MARK;
	ASSERT_IN_VM;
	ResourceMark rm;
	char* key = get_key(method);
	char* rec =  (char*) (*_compile_records)[key];
	if(rec == NULL) {
		return 0;
	} else {
		return atoi(rec);
	}
}
// returns the complevel if cached, else 0
int ciCacheProfiles::is_cached(char* key) {
	char* rec = (char*) (*_compile_records)[key];
	if(rec == NULL) {
		return 0;
	} else {
		return atoi(rec);
	}
}
// removes a cached profile
void ciCacheProfiles::clear_cache(Method* method) {
	char* key = get_key(method);
	_compile_records->Delete(key);
	_compile_records_dictionary->Delete(key);
}
// Process each line of the replay file and store in hashmap
void ciCacheProfiles::process_file(TRAPS) {
	int line_no = 1;
	int c = getc(_stream);
    int process_buffer_length = 1024;
    char* process_buffer = NEW_C_HEAP_ARRAY(char, process_buffer_length, mtCompiler);
    int process_buffer_pos = 0;
	while(c != EOF) {
		c = get_line(c);
		bool isCompileEntry = true;
		for(int i = 0; i < 7; i++) {
			if(_buffer[i] != _CMD_COMPILE[i]) {
				isCompileEntry = false;
				break;
			}
		}
		// now copy the content of _buffer to process_buffer and terminate with \n (newline)
		int i = 0;
		while(_buffer[i] != '\n') {
			process_buffer[process_buffer_pos++] = _buffer[i];
			i++;
			if(process_buffer_pos + 2 >= process_buffer_length) {
				// if the process buffer is out of space we need to grow it
				process_buffer_length = process_buffer_length * 2;
				// Next call will throw error in case of OOM.
				process_buffer = REALLOC_C_HEAP_ARRAY(char, process_buffer, process_buffer_length, mtCompiler);
			}
		}
		process_buffer[process_buffer_pos++] = '\n';
		// if it's a compile entry start a new dictionary entry and save the current one
		if(isCompileEntry) {
			process_buffer[process_buffer_pos++] = EOF;
			// backup the content of the process buffer
			char* value = NEW_C_HEAP_ARRAY(char, process_buffer_pos, mtCompiler);
			strncpy(value, process_buffer, process_buffer_pos);
			// first parse string is to eliminate the 'compile' keyword
			parse_string();
			const char* klass_name = parse_escaped_string();
			const char* method_name = parse_escaped_string();
			const char* signature = parse_escaped_string();
			int entry_bci = parse_int("entry_bci");
			int comp_level = parse_int("comp_level");
			// old version w/o comp_level
			if (had_error() && (error_message() == "comp_level")) {
				comp_level = CompLevel_full_optimization;
			}
			char* key = get_key(klass_name, method_name, signature);
			if(comp_level >= is_cached(key)) {
				_compile_records_dictionary->Insert(key, value, true);
				char* integer = NEW_C_HEAP_ARRAY(char, 2, mtCompiler);
				sprintf(integer, "%d", comp_level);
				_compile_records->Insert(key, integer, true);

			}

			// reset processing datastructure
		    process_buffer = REALLOC_C_HEAP_ARRAY(char, process_buffer, 1024, mtCompiler);
		    process_buffer_length = 1024;
		    process_buffer_pos = 0;
		   // FREE_C_HEAP_ARRAY(char, value);
		}
		//FREE_C_HEAP_ARRAY(char, process_buffer);
		line_no++;
	}
}
