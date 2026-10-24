// jpeglib.cpp - the jpeglib.h API implemented on vio's JPEG codec.
//
// Each JPEG object's private state (its `master` field) owns a vio
// Compressor or Decompressor.  The public parameters are copied into it when
// processing starts, and its results are copied back into the public fields.
//
// Error handling: everything that may throw (vio, and the C++ parts of this
// file) runs inside run().  Errors and warnings are queued there and
// delivered through the application's error manager only once those frames
// have unwound, so that error_exit() may longjmp() safely.  Application
// callbacks (data source and destination, progress monitor, marker
// processors) are called only from the API functions' own frames too.
//
// Input: before decoding, compressed data is gathered from the data source
// up to the end of the image (found by a light-weight marker scanner), and
// used in place when the source's buffer already holds it all.  Output: vio
// writes into its own buffer, which is copied to the destination after each
// call.
#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>

#include "vio/jpeg/decoder.hpp"
#include "vio/jpeg/encoder.hpp"

// The API headers come last: they define macros (DCTSIZE, MAX_COMPONENTS,
// ...) that would clash with vio's own constants.
#define JPEG_INTERNALS
#include "jpeglib.h"
#include "jerror.h"

#if defined(_WIN32)
#define JEXPORT
#elif defined(__GNUC__)
#define JEXPORT __attribute__((visibility("default")))
#else
#define JEXPORT
#endif

namespace vd = vio::jpeg::detail;
using vio::ColorSpace;

// ===========================================================================
// Message table
// ===========================================================================

#if JPEG_LIB_VERSION >= 80
#define JVERSION  "8d  15-Jan-2012"
#elif JPEG_LIB_VERSION >= 70
#define JVERSION  "7  27-Jun-2009"
#else
#define JVERSION  "6b  27-Mar-1998"
#endif
#define JCOPYRIGHT  "vio JPEG library (jpeglib.h interface)"

// One message beyond the standard ones: a message given as text (in
// msg_parm.s), for errors without a message code of their own.
constexpr int JMSG_TEXT = JMSG_LASTMSGCODE;

#define JMESSAGE(code, string)  string,
extern "C" JEXPORT const char* const jpeg_std_message_table[] = {
#include "jerror.h"
    "%s", nullptr};

extern "C" JEXPORT const int jpeg_natural_order[DCTSIZE2 + 16] = {
    0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,
    12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6,  7,  14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63,
    63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63};

namespace {

// A message for the error manager: code and parameters.
struct Msg {
  int code = 0;
  int level = -1;  // emit_message level (-1: warning)
  bool is_str = false;
  int i[8] = {};
  char s[JMSG_STR_PARM_MAX] = {};
};

// Thrown by this file's C++ code for errors with a known message code.
struct CodeError {
  Msg msg;
};

[[noreturn]] void fail(int code, int p0 = 0, int p1 = 0, int p2 = 0, int p3 = 0, int p4 = 0,
                       int p5 = 0) {
  CodeError e;
  e.msg.code = code;
  int p[6] = {p0, p1, p2, p3, p4, p5};
  for (int k = 0; k < 6; k++) e.msg.i[k] = p[k];
  throw e;
}

Msg code_msg(int code, int level = -1, int p0 = 0, int p1 = 0) {
  Msg m;
  m.code = code;
  m.level = level;
  m.i[0] = p0;
  m.i[1] = p1;
  return m;
}

// Matches `text` against the printf format `f`, extracting the parameters.
bool match_format(const char* f, const char* t, Msg& m) {
  int n = 0;
  while (*f) {
    if (*f != '%') {
      if (*f++ != *t++) return false;
      continue;
    }
    f++;
    if (*f == '%') {
      if (*t++ != '%') return false;
      f++;
      continue;
    }
    while (*f && std::strchr("-+ #0", *f)) f++;
    while (*f >= '0' && *f <= '9') f++;
    if (*f == 'l') f++;
    char conv = *f++;
    if (conv == 's') {
      // Only messages with a literal tail after the string are matched.
      if (std::strchr(f, '%')) return false;
      std::size_t rest = std::strlen(f), tl = std::strlen(t);
      if (tl < rest || std::strcmp(t + tl - rest, f) != 0) return false;
      std::size_t len = std::min(tl - rest, sizeof(m.s) - 1);
      std::memcpy(m.s, t, len);
      m.s[len] = 0;
      m.is_str = true;
      return true;
    }
    if (n >= 8) return false;
    if (conv == 'c') {
      if (!*t) return false;
      m.i[n++] = static_cast<unsigned char>(*t++);
      continue;
    }
    while (*t == ' ') t++;
    bool neg = false;
    if ((conv == 'd' || conv == 'i') && *t == '-') {
      neg = true;
      t++;
    }
    int base = (conv == 'x' || conv == 'X') ? 16 : 10;
    long long v = 0;
    int digits = 0;
    for (;;) {
      char c = *t;
      int d;
      if (c >= '0' && c <= '9') d = c - '0';
      else if (base == 16 && c >= 'a' && c <= 'f') d = c - 'a' + 10;
      else if (base == 16 && c >= 'A' && c <= 'F') d = c - 'A' + 10;
      else break;
      v = v * base + d;
      t++;
      if (++digits > 20) return false;
    }
    if (!digits) return false;
    m.i[n++] = static_cast<int>(neg ? -v : v);
  }
  return *t == 0;
}

// Finds the message code of a message given as text (vio's messages use the
// standard texts); otherwise returns JMSG_TEXT.
Msg msg_from_text(const char* text, int level = -1) {
  for (int code = 0; code < JMSG_TEXT; code++) {
    const char* f = jpeg_std_message_table[code];
    if (!f) continue;
    Msg m;
    if (!match_format(f, text, m)) continue;
    if (!m.is_str) {
      char buf[JMSG_LENGTH_MAX];
      std::snprintf(buf, sizeof(buf), f, m.i[0], m.i[1], m.i[2], m.i[3], m.i[4], m.i[5],
                    m.i[6], m.i[7]);
      if (std::strcmp(buf, text) != 0) continue;
    }
    m.code = code;
    m.level = level;
    return m;
  }
  Msg m;
  m.code = JMSG_TEXT;
  m.level = level;
  m.is_str = true;
  std::size_t len = std::min(std::strlen(text), sizeof(m.s) - 1);
  std::memcpy(m.s, text, len);
  m.s[len] = 0;
  return m;
}

void set_err(jpeg_error_mgr* err, const Msg& m) {
  err->msg_code = m.code;
  if (m.is_str) std::memcpy(err->msg_parm.s, m.s, sizeof(m.s));
  else std::memcpy(err->msg_parm.i, m.i, sizeof(m.i));
}

}  // namespace

// ===========================================================================
// Private state
// ===========================================================================

// State shared by compression and decompression objects: the queue of
// messages produced inside run().
struct PrivateBase {
  std::vector<Msg> msgs;
  std::size_t next_msg = 0;
  bool failed = false;
  Msg error;
  std::string last_text;  // full text of the last JMSG_TEXT message

  void warn(const std::string& text) {
    msgs.push_back(msg_from_text(text.c_str()));
    if (msgs.back().code == JMSG_TEXT) last_text = text;
  }
  void warn(int code, int p0 = 0, int p1 = 0) { msgs.push_back(code_msg(code, -1, p0, p1)); }
};

namespace {

// Incremental scanner of a JPEG datastream's marker structure: finds the end
// of the first SOS header and the EOI marker without decoding.
struct Scanner {
  enum State { SOI1, SOI2, MARKER, MARKER_FF, LEN1, LEN2, SKIP, ENTROPY, ENTROPY_FF, DONE };
  State state = SOI1;
  bool not_jpeg = false;
  bool header_done = false;  // SOS header (or EOI) seen
  int marker = 0;
  std::size_t skip = 0;
  unsigned len = 0;

  bool done() const { return state == DONE; }

  void marker_code(int c) {
    if (c == 0xD9) {
      state = DONE;
      header_done = true;
    } else if ((c >= 0xD0 && c <= 0xD8) || c == 0x01) {
      state = MARKER;
    } else {
      marker = c;
      state = LEN1;
    }
  }

  void end_segment() {
    if (marker == 0xDA) {
      header_done = true;
      state = ENTROPY;
    } else {
      state = MARKER;
    }
  }

  // Scans up to n bytes; stops right after EOI.  Returns the bytes used.
  std::size_t feed(const std::uint8_t* p, std::size_t n) {
    std::size_t i = 0;
    while (i < n && state != DONE) {
      switch (state) {
        case SOI1:
          not_jpeg = p[i++] != 0xFF;
          state = SOI2;
          break;
        case SOI2:
          if (not_jpeg || p[i] != 0xD8) {
            state = DONE;
            header_done = true;
          } else {
            state = MARKER;
          }
          i++;
          break;
        case MARKER:
          if (p[i++] == 0xFF) state = MARKER_FF;
          break;
        case MARKER_FF: {
          int c = p[i++];
          if (c == 0xFF) break;
          if (c == 0) state = MARKER;
          else marker_code(c);
          break;
        }
        case LEN1:
          len = static_cast<unsigned>(p[i++]) << 8;
          state = LEN2;
          break;
        case LEN2:
          len |= p[i++];
          skip = len >= 2 ? len - 2 : 0;
          if (skip == 0) end_segment();
          else state = SKIP;
          break;
        case SKIP: {
          std::size_t k = std::min(skip, n - i);
          i += k;
          skip -= k;
          if (skip == 0) end_segment();
          break;
        }
        case ENTROPY: {
          const void* ff = std::memchr(p + i, 0xFF, n - i);
          if (!ff) {
            i = n;
          } else {
            i = static_cast<std::size_t>(static_cast<const std::uint8_t*>(ff) - p) + 1;
            state = ENTROPY_FF;
          }
          break;
        }
        case ENTROPY_FF: {
          int c = p[i++];
          if (c == 0xFF) break;
          if (c == 0 || (c >= 0xD0 && c <= 0xD7)) state = ENTROPY;
          else marker_code(c);
          break;
        }
        case DONE:
          break;
      }
    }
    return i;
  }
};

}  // namespace

struct jpeg_comp_master : PrivateBase {
  vd::Compressor c;
  bool lossless = false;
  // Compressed data the destination has not accepted yet (it suspended).
  std::vector<std::uint8_t> pending;
  std::size_t pending_pos = 0;
  std::vector<vd::BlockArray> coef_views;

  jpeg_comp_master() {
    c.diag.on_warning = [this](const std::string& s) { warn(s); };
  }
};

struct jpeg_decomp_master : PrivateBase {
  vd::Decompressor d;

  // Compressed input of the current image
  Scanner scan;
  std::vector<std::uint8_t> buf;   // gathered data (copy mode)
  const JOCTET* data = nullptr;    // data handed to the decoder
  std::size_t size = 0;
  bool zero_copy = false;          // data lies in the source's buffer
  const JOCTET* zc_base = nullptr; // source buffer (zero-copy mode)
  std::size_t zc_avail = 0;
  bool input_done = false;         // all of the image gathered
  bool source_set = false;         // decoder attached to `data`

  // Marker processors installed by jpeg_set_marker_processor(): APP0..15, COM
  jpeg_marker_parser_method processors[17] = {};
  jpeg_source_mgr marker_src{};
  jpeg_source_mgr* saved_src = nullptr;

  // Public structures derived from the decoder state
  std::size_t markers_stored = 0;
  jpeg_saved_marker_ptr marker_tail = nullptr;
  JSAMPARRAY colormap_ours = nullptr;
  jvirt_barray_ptr* coef_arrays = nullptr;
  bool lossless = false;

  jpeg_decomp_master() {
    d.diag.on_warning = [this](const std::string& s) { warn(s); };
  }

  void reset_input() {
    scan = Scanner();
    buf.clear();
    data = nullptr;
    size = 0;
    zero_copy = false;
    zc_base = nullptr;
    zc_avail = 0;
    input_done = false;
    source_set = false;
  }
};

namespace {


PrivateBase* priv(j_common_ptr cinfo) {
  if (cinfo->is_decompressor) return reinterpret_cast<j_decompress_ptr>(cinfo)->master;
  return reinterpret_cast<j_compress_ptr>(cinfo)->master;
}

// Runs f, catching any exception; the error (and any warnings) are left in
// the queue for deliver().
template <class F>
bool run(PrivateBase* p, F&& f) noexcept {
  p->msgs.clear();
  p->next_msg = 0;
  p->failed = false;
  try {
    f();
    return true;
  } catch (const CodeError& e) {
    p->error = e.msg;
  } catch (const vio::Error& e) {
    p->error = msg_from_text(e.what());
    if (p->error.code == JMSG_TEXT) {
      try {
        p->last_text = e.what();
      } catch (...) {
      }
    }
  } catch (const std::bad_alloc&) {
    p->error = code_msg(JERR_OUT_OF_MEMORY, -1, 0);
  } catch (const std::exception& e) {
    p->error = msg_from_text(e.what());
  } catch (...) {
    p->error = msg_from_text("Unknown error");
  }
  p->failed = true;
  return false;
}

// Emits queued warnings, then raises the queued error, if any.  Called from
// the API functions' frames only.
void deliver(j_common_ptr cinfo, PrivateBase* p) {
  while (p->next_msg < p->msgs.size()) {
    const Msg& m = p->msgs[p->next_msg++];
    set_err(cinfo->err, m);
    (*cinfo->err->emit_message)(cinfo, m.level);
  }
  p->msgs.clear();
  p->next_msg = 0;
  if (p->failed) {
    p->failed = false;
    set_err(cinfo->err, p->error);
    (*cinfo->err->error_exit)(cinfo);
  }
}

template <class F>
bool call(j_common_ptr cinfo, F&& f) {
  PrivateBase* p = priv(cinfo);
  bool ok = run(p, f);
  deliver(cinfo, p);
  return ok;
}

#define CALL(cinfo, ...) call(reinterpret_cast<j_common_ptr>(cinfo), [&] { __VA_ARGS__; })

}  // namespace

// ===========================================================================
// Error manager
// ===========================================================================

namespace {

void error_exit(j_common_ptr cinfo) {
  (*cinfo->err->output_message)(cinfo);
  jpeg_destroy(cinfo);
  std::exit(EXIT_FAILURE);
}

void output_message(j_common_ptr cinfo) {
  char buffer[JMSG_LENGTH_MAX];
  (*cinfo->err->format_message)(cinfo, buffer);
  std::fprintf(stderr, "%s\n", buffer);
}

void emit_message(j_common_ptr cinfo, int msg_level) {
  jpeg_error_mgr* err = cinfo->err;
  if (msg_level < 0) {
    // Show the first warning (or all with trace_level >= 3); count them all
    if (err->num_warnings == 0 || err->trace_level >= 3) (*err->output_message)(cinfo);
    err->num_warnings++;
  } else if (err->trace_level >= msg_level) {
    (*err->output_message)(cinfo);
  }
}

void format_message(j_common_ptr cinfo, char* buffer) {
  jpeg_error_mgr* err = cinfo->err;
  int msg_code = err->msg_code;
  const char* msgtext = nullptr;
  if (msg_code > 0 && msg_code <= err->last_jpeg_message) {
    msgtext = err->jpeg_message_table[msg_code];
  } else if (err->addon_message_table && msg_code >= err->first_addon_message &&
             msg_code <= err->last_addon_message) {
    msgtext = err->addon_message_table[msg_code - err->first_addon_message];
  }
  if (!msgtext) {
    err->msg_parm.i[0] = msg_code;
    msgtext = err->jpeg_message_table[0];
  }
  // The full text of messages given as text, when known
  if (msg_code == JMSG_TEXT && err->jpeg_message_table == jpeg_std_message_table &&
      cinfo->global_state != 0 && cinfo->mem) {
    PrivateBase* p = priv(cinfo);
    if (p && !p->last_text.empty() &&
        std::strncmp(p->last_text.c_str(), err->msg_parm.s, JMSG_STR_PARM_MAX - 1) == 0) {
      std::snprintf(buffer, JMSG_LENGTH_MAX, "%s", p->last_text.c_str());
      return;
    }
  }
  // A "%s" in the message selects the string parameter
  bool isstring = false;
  for (const char* q = msgtext; *q; q++)
    if (q[0] == '%') {
      if (q[1] == 's') isstring = true;
      break;
    }
  if (isstring)
    std::snprintf(buffer, JMSG_LENGTH_MAX, msgtext, err->msg_parm.s);
  else
    std::snprintf(buffer, JMSG_LENGTH_MAX, msgtext, err->msg_parm.i[0], err->msg_parm.i[1],
                  err->msg_parm.i[2], err->msg_parm.i[3], err->msg_parm.i[4],
                  err->msg_parm.i[5], err->msg_parm.i[6], err->msg_parm.i[7]);
}

void reset_error_mgr(j_common_ptr cinfo) {
  cinfo->err->num_warnings = 0;
  cinfo->err->msg_code = 0;
}

}  // namespace

extern "C" JEXPORT jpeg_error_mgr* jpeg_std_error(jpeg_error_mgr* err) {
  std::memset(err, 0, sizeof(*err));
  err->error_exit = error_exit;
  err->emit_message = emit_message;
  err->output_message = output_message;
  err->format_message = format_message;
  err->reset_error_mgr = reset_error_mgr;
  err->trace_level = 0;
  err->num_warnings = 0;
  err->msg_code = 0;
  err->jpeg_message_table = jpeg_std_message_table;
  err->last_jpeg_message = JMSG_TEXT;
  err->addon_message_table = nullptr;
  err->first_addon_message = 0;
  err->last_addon_message = 0;
  return err;
}

// ===========================================================================
// Memory manager
// ===========================================================================

namespace {

constexpr std::size_t kMaxAllocChunk = 1000000000L;
constexpr std::size_t kAlign = 32;

// Header in front of each allocation.
struct alignas(kAlign) Chunk {
  Chunk* next;
  void* raw;
  std::size_t size;
};

struct MemMgr {
  jpeg_memory_mgr pub;
  Chunk* pools[JPOOL_NUMPOOLS];
  jvirt_sarray_control* sarrays;
  jvirt_barray_control* barrays;
  std::size_t total;
};

MemMgr* memmgr(j_common_ptr cinfo) { return reinterpret_cast<MemMgr*>(cinfo->mem); }

int data_precision(j_common_ptr cinfo) {
  return cinfo->is_decompressor ? reinterpret_cast<j_decompress_ptr>(cinfo)->data_precision
                                : reinterpret_cast<j_compress_ptr>(cinfo)->data_precision;
}

std::size_t sample_size(j_common_ptr cinfo) {
  int p = data_precision(cinfo);
  return p > 12 ? sizeof(J16SAMPLE) : p > 8 ? sizeof(J12SAMPLE) : sizeof(JSAMPLE);
}

std::size_t round_up(std::size_t a, std::size_t b) { return (a + b - 1) / b * b; }

// Allocates from a pool; returns null and sets `which` (the error case
// number) on failure.
void* pool_alloc(j_common_ptr cinfo, int pool_id, std::size_t size, bool large, int& which) {
  MemMgr* mem = memmgr(cinfo);
  if (size > kMaxAllocChunk) {
    which = large ? 8 : 7;
    return nullptr;
  }
  size = round_up(std::max<std::size_t>(size, 1), kAlign);
  if (size + sizeof(Chunk) + kAlign - 1 > kMaxAllocChunk) {
    which = large ? 3 : 1;
    return nullptr;
  }
  if (pool_id < 0 || pool_id >= JPOOL_NUMPOOLS) {
    which = -1;
    return nullptr;
  }
  void* raw = std::malloc(size + sizeof(Chunk) + kAlign);
  if (!raw) {
    which = large ? 4 : 2;
    return nullptr;
  }
  auto a = (reinterpret_cast<std::uintptr_t>(raw) + sizeof(Chunk) + kAlign - 1) &
           ~static_cast<std::uintptr_t>(kAlign - 1);
  Chunk* h = reinterpret_cast<Chunk*>(a - sizeof(Chunk));
  h->raw = raw;
  h->size = size;
  h->next = mem->pools[pool_id];
  mem->pools[pool_id] = h;
  mem->total += size + sizeof(Chunk) + kAlign;
  return reinterpret_cast<void*>(a);
}

[[noreturn]] void raise_alloc(j_common_ptr cinfo, int pool_id, int which) {
  if (which < 0) ERREXIT1(cinfo, JERR_BAD_POOL_ID, pool_id);
  else ERREXIT1(cinfo, JERR_OUT_OF_MEMORY, which);
  std::abort();  // error_exit returned
}

void* alloc_small(j_common_ptr cinfo, int pool_id, std::size_t size) {
  int which = 0;
  void* p = pool_alloc(cinfo, pool_id, size, false, which);
  if (!p) raise_alloc(cinfo, pool_id, which);
  return p;
}

void* alloc_large(j_common_ptr cinfo, int pool_id, std::size_t size) {
  int which = 0;
  void* p = pool_alloc(cinfo, pool_id, size, true, which);
  if (!p) raise_alloc(cinfo, pool_id, which);
  return p;
}

// For this file's C++ code: throws instead of calling error_exit.
void* xalloc(j_common_ptr cinfo, int pool_id, std::size_t size) {
  int which = 0;
  void* p = pool_alloc(cinfo, pool_id, size, true, which);
  if (!p) {
    if (which < 0) fail(JERR_BAD_POOL_ID, pool_id);
    fail(JERR_OUT_OF_MEMORY, which);
  }
  return p;
}

// Rows of `elemsize`-byte elements; rows are padded to 64 bytes.
void** alloc_rows(j_common_ptr cinfo, int pool_id, std::size_t elemsize, JDIMENSION perrow,
                  JDIMENSION numrows, bool throws) {
  std::size_t rowbytes = round_up(static_cast<std::size_t>(perrow) * elemsize, 2 * kAlign);
  if (rowbytes == 0) rowbytes = 2 * kAlign;
  std::size_t rowsperchunk = (kMaxAllocChunk - sizeof(Chunk)) / rowbytes;
  if (rowsperchunk == 0) {
    if (throws) fail(JERR_WIDTH_OVERFLOW);
    ERREXIT(cinfo, JERR_WIDTH_OVERFLOW);
  }
  auto alloc = [&](std::size_t n) {
    return throws ? xalloc(cinfo, pool_id, n) : alloc_large(cinfo, pool_id, n);
  };
  void** result = static_cast<void**>(alloc(std::max<JDIMENSION>(numrows, 1) * sizeof(void*)));
  JDIMENSION currow = 0;
  while (currow < numrows) {
    std::size_t n = std::min<std::size_t>(rowsperchunk, numrows - currow);
    char* work = static_cast<char*>(alloc(n * rowbytes));
    for (std::size_t i = 0; i < n; i++) {
      result[currow++] = work;
      work += rowbytes;
    }
  }
  return result;
}

JSAMPARRAY alloc_sarray(j_common_ptr cinfo, int pool_id, JDIMENSION samplesperrow,
                        JDIMENSION numrows) {
  return reinterpret_cast<JSAMPARRAY>(
      alloc_rows(cinfo, pool_id, sample_size(cinfo), samplesperrow, numrows, false));
}

JBLOCKARRAY alloc_barray(j_common_ptr cinfo, int pool_id, JDIMENSION blocksperrow,
                         JDIMENSION numrows) {
  return reinterpret_cast<JBLOCKARRAY>(
      alloc_rows(cinfo, pool_id, sizeof(JBLOCK), blocksperrow, numrows, false));
}

}  // namespace

// Virtual arrays are kept entirely in memory.
struct jvirt_sarray_control {
  JSAMPARRAY rows;  // null until realized
  JDIMENSION rows_in_array, samplesperrow, maxaccess, first_undef_row;
  std::size_t elemsize;
  boolean pre_zero;
  jvirt_sarray_control* next;
};

struct jvirt_barray_control {
  JBLOCKARRAY rows;  // null until realized
  JBLOCKROW base;    // contiguous storage of the rows
  JDIMENSION rows_in_array, blocksperrow, maxaccess, first_undef_row;
  boolean pre_zero;
  jvirt_barray_control* next;
};

namespace {

jvirt_sarray_ptr request_virt_sarray(j_common_ptr cinfo, int pool_id, boolean pre_zero,
                                     JDIMENSION samplesperrow, JDIMENSION numrows,
                                     JDIMENSION maxaccess) {
  MemMgr* mem = memmgr(cinfo);
  if (pool_id != JPOOL_IMAGE) ERREXIT1(cinfo, JERR_BAD_POOL_ID, pool_id);
  auto* r = static_cast<jvirt_sarray_ptr>(alloc_small(cinfo, pool_id, sizeof(jvirt_sarray_control)));
  r->rows = nullptr;
  r->rows_in_array = numrows;
  r->samplesperrow = samplesperrow;
  r->maxaccess = maxaccess;
  r->first_undef_row = 0;
  r->elemsize = 0;
  r->pre_zero = pre_zero;
  r->next = mem->sarrays;
  mem->sarrays = r;
  return r;
}

jvirt_barray_ptr request_virt_barray(j_common_ptr cinfo, int pool_id, boolean pre_zero,
                                     JDIMENSION blocksperrow, JDIMENSION numrows,
                                     JDIMENSION maxaccess) {
  MemMgr* mem = memmgr(cinfo);
  if (pool_id != JPOOL_IMAGE) ERREXIT1(cinfo, JERR_BAD_POOL_ID, pool_id);
  auto* r = static_cast<jvirt_barray_ptr>(alloc_small(cinfo, pool_id, sizeof(jvirt_barray_control)));
  r->rows = nullptr;
  r->base = nullptr;
  r->rows_in_array = numrows;
  r->blocksperrow = blocksperrow;
  r->maxaccess = maxaccess;
  r->first_undef_row = 0;
  r->pre_zero = pre_zero;
  r->next = mem->barrays;
  mem->barrays = r;
  return r;
}

void realize_virt_arrays(j_common_ptr cinfo) {
  MemMgr* mem = memmgr(cinfo);
  std::size_t ssize = sample_size(cinfo);
  // Without backing store, the memory limit applies to the whole arrays.
  std::size_t needed = 0;
  for (jvirt_sarray_ptr s = mem->sarrays; s; s = s->next)
    if (!s->rows) needed += static_cast<std::size_t>(s->rows_in_array) * s->samplesperrow * ssize;
  for (jvirt_barray_ptr b = mem->barrays; b; b = b->next)
    if (!b->rows)
      needed += static_cast<std::size_t>(b->rows_in_array) * b->blocksperrow * sizeof(JBLOCK);
  if (needed == 0) return;
  long limit = mem->pub.max_memory_to_use;
  if (limit > 0 && mem->total + needed > static_cast<std::size_t>(limit))
    ERREXIT(cinfo, JERR_NO_BACKING_STORE);
  for (jvirt_sarray_ptr s = mem->sarrays; s; s = s->next) {
    if (s->rows) continue;
    s->elemsize = ssize;
    s->rows = reinterpret_cast<JSAMPARRAY>(
        alloc_rows(cinfo, JPOOL_IMAGE, ssize, s->samplesperrow, s->rows_in_array, false));
  }
  for (jvirt_barray_ptr b = mem->barrays; b; b = b->next) {
    if (b->rows) continue;
    // One contiguous block, so that the encoder can read the rows directly.
    std::size_t n = static_cast<std::size_t>(b->rows_in_array) * b->blocksperrow;
    b->base = static_cast<JBLOCKROW>(alloc_large(cinfo, JPOOL_IMAGE, std::max<std::size_t>(n, 1) * sizeof(JBLOCK)));
    b->rows = static_cast<JBLOCKARRAY>(
        alloc_small(cinfo, JPOOL_IMAGE, std::max<JDIMENSION>(b->rows_in_array, 1) * sizeof(JBLOCKROW)));
    for (JDIMENSION r = 0; r < b->rows_in_array; r++) b->rows[r] = b->base + static_cast<std::size_t>(r) * b->blocksperrow;
  }
}

template <class Ctl>
void check_access(j_common_ptr cinfo, Ctl* ptr, JDIMENSION start_row, JDIMENSION num_rows,
                  boolean writable, std::size_t rowbytes, void* const* rows) {
  JDIMENSION end_row = start_row + num_rows;
  if (end_row > ptr->rows_in_array || num_rows > ptr->maxaccess || !ptr->rows)
    ERREXIT(cinfo, JERR_BAD_VIRTUAL_ACCESS);
  if (ptr->first_undef_row < end_row) {
    JDIMENSION undef_row;
    if (ptr->first_undef_row < start_row) {
      if (writable) ERREXIT(cinfo, JERR_BAD_VIRTUAL_ACCESS);
      undef_row = start_row;
    } else {
      undef_row = ptr->first_undef_row;
    }
    if (writable) ptr->first_undef_row = end_row;
    if (ptr->pre_zero) {
      for (JDIMENSION r = undef_row; r < end_row; r++) std::memset(rows[r], 0, rowbytes);
    } else if (!writable) {
      ERREXIT(cinfo, JERR_BAD_VIRTUAL_ACCESS);
    }
  }
}

JSAMPARRAY access_virt_sarray(j_common_ptr cinfo, jvirt_sarray_ptr ptr, JDIMENSION start_row,
                              JDIMENSION num_rows, boolean writable) {
  check_access(cinfo, ptr, start_row, num_rows, writable, ptr->samplesperrow * ptr->elemsize,
               reinterpret_cast<void* const*>(ptr->rows));
  return ptr->rows + start_row;
}

JBLOCKARRAY access_virt_barray(j_common_ptr cinfo, jvirt_barray_ptr ptr, JDIMENSION start_row,
                               JDIMENSION num_rows, boolean writable) {
  check_access(cinfo, ptr, start_row, num_rows, writable, ptr->blocksperrow * sizeof(JBLOCK),
               reinterpret_cast<void* const*>(ptr->rows));
  return ptr->rows + start_row;
}

void free_pool(j_common_ptr cinfo, int pool_id) {
  MemMgr* mem = memmgr(cinfo);
  if (pool_id < 0 || pool_id >= JPOOL_NUMPOOLS) ERREXIT1(cinfo, JERR_BAD_POOL_ID, pool_id);
  if (pool_id == JPOOL_IMAGE) {
    mem->sarrays = nullptr;
    mem->barrays = nullptr;
  }
  Chunk* h = mem->pools[pool_id];
  mem->pools[pool_id] = nullptr;
  while (h) {
    Chunk* next = h->next;
    mem->total -= h->size + sizeof(Chunk) + kAlign;
    std::free(h->raw);
    h = next;
  }
}

void self_destruct(j_common_ptr cinfo) {
  for (int pool = JPOOL_NUMPOOLS - 1; pool >= JPOOL_PERMANENT; pool--) free_pool(cinfo, pool);
  if (cinfo->is_decompressor) {
    auto* d = reinterpret_cast<j_decompress_ptr>(cinfo);
    delete d->master;
    d->master = nullptr;
  } else {
    auto* c = reinterpret_cast<j_compress_ptr>(cinfo);
    delete c->master;
    c->master = nullptr;
  }
  std::free(cinfo->mem);
  cinfo->mem = nullptr;
}

void init_memory_mgr(j_common_ptr cinfo) {
  cinfo->mem = nullptr;
  auto* mem = static_cast<MemMgr*>(std::malloc(sizeof(MemMgr)));
  if (!mem) ERREXIT1(cinfo, JERR_OUT_OF_MEMORY, 0);
  std::memset(mem, 0, sizeof(*mem));
  mem->pub.alloc_small = alloc_small;
  mem->pub.alloc_large = alloc_large;
  mem->pub.alloc_sarray = alloc_sarray;
  mem->pub.alloc_barray = alloc_barray;
  mem->pub.request_virt_sarray = request_virt_sarray;
  mem->pub.request_virt_barray = request_virt_barray;
  mem->pub.realize_virt_arrays = realize_virt_arrays;
  mem->pub.access_virt_sarray = access_virt_sarray;
  mem->pub.access_virt_barray = access_virt_barray;
  mem->pub.free_pool = free_pool;
  mem->pub.self_destruct = self_destruct;
  mem->pub.max_alloc_chunk = static_cast<long>(kMaxAllocChunk);
  mem->pub.max_memory_to_use = 0;
  mem->total = sizeof(MemMgr);
  cinfo->mem = &mem->pub;
  // JPEGMEM=<n>[m]: memory limit in kilobytes (or megabytes)
  if (const char* env = std::getenv("JPEGMEM")) {
    long max_to_use = 0;
    char ch = 'x';
    if (std::sscanf(env, "%ld%c", &max_to_use, &ch) > 0) {
      if (ch == 'm' || ch == 'M') max_to_use *= 1000L;
      mem->pub.max_memory_to_use = max_to_use * 1000L;
    }
  }
}

}  // namespace

// ===========================================================================
// Utilities
// ===========================================================================

extern "C" {

JEXPORT long jdiv_round_up(long a, long b) { return (a + b - 1L) / b; }

JEXPORT long jround_up(long a, long b) {
  a += b - 1L;
  return a - (a % b);
}

JEXPORT void jcopy_sample_rows(JSAMPARRAY input_array, int source_row, JSAMPARRAY output_array,
                               int dest_row, int num_rows, JDIMENSION num_cols) {
  for (int i = 0; i < num_rows; i++)
    std::memcpy(output_array[dest_row + i], input_array[source_row + i], num_cols);
}

JEXPORT void j12copy_sample_rows(J12SAMPARRAY input_array, int source_row,
                                 J12SAMPARRAY output_array, int dest_row, int num_rows,
                                 JDIMENSION num_cols) {
  for (int i = 0; i < num_rows; i++)
    std::memcpy(output_array[dest_row + i], input_array[source_row + i],
                num_cols * sizeof(J12SAMPLE));
}

JEXPORT void j16copy_sample_rows(J16SAMPARRAY input_array, int source_row,
                                 J16SAMPARRAY output_array, int dest_row, int num_rows,
                                 JDIMENSION num_cols) {
  for (int i = 0; i < num_rows; i++)
    std::memcpy(output_array[dest_row + i], input_array[source_row + i],
                num_cols * sizeof(J16SAMPLE));
}

JEXPORT void jcopy_block_row(JBLOCKROW input_row, JBLOCKROW output_row, JDIMENSION num_blocks) {
  std::memcpy(output_row, input_row, num_blocks * (DCTSIZE2 * sizeof(JCOEF)));
}

JEXPORT void jzero_far(void* target, size_t bytestozero) { std::memset(target, 0, bytestozero); }

}  // extern "C"

// ===========================================================================
// Common object operations
// ===========================================================================

namespace {

void abort_decompress_state(j_decompress_ptr cinfo) {
  jpeg_decomp_master* m = cinfo->master;
  if (!m) return;
  if (m->saved_src) {
    cinfo->src = m->saved_src;
    m->saved_src = nullptr;
  }
  m->d.abort();
  m->reset_input();
  m->markers_stored = 0;
  m->marker_tail = nullptr;
  if (cinfo->colormap == m->colormap_ours) cinfo->colormap = nullptr;
  m->colormap_ours = nullptr;
  m->coef_arrays = nullptr;
  cinfo->marker_list = nullptr;
  cinfo->comp_info = nullptr;
  cinfo->sample_range_limit = nullptr;
  cinfo->coef_bits = nullptr;
  for (int ci = 0; ci < MAX_COMPS_IN_SCAN; ci++) cinfo->cur_comp_info[ci] = nullptr;
}

void abort_compress_state(j_compress_ptr cinfo) {
  jpeg_comp_master* m = cinfo->master;
  if (!m) return;
  m->c.abort();
  m->pending.clear();
  m->pending_pos = 0;
  m->coef_views.clear();
}

}  // namespace

extern "C" {

JEXPORT void jpeg_abort(j_common_ptr cinfo) {
  if (!cinfo->mem) return;
  if (cinfo->is_decompressor) abort_decompress_state(reinterpret_cast<j_decompress_ptr>(cinfo));
  else abort_compress_state(reinterpret_cast<j_compress_ptr>(cinfo));
  for (int pool = JPOOL_NUMPOOLS - 1; pool > JPOOL_PERMANENT; pool--)
    (*cinfo->mem->free_pool)(cinfo, pool);
  cinfo->global_state = cinfo->is_decompressor ? DSTATE_START : CSTATE_START;
}

JEXPORT void jpeg_destroy(j_common_ptr cinfo) {
  if (cinfo->mem) (*cinfo->mem->self_destruct)(cinfo);
  cinfo->mem = nullptr;
  cinfo->global_state = 0;
}

JEXPORT JQUANT_TBL* jpeg_alloc_quant_table(j_common_ptr cinfo) {
  auto* tbl = static_cast<JQUANT_TBL*>(
      (*cinfo->mem->alloc_small)(cinfo, JPOOL_PERMANENT, sizeof(JQUANT_TBL)));
  std::memset(tbl, 0, sizeof(*tbl));
  tbl->sent_table = FALSE;
  return tbl;
}

JEXPORT JHUFF_TBL* jpeg_alloc_huff_table(j_common_ptr cinfo) {
  auto* tbl = static_cast<JHUFF_TBL*>(
      (*cinfo->mem->alloc_small)(cinfo, JPOOL_PERMANENT, sizeof(JHUFF_TBL)));
  std::memset(tbl, 0, sizeof(*tbl));
  tbl->sent_table = FALSE;
  return tbl;
}

}  // extern "C"

// ===========================================================================
// Data destinations
// ===========================================================================

namespace {

constexpr std::size_t kOutputBufSize = 4096;
constexpr std::size_t kInputBufSize = 4096;

struct StdioDest {
  jpeg_destination_mgr pub;
  FILE* outfile;
  JOCTET* buffer;
};

void init_stdio_destination(j_compress_ptr cinfo) {
  auto* dest = reinterpret_cast<StdioDest*>(cinfo->dest);
  dest->buffer = static_cast<JOCTET*>((*cinfo->mem->alloc_small)(
      reinterpret_cast<j_common_ptr>(cinfo), JPOOL_IMAGE, kOutputBufSize));
  dest->pub.next_output_byte = dest->buffer;
  dest->pub.free_in_buffer = kOutputBufSize;
}

boolean empty_stdio_output_buffer(j_compress_ptr cinfo) {
  auto* dest = reinterpret_cast<StdioDest*>(cinfo->dest);
  if (std::fwrite(dest->buffer, 1, kOutputBufSize, dest->outfile) != kOutputBufSize)
    ERREXIT(cinfo, JERR_FILE_WRITE);
  dest->pub.next_output_byte = dest->buffer;
  dest->pub.free_in_buffer = kOutputBufSize;
  return TRUE;
}

void term_stdio_destination(j_compress_ptr cinfo) {
  auto* dest = reinterpret_cast<StdioDest*>(cinfo->dest);
  std::size_t datacount = kOutputBufSize - dest->pub.free_in_buffer;
  if (datacount > 0 && std::fwrite(dest->buffer, 1, datacount, dest->outfile) != datacount)
    ERREXIT(cinfo, JERR_FILE_WRITE);
  std::fflush(dest->outfile);
  if (std::ferror(dest->outfile)) ERREXIT(cinfo, JERR_FILE_WRITE);
}

struct MemDest {
  jpeg_destination_mgr pub;
  unsigned char** outbuffer;
  unsigned long* outsize;
  unsigned char* newbuffer;
  JOCTET* buffer;
  std::size_t bufsize;
};

void init_mem_destination(j_compress_ptr) {}

boolean empty_mem_output_buffer(j_compress_ptr cinfo) {
  auto* dest = reinterpret_cast<MemDest*>(cinfo->dest);
  std::size_t nextsize = dest->bufsize * 2;
  auto* nextbuffer = static_cast<JOCTET*>(std::malloc(nextsize));
  if (!nextbuffer) ERREXIT1(cinfo, JERR_OUT_OF_MEMORY, 10);
  std::memcpy(nextbuffer, dest->buffer, dest->bufsize);
  std::free(dest->newbuffer);
  dest->newbuffer = nextbuffer;
  dest->pub.next_output_byte = nextbuffer + dest->bufsize;
  dest->pub.free_in_buffer = dest->bufsize;
  dest->buffer = nextbuffer;
  dest->bufsize = nextsize;
  return TRUE;
}

void term_mem_destination(j_compress_ptr cinfo) {
  auto* dest = reinterpret_cast<MemDest*>(cinfo->dest);
  *dest->outbuffer = dest->buffer;
  *dest->outsize = static_cast<unsigned long>(dest->bufsize - dest->pub.free_in_buffer);
}

}  // namespace

extern "C" {

JEXPORT void jpeg_stdio_dest(j_compress_ptr cinfo, FILE* outfile) {
  if (!cinfo->dest) {
    cinfo->dest = static_cast<jpeg_destination_mgr*>((*cinfo->mem->alloc_small)(
        reinterpret_cast<j_common_ptr>(cinfo), JPOOL_PERMANENT, sizeof(StdioDest)));
  } else if (cinfo->dest->init_destination != init_stdio_destination) {
    // A destination of another kind would be mistaken for this one.
    ERREXIT(cinfo, JERR_BUFFER_SIZE);
  }
  auto* dest = reinterpret_cast<StdioDest*>(cinfo->dest);
  dest->pub.init_destination = init_stdio_destination;
  dest->pub.empty_output_buffer = empty_stdio_output_buffer;
  dest->pub.term_destination = term_stdio_destination;
  dest->outfile = outfile;
}

JEXPORT void jpeg_mem_dest(j_compress_ptr cinfo, unsigned char** outbuffer,
                           unsigned long* outsize) {
  if (!outbuffer || !outsize) ERREXIT(cinfo, JERR_BUFFER_SIZE);
  if (!cinfo->dest) {
    cinfo->dest = static_cast<jpeg_destination_mgr*>((*cinfo->mem->alloc_small)(
        reinterpret_cast<j_common_ptr>(cinfo), JPOOL_PERMANENT, sizeof(MemDest)));
  } else if (cinfo->dest->init_destination != init_mem_destination) {
    ERREXIT(cinfo, JERR_BUFFER_SIZE);
  }
  auto* dest = reinterpret_cast<MemDest*>(cinfo->dest);
  dest->pub.init_destination = init_mem_destination;
  dest->pub.empty_output_buffer = empty_mem_output_buffer;
  dest->pub.term_destination = term_mem_destination;
  dest->outbuffer = outbuffer;
  dest->outsize = outsize;
  dest->newbuffer = nullptr;
  if (!*outbuffer || *outsize == 0) {
    dest->newbuffer = *outbuffer = static_cast<unsigned char*>(std::malloc(kOutputBufSize));
    if (!dest->newbuffer) ERREXIT1(cinfo, JERR_OUT_OF_MEMORY, 10);
    *outsize = kOutputBufSize;
  }
  dest->pub.next_output_byte = dest->buffer = *outbuffer;
  dest->pub.free_in_buffer = dest->bufsize = *outsize;
}

}  // extern "C"

// ===========================================================================
// Data sources
// ===========================================================================

namespace {

const JOCTET kFakeEOI[2] = {0xFF, JPEG_EOI};

struct StdioSource {
  jpeg_source_mgr pub;
  FILE* infile;
  JOCTET* buffer;
  boolean start_of_file;
};

void init_stdio_source(j_decompress_ptr cinfo) {
  reinterpret_cast<StdioSource*>(cinfo->src)->start_of_file = TRUE;
}

boolean fill_stdio_input_buffer(j_decompress_ptr cinfo) {
  auto* src = reinterpret_cast<StdioSource*>(cinfo->src);
  std::size_t nbytes = std::fread(src->buffer, 1, kInputBufSize, src->infile);
  if (nbytes == 0) {
    if (src->start_of_file) ERREXIT(cinfo, JERR_INPUT_EMPTY);
    WARNMS(cinfo, JWRN_JPEG_EOF);
    // Insert a fake EOI marker
    src->buffer[0] = 0xFF;
    src->buffer[1] = JPEG_EOI;
    nbytes = 2;
  }
  src->pub.next_input_byte = src->buffer;
  src->pub.bytes_in_buffer = nbytes;
  src->start_of_file = FALSE;
  return TRUE;
}

void skip_input_data(j_decompress_ptr cinfo, long num_bytes) {
  jpeg_source_mgr* src = cinfo->src;
  if (num_bytes > 0) {
    while (num_bytes > static_cast<long>(src->bytes_in_buffer)) {
      num_bytes -= static_cast<long>(src->bytes_in_buffer);
      (void)(*src->fill_input_buffer)(cinfo);
    }
    src->next_input_byte += num_bytes;
    src->bytes_in_buffer -= static_cast<std::size_t>(num_bytes);
  }
}

void term_source(j_decompress_ptr) {}

void init_mem_source(j_decompress_ptr) {}

boolean fill_mem_input_buffer(j_decompress_ptr cinfo) {
  WARNMS(cinfo, JWRN_JPEG_EOF);
  cinfo->src->next_input_byte = kFakeEOI;
  cinfo->src->bytes_in_buffer = 2;
  return TRUE;
}

}  // namespace

extern "C" {

JEXPORT void jpeg_stdio_src(j_decompress_ptr cinfo, FILE* infile) {
  if (!cinfo->src) {
    auto* src = static_cast<StdioSource*>((*cinfo->mem->alloc_small)(
        reinterpret_cast<j_common_ptr>(cinfo), JPOOL_PERMANENT, sizeof(StdioSource)));
    src->buffer = static_cast<JOCTET*>((*cinfo->mem->alloc_small)(
        reinterpret_cast<j_common_ptr>(cinfo), JPOOL_PERMANENT, kInputBufSize));
    cinfo->src = &src->pub;
  } else if (cinfo->src->init_source != init_stdio_source) {
    // A source of another kind would be mistaken for this one.
    ERREXIT(cinfo, JERR_BUFFER_SIZE);
  }
  auto* src = reinterpret_cast<StdioSource*>(cinfo->src);
  src->pub.init_source = init_stdio_source;
  src->pub.fill_input_buffer = fill_stdio_input_buffer;
  src->pub.skip_input_data = skip_input_data;
  src->pub.resync_to_restart = jpeg_resync_to_restart;
  src->pub.term_source = term_source;
  src->infile = infile;
  src->pub.bytes_in_buffer = 0;
  src->pub.next_input_byte = nullptr;
}

JEXPORT void jpeg_mem_src(j_decompress_ptr cinfo, const unsigned char* inbuffer,
                          unsigned long insize) {
  if (!inbuffer || insize == 0) ERREXIT(cinfo, JERR_INPUT_EMPTY);
  if (!cinfo->src) {
    cinfo->src = static_cast<jpeg_source_mgr*>((*cinfo->mem->alloc_small)(
        reinterpret_cast<j_common_ptr>(cinfo), JPOOL_PERMANENT, sizeof(jpeg_source_mgr)));
  } else if (cinfo->src->init_source != init_mem_source) {
    ERREXIT(cinfo, JERR_BUFFER_SIZE);
  }
  jpeg_source_mgr* src = cinfo->src;
  src->init_source = init_mem_source;
  src->fill_input_buffer = fill_mem_input_buffer;
  src->skip_input_data = skip_input_data;
  src->resync_to_restart = jpeg_resync_to_restart;
  src->term_source = term_source;
  src->bytes_in_buffer = static_cast<std::size_t>(insize);
  src->next_input_byte = inbuffer;
}

// Reads the next marker from the data source (for jpeg_resync_to_restart()).
static boolean next_marker(j_decompress_ptr cinfo) {
  jpeg_source_mgr* src = cinfo->src;
  auto getc = [&](int& c) {
    if (src->bytes_in_buffer == 0 && !(*src->fill_input_buffer)(cinfo)) return false;
    src->bytes_in_buffer--;
    c = *src->next_input_byte++;
    return true;
  };
  int c;
  for (;;) {
    if (!getc(c)) return FALSE;
    while (c != 0xFF)
      if (!getc(c)) return FALSE;
    do {
      if (!getc(c)) return FALSE;
    } while (c == 0xFF);
    if (c != 0) break;
  }
  cinfo->unread_marker = c;
  return TRUE;
}

JEXPORT boolean jpeg_resync_to_restart(j_decompress_ptr cinfo, int desired) {
  int marker = cinfo->unread_marker;
  WARNMS2(cinfo, JWRN_MUST_RESYNC, marker, desired);
  for (;;) {
    int action;
    if (marker < 0xC0) {
      action = 2;  // invalid marker: scan forward
    } else if (marker < JPEG_RST0 || marker > JPEG_RST0 + 7) {
      action = 3;  // valid non-restart marker
    } else if (marker == JPEG_RST0 + ((desired + 1) & 7) ||
               marker == JPEG_RST0 + ((desired + 2) & 7)) {
      action = 3;  // one of the next two expected restarts
    } else if (marker == JPEG_RST0 + ((desired - 1) & 7) ||
               marker == JPEG_RST0 + ((desired - 2) & 7)) {
      action = 2;  // a prior restart: advance
    } else {
      action = 1;  // desired restart or too far away
    }
    TRACEMS2(cinfo, 4, JTRC_RECOVERY_ACTION, marker, action);
    switch (action) {
      case 1:
        cinfo->unread_marker = 0;
        return TRUE;
      case 2:
        if (!next_marker(cinfo)) return FALSE;
        marker = cinfo->unread_marker;
        break;
      default:
        return TRUE;
    }
  }
}

}  // extern "C"

// ===========================================================================
// Compression
// ===========================================================================

namespace {

#if JPEG_LIB_VERSION >= 70
#define C_SET_DCT_SCALED(c, v) ((c)->DCT_h_scaled_size = (c)->DCT_v_scaled_size = (v))
#define D_SET_DCT_SCALED(c, v) ((c)->DCT_h_scaled_size = (c)->DCT_v_scaled_size = (v))
#define D_SET_MIN_DCT_SCALED(d, v) ((d)->min_DCT_h_scaled_size = (d)->min_DCT_v_scaled_size = (v))
#else
#define C_SET_DCT_SCALED(c, v) ((void)(c), (void)(v))
#define D_SET_DCT_SCALED(c, v) ((c)->DCT_scaled_size = (v))
#define D_SET_MIN_DCT_SCALED(d, v) ((d)->min_DCT_scaled_size = (v))
#endif

void copy_quant(const JQUANT_TBL* t, std::optional<vio::jpeg::QuantTable>& q) {
  if (!t) {
    q.reset();
    return;
  }
  q.emplace();
  for (int k = 0; k < DCTSIZE2; k++) q->quantval[k] = t->quantval[k];
  q->sent_table = t->sent_table != 0;
}

void copy_huff(const JHUFF_TBL* t, std::optional<vio::jpeg::HuffTable>& h) {
  if (!t) {
    h.reset();
    return;
  }
  h.emplace();
  std::memcpy(h->bits.data(), t->bits, 17);
  std::memcpy(h->huffval.data(), t->huffval, 256);
  h->sent_table = t->sent_table != 0;
}

// Public compression parameters -> vio
void load_params(j_compress_ptr cinfo, vd::CState& cs, bool transcode = false) {
  jpeg_comp_master* m = cinfo->master;
  cs.image_width = cinfo->image_width;
  cs.image_height = cinfo->image_height;
#if JPEG_LIB_VERSION >= 80
  // When transcoding, the (transformed) frame size is given as jpeg_width and
  // jpeg_height.
  if (transcode) {
    cs.image_width = cinfo->jpeg_width;
    cs.image_height = cinfo->jpeg_height;
  }
#endif
  (void)transcode;
  cs.input_components = cinfo->input_components;
  cs.in_color_space = static_cast<ColorSpace>(cinfo->in_color_space);
  if (cinfo->in_color_space < JCS_UNKNOWN || cinfo->in_color_space > JCS_RGB565)
    fail(JERR_BAD_IN_COLORSPACE);
  cs.data_precision = cinfo->data_precision;
  cs.num_components = cinfo->num_components;
  if (cinfo->jpeg_color_space < JCS_UNKNOWN || cinfo->jpeg_color_space > JCS_RGB565)
    fail(JERR_BAD_J_COLORSPACE);
  cs.jpeg_color_space = static_cast<ColorSpace>(cinfo->jpeg_color_space);
  if (cinfo->num_components > MAX_COMPONENTS)
    fail(JERR_COMPONENT_COUNT, cinfo->num_components, MAX_COMPONENTS);
  if (cinfo->comp_info) {
    for (int ci = 0; ci < MAX_COMPONENTS; ci++) {
      const jpeg_component_info& s = cinfo->comp_info[ci];
      vd::ComponentInfo& c = cs.comp_info[ci];
      c.component_id = s.component_id;
      c.component_index = ci;
      c.h_samp_factor = s.h_samp_factor;
      c.v_samp_factor = s.v_samp_factor;
      c.quant_tbl_no = s.quant_tbl_no;
      c.dc_tbl_no = s.dc_tbl_no;
      c.ac_tbl_no = s.ac_tbl_no;
    }
  }
  for (int i = 0; i < NUM_QUANT_TBLS; i++) copy_quant(cinfo->quant_tbl_ptrs[i], cs.quant_tbl_ptrs[i]);
#if JPEG_LIB_VERSION >= 70
  for (int i = 0; i < NUM_QUANT_TBLS; i++) cs.q_scale_factor[i] = cinfo->q_scale_factor[i];
#endif
  for (int i = 0; i < NUM_HUFF_TBLS; i++) {
    copy_huff(cinfo->dc_huff_tbl_ptrs[i], cs.dc_huff_tbl_ptrs[i]);
    copy_huff(cinfo->ac_huff_tbl_ptrs[i], cs.ac_huff_tbl_ptrs[i]);
  }
  std::memcpy(cs.arith_dc_L, cinfo->arith_dc_L, NUM_ARITH_TBLS);
  std::memcpy(cs.arith_dc_U, cinfo->arith_dc_U, NUM_ARITH_TBLS);
  std::memcpy(cs.arith_ac_K, cinfo->arith_ac_K, NUM_ARITH_TBLS);
  cs.scan_info.clear();
  if (cinfo->scan_info) {
    if (cinfo->num_scans <= 0) fail(JERR_BAD_SCAN_SCRIPT, 0);
    for (int s = 0; s < cinfo->num_scans; s++) {
      const jpeg_scan_info& si = cinfo->scan_info[s];
      vd::ScanInfo v;
      v.comps_in_scan = si.comps_in_scan;
      for (int k = 0; k < MAX_COMPS_IN_SCAN; k++) v.component_index[k] = si.component_index[k];
      v.Ss = si.Ss;
      v.Se = si.Se;
      v.Ah = si.Ah;
      v.Al = si.Al;
      cs.scan_info.push_back(v);
    }
    cs.num_scans = cinfo->num_scans;
  } else {
    cs.num_scans = 0;
  }
  cs.raw_data_in = cinfo->raw_data_in != 0;
  cs.arith_code = cinfo->arith_code != 0;
  cs.optimize_coding = cinfo->optimize_coding != 0;
  cs.CCIR601_sampling = cinfo->CCIR601_sampling != 0;
  cs.smoothing_factor = cinfo->smoothing_factor;
  cs.dct_method = static_cast<vio::jpeg::DctMethod>(cinfo->dct_method);
  cs.restart_interval = cinfo->restart_interval;
  cs.restart_in_rows = cinfo->restart_in_rows;
  cs.write_JFIF_header = cinfo->write_JFIF_header != 0;
  cs.JFIF_major_version = cinfo->JFIF_major_version;
  cs.JFIF_minor_version = cinfo->JFIF_minor_version;
  cs.density_unit = cinfo->density_unit;
  cs.X_density = cinfo->X_density;
  cs.Y_density = cinfo->Y_density;
  cs.write_Adobe_marker = cinfo->write_Adobe_marker != 0;
  cs.lossless = m->lossless;
  if (m->lossless) {
    cs.Ss = cinfo->Ss;
    cs.Se = cinfo->Se;
    cs.Ah = cinfo->Ah;
    cs.Al = cinfo->Al;
  }
  cs.next_scanline = cinfo->next_scanline;
}

void store_tables(j_compress_ptr cinfo, const vd::CState& cs) {
  auto common = reinterpret_cast<j_common_ptr>(cinfo);
  for (int i = 0; i < NUM_QUANT_TBLS; i++) {
    if (!cs.quant_tbl_ptrs[i]) continue;
    JQUANT_TBL*& t = cinfo->quant_tbl_ptrs[i];
    if (!t) {
      t = static_cast<JQUANT_TBL*>(xalloc(common, JPOOL_PERMANENT, sizeof(JQUANT_TBL)));
      std::memset(t, 0, sizeof(*t));
    }
    for (int k = 0; k < DCTSIZE2; k++) t->quantval[k] = cs.quant_tbl_ptrs[i]->quantval[k];
    t->sent_table = cs.quant_tbl_ptrs[i]->sent_table;
  }
  for (int i = 0; i < NUM_HUFF_TBLS; i++) {
    for (int ac = 0; ac < 2; ac++) {
      const auto& h = ac ? cs.ac_huff_tbl_ptrs[i] : cs.dc_huff_tbl_ptrs[i];
      if (!h) continue;
      JHUFF_TBL*& t = ac ? cinfo->ac_huff_tbl_ptrs[i] : cinfo->dc_huff_tbl_ptrs[i];
      if (!t) {
        t = static_cast<JHUFF_TBL*>(xalloc(common, JPOOL_PERMANENT, sizeof(JHUFF_TBL)));
        std::memset(t, 0, sizeof(*t));
      }
      std::memcpy(t->bits, h->bits.data(), 17);
      std::memcpy(t->huffval, h->huffval.data(), 256);
      t->sent_table = h->sent_table;
    }
  }
}

// vio's state after starting compression -> public fields
void store_state(j_compress_ptr cinfo, const vd::CState& cs) {
  int data_unit = cs.lossless ? 1 : DCTSIZE;
  cinfo->num_components = cs.num_components;
  cinfo->jpeg_color_space = static_cast<J_COLOR_SPACE>(cs.jpeg_color_space);
  cinfo->num_scans = cs.num_scans;
  cinfo->raw_data_in = cs.raw_data_in;
  cinfo->optimize_coding = cs.optimize_coding;
  cinfo->smoothing_factor = cs.smoothing_factor;
  cinfo->restart_interval = cs.restart_interval;
  cinfo->progressive_mode = cs.progressive_mode;
  cinfo->max_h_samp_factor = cs.max_h_samp_factor;
  cinfo->max_v_samp_factor = cs.max_v_samp_factor;
  cinfo->total_iMCU_rows = cs.total_iMCU_rows;
#if JPEG_LIB_VERSION >= 70
  cinfo->jpeg_width = cs.image_width;
  cinfo->jpeg_height = cs.image_height;
  cinfo->min_DCT_h_scaled_size = cinfo->min_DCT_v_scaled_size = data_unit;
#endif
  if (cinfo->comp_info) {
    for (int ci = 0; ci < cs.num_components; ci++) {
      const vd::ComponentInfo& c = cs.comp_info[ci];
      jpeg_component_info& d = cinfo->comp_info[ci];
      d.component_id = c.component_id;
      d.component_index = c.component_index;
      d.h_samp_factor = c.h_samp_factor;
      d.v_samp_factor = c.v_samp_factor;
      d.quant_tbl_no = c.quant_tbl_no;
      d.dc_tbl_no = c.dc_tbl_no;
      d.ac_tbl_no = c.ac_tbl_no;
      d.width_in_blocks = c.width_in_blocks;
      d.height_in_blocks = c.height_in_blocks;
      C_SET_DCT_SCALED(&d, data_unit);
      d.downsampled_width = c.downsampled_width;
      d.downsampled_height = c.downsampled_height;
      d.component_needed = c.component_needed;
      d.MCU_width = c.MCU_width;
      d.MCU_height = c.MCU_height;
      d.MCU_blocks = c.MCU_blocks;
      d.MCU_sample_width = c.MCU_sample_width;
      d.last_col_width = c.last_col_width;
      d.last_row_height = c.last_row_height;
    }
    cinfo->comps_in_scan = cs.comps_in_scan;
    for (int i = 0; i < MAX_COMPS_IN_SCAN; i++)
      cinfo->cur_comp_info[i] =
          i < cs.comps_in_scan && cs.cur_comp_info[i]
              ? &cinfo->comp_info[cs.cur_comp_info[i]->component_index]
              : nullptr;
  }
  (void)data_unit;
  cinfo->MCUs_per_row = cs.MCUs_per_row;
  cinfo->MCU_rows_in_scan = cs.MCU_rows_in_scan;
  cinfo->blocks_in_MCU = cs.blocks_in_MCU;
  for (int i = 0; i < C_MAX_BLOCKS_IN_MCU; i++) cinfo->MCU_membership[i] = cs.MCU_membership[i];
  cinfo->Ss = cs.Ss;
  cinfo->Se = cs.Se;
  cinfo->Ah = cs.Ah;
  cinfo->Al = cs.Al;
  cinfo->next_scanline = cs.next_scanline;
  store_tables(cinfo, cs);
}

void append_pending(jpeg_comp_master* m, const std::uint8_t* p, std::size_t n) {
  if (m->pending_pos == m->pending.size()) {
    m->pending.clear();
    m->pending_pos = 0;
  }
  m->pending.insert(m->pending.end(), p, p + n);
}

// Copies the compressed data produced so far to the destination.  Returns
// false if the destination suspended (the rest is kept).
bool flush_output(j_compress_ptr cinfo) {
  jpeg_comp_master* m = cinfo->master;
  jpeg_destination_mgr* dest = cinfo->dest;
  vd::ByteSink& sink = m->c.cs.dest;
  bool suspended = false;
  while (m->pending_pos < m->pending.size()) {
    if (dest->free_in_buffer == 0 && !(*dest->empty_output_buffer)(cinfo)) {
      suspended = true;
      break;
    }
    std::size_t n = std::min(dest->free_in_buffer, m->pending.size() - m->pending_pos);
    std::memcpy(dest->next_output_byte, m->pending.data() + m->pending_pos, n);
    dest->next_output_byte += n;
    dest->free_in_buffer -= n;
    m->pending_pos += n;
  }
  const std::uint8_t* p = sink.data();
  std::size_t left = sink.size();
  while (left > 0 && !suspended) {
    if (dest->free_in_buffer == 0 && !(*dest->empty_output_buffer)(cinfo)) {
      suspended = true;
      break;
    }
    std::size_t n = std::min(dest->free_in_buffer, left);
    std::memcpy(dest->next_output_byte, p, n);
    dest->next_output_byte += n;
    dest->free_in_buffer -= n;
    p += n;
    left -= n;
  }
  if (left > 0 && !CALL(cinfo, append_pending(m, p, left))) return false;
  sink.rewind();
  return !suspended;
}

void check_state(j_common_ptr cinfo, int state) {
  if (cinfo->global_state != state) ERREXIT1(cinfo, JERR_BAD_STATE, cinfo->global_state);
}

void progress(j_common_ptr cinfo, long counter, long limit) {
  if (cinfo->progress) {
    cinfo->progress->pass_counter = counter;
    cinfo->progress->pass_limit = limit;
    (*cinfo->progress->progress_monitor)(cinfo);
  }
}

template <class T>
constexpr int kSampleBits = std::is_same_v<T, JSAMPLE> ? 8 : std::is_same_v<T, J12SAMPLE> ? 12 : 16;

// Checks the precision of the API variant used (jpeg_, jpeg12_ or jpeg16_).
template <class T>
void check_precision(j_common_ptr cinfo, int precision, bool lossless) {
  constexpr int bits = kSampleBits<T>;
  bool ok;
  if (lossless) ok = bits == 8 ? (precision >= 2 && precision <= 8)
                               : (precision >= bits - 3 && precision <= bits);
  else ok = bits != 16 && precision == bits;
  if (!ok) ERREXIT1(cinfo, JERR_BAD_PRECISION, precision);
}

template <class T>
JDIMENSION write_scanlines(j_compress_ptr cinfo, T** scanlines, JDIMENSION num_lines) {
  auto common = reinterpret_cast<j_common_ptr>(cinfo);
  jpeg_comp_master* m = cinfo->master;
  check_precision<T>(common, cinfo->data_precision, m->lossless);
  check_state(common, CSTATE_SCANNING);
  progress(common, static_cast<long>(cinfo->next_scanline), static_cast<long>(cinfo->image_height));
  JDIMENSION rows = 0;
  CALL(cinfo, rows = m->c.write_scanlines<T>(scanlines, num_lines));
  cinfo->next_scanline = m->c.cs.next_scanline;
  if (!flush_output(cinfo)) {
    // The destination suspended: the data is kept for the next call.
  }
  return rows;
}

template <class T>
JDIMENSION write_raw_data(j_compress_ptr cinfo, T*** data, JDIMENSION num_lines) {
  auto common = reinterpret_cast<j_common_ptr>(cinfo);
  jpeg_comp_master* m = cinfo->master;
  check_precision<T>(common, cinfo->data_precision, false);
  check_state(common, CSTATE_RAW_OK);
  if (cinfo->next_scanline >= cinfo->image_height) {
    WARNMS(cinfo, JWRN_TOO_MUCH_DATA);
    return 0;
  }
  progress(common, static_cast<long>(cinfo->next_scanline), static_cast<long>(cinfo->image_height));
  JDIMENSION rows = 0;
  CALL(cinfo, rows = m->c.write_raw_data<T>(data, num_lines));
  cinfo->next_scanline = m->c.cs.next_scanline;
  flush_output(cinfo);
  return rows;
}

void check_marker_state(j_compress_ptr cinfo) {
  if (cinfo->next_scanline != 0 ||
      (cinfo->global_state != CSTATE_SCANNING && cinfo->global_state != CSTATE_RAW_OK &&
       cinfo->global_state != CSTATE_WRCOEFS))
    ERREXIT1(cinfo, JERR_BAD_STATE, cinfo->global_state);
}

void add_huff_table(j_compress_ptr cinfo, JHUFF_TBL** htblptr, const UINT8* bits,
                    const UINT8* val) {
  if (!*htblptr) *htblptr = jpeg_alloc_huff_table(reinterpret_cast<j_common_ptr>(cinfo));
  std::memcpy((*htblptr)->bits, bits, sizeof((*htblptr)->bits));
  int nsymbols = 0;
  for (int len = 1; len <= 16; len++) nsymbols += bits[len];
  if (nsymbols < 1 || nsymbols > 256) ERREXIT(cinfo, JERR_BAD_HUFF_TABLE);
  std::memset((*htblptr)->huffval, 0, sizeof((*htblptr)->huffval));
  std::memcpy((*htblptr)->huffval, val, nsymbols * sizeof(UINT8));
  (*htblptr)->sent_table = FALSE;
}

void std_huff_tables(j_compress_ptr cinfo) {
  add_huff_table(cinfo, &cinfo->dc_huff_tbl_ptrs[0], vd::bits_dc_luminance, vd::val_dc_luminance);
  add_huff_table(cinfo, &cinfo->ac_huff_tbl_ptrs[0], vd::bits_ac_luminance, vd::val_ac_luminance);
  add_huff_table(cinfo, &cinfo->dc_huff_tbl_ptrs[1], vd::bits_dc_chrominance,
                 vd::val_dc_chrominance);
  add_huff_table(cinfo, &cinfo->ac_huff_tbl_ptrs[1], vd::bits_ac_chrominance,
                 vd::val_ac_chrominance);
}

}  // namespace

extern "C" {

JEXPORT void jpeg_CreateCompress(j_compress_ptr cinfo, int version, size_t structsize) {
  cinfo->mem = nullptr;
  if (version != JPEG_LIB_VERSION) ERREXIT2(cinfo, JERR_BAD_LIB_VERSION, JPEG_LIB_VERSION, version);
  if (structsize != sizeof(jpeg_compress_struct))
    ERREXIT2(cinfo, JERR_BAD_STRUCT_SIZE, static_cast<int>(sizeof(jpeg_compress_struct)),
             static_cast<int>(structsize));
  {
    jpeg_error_mgr* err = cinfo->err;
    void* client_data = cinfo->client_data;
    std::memset(static_cast<void*>(cinfo), 0, sizeof(jpeg_compress_struct));
    cinfo->err = err;
    cinfo->client_data = client_data;
  }
  cinfo->is_decompressor = FALSE;
  init_memory_mgr(reinterpret_cast<j_common_ptr>(cinfo));
  cinfo->progress = nullptr;
  cinfo->dest = nullptr;
  cinfo->comp_info = nullptr;
  for (int i = 0; i < NUM_QUANT_TBLS; i++) {
    cinfo->quant_tbl_ptrs[i] = nullptr;
#if JPEG_LIB_VERSION >= 70
    cinfo->q_scale_factor[i] = 100;
#endif
  }
  for (int i = 0; i < NUM_HUFF_TBLS; i++) {
    cinfo->dc_huff_tbl_ptrs[i] = nullptr;
    cinfo->ac_huff_tbl_ptrs[i] = nullptr;
  }
#if JPEG_LIB_VERSION >= 80
  cinfo->block_size = DCTSIZE;
  cinfo->natural_order = jpeg_natural_order;
  cinfo->lim_Se = DCTSIZE2 - 1;
#endif
  cinfo->script_space = nullptr;
  cinfo->input_gamma = 1.0;
  cinfo->data_precision = 8;
  cinfo->global_state = CSTATE_START;
  cinfo->master = new (std::nothrow) jpeg_comp_master;
  if (!cinfo->master) ERREXIT1(cinfo, JERR_OUT_OF_MEMORY, 0);
}

JEXPORT void jpeg_destroy_compress(j_compress_ptr cinfo) {
  jpeg_destroy(reinterpret_cast<j_common_ptr>(cinfo));
}

JEXPORT void jpeg_abort_compress(j_compress_ptr cinfo) {
  jpeg_abort(reinterpret_cast<j_common_ptr>(cinfo));
}

JEXPORT void jpeg_suppress_tables(j_compress_ptr cinfo, boolean suppress) {
  for (int i = 0; i < NUM_QUANT_TBLS; i++)
    if (cinfo->quant_tbl_ptrs[i]) cinfo->quant_tbl_ptrs[i]->sent_table = suppress;
  for (int i = 0; i < NUM_HUFF_TBLS; i++) {
    if (cinfo->dc_huff_tbl_ptrs[i]) cinfo->dc_huff_tbl_ptrs[i]->sent_table = suppress;
    if (cinfo->ac_huff_tbl_ptrs[i]) cinfo->ac_huff_tbl_ptrs[i]->sent_table = suppress;
  }
}

JEXPORT int jpeg_quality_scaling(int quality) { return vd::quality_scaling(quality); }

JEXPORT void jpeg_add_quant_table(j_compress_ptr cinfo, int which_tbl,
                                  const unsigned int* basic_table, int scale_factor,
                                  boolean force_baseline) {
  check_state(reinterpret_cast<j_common_ptr>(cinfo), CSTATE_START);
  if (which_tbl < 0 || which_tbl >= NUM_QUANT_TBLS) ERREXIT1(cinfo, JERR_DQT_INDEX, which_tbl);
  JQUANT_TBL** qtblptr = &cinfo->quant_tbl_ptrs[which_tbl];
  if (!*qtblptr) *qtblptr = jpeg_alloc_quant_table(reinterpret_cast<j_common_ptr>(cinfo));
  for (int i = 0; i < DCTSIZE2; i++) {
    long temp = (static_cast<long>(basic_table[i]) * scale_factor + 50L) / 100L;
    if (temp <= 0L) temp = 1L;
    if (temp > 32767L) temp = 32767L;
    if (force_baseline && temp > 255L) temp = 255L;
    (*qtblptr)->quantval[i] = static_cast<UINT16>(temp);
  }
  (*qtblptr)->sent_table = FALSE;
}

#if JPEG_LIB_VERSION >= 70
JEXPORT void jpeg_default_qtables(j_compress_ptr cinfo, boolean force_baseline) {
  jpeg_add_quant_table(cinfo, 0, vd::std_luminance_quant_tbl, cinfo->q_scale_factor[0],
                       force_baseline);
  jpeg_add_quant_table(cinfo, 1, vd::std_chrominance_quant_tbl, cinfo->q_scale_factor[1],
                       force_baseline);
}
#endif

JEXPORT void jpeg_set_linear_quality(j_compress_ptr cinfo, int scale_factor,
                                     boolean force_baseline) {
  jpeg_add_quant_table(cinfo, 0, vd::std_luminance_quant_tbl, scale_factor, force_baseline);
  jpeg_add_quant_table(cinfo, 1, vd::std_chrominance_quant_tbl, scale_factor, force_baseline);
}

JEXPORT void jpeg_set_quality(j_compress_ptr cinfo, int quality, boolean force_baseline) {
  jpeg_set_linear_quality(cinfo, jpeg_quality_scaling(quality), force_baseline);
}

JEXPORT void jpeg_set_colorspace(j_compress_ptr cinfo, J_COLOR_SPACE colorspace) {
  check_state(reinterpret_cast<j_common_ptr>(cinfo), CSTATE_START);
  auto set_comp = [&](int index, int id, int hsamp, int vsamp, int quant, int dctbl, int actbl) {
    jpeg_component_info* c = &cinfo->comp_info[index];
    c->component_id = id;
    c->h_samp_factor = hsamp;
    c->v_samp_factor = vsamp;
    c->quant_tbl_no = quant;
    c->dc_tbl_no = dctbl;
    c->ac_tbl_no = actbl;
  };
  cinfo->jpeg_color_space = colorspace;
  cinfo->write_JFIF_header = FALSE;
  cinfo->write_Adobe_marker = FALSE;
  switch (colorspace) {
    case JCS_GRAYSCALE:
      cinfo->write_JFIF_header = TRUE;
      cinfo->num_components = 1;
      set_comp(0, 1, 1, 1, 0, 0, 0);
      break;
    case JCS_RGB:
      cinfo->write_Adobe_marker = TRUE;
      cinfo->num_components = 3;
      set_comp(0, 0x52, 1, 1, 0, 0, 0);
      set_comp(1, 0x47, 1, 1, 0, 0, 0);
      set_comp(2, 0x42, 1, 1, 0, 0, 0);
      break;
    case JCS_YCbCr:
      cinfo->write_JFIF_header = TRUE;
      cinfo->num_components = 3;
      set_comp(0, 1, 2, 2, 0, 0, 0);
      set_comp(1, 2, 1, 1, 1, 1, 1);
      set_comp(2, 3, 1, 1, 1, 1, 1);
      break;
    case JCS_CMYK:
      cinfo->write_Adobe_marker = TRUE;
      cinfo->num_components = 4;
      set_comp(0, 0x43, 1, 1, 0, 0, 0);
      set_comp(1, 0x4D, 1, 1, 0, 0, 0);
      set_comp(2, 0x59, 1, 1, 0, 0, 0);
      set_comp(3, 0x4B, 1, 1, 0, 0, 0);
      break;
    case JCS_YCCK:
      cinfo->write_Adobe_marker = TRUE;
      cinfo->num_components = 4;
      set_comp(0, 1, 2, 2, 0, 0, 0);
      set_comp(1, 2, 1, 1, 1, 1, 1);
      set_comp(2, 3, 1, 1, 1, 1, 1);
      set_comp(3, 4, 2, 2, 0, 0, 0);
      break;
    case JCS_UNKNOWN:
      cinfo->num_components = cinfo->input_components;
      if (cinfo->num_components < 1 || cinfo->num_components > MAX_COMPONENTS)
        ERREXIT2(cinfo, JERR_COMPONENT_COUNT, cinfo->num_components, MAX_COMPONENTS);
      for (int ci = 0; ci < cinfo->num_components; ci++) set_comp(ci, ci, 1, 1, 0, 0, 0);
      break;
    default:
      ERREXIT(cinfo, JERR_BAD_J_COLORSPACE);
  }
}

JEXPORT void jpeg_default_colorspace(j_compress_ptr cinfo) {
  switch (cinfo->in_color_space) {
    case JCS_GRAYSCALE:
      jpeg_set_colorspace(cinfo, JCS_GRAYSCALE);
      break;
    case JCS_RGB:
    case JCS_EXT_RGB:
    case JCS_EXT_RGBX:
    case JCS_EXT_BGR:
    case JCS_EXT_BGRX:
    case JCS_EXT_XBGR:
    case JCS_EXT_XRGB:
    case JCS_EXT_RGBA:
    case JCS_EXT_BGRA:
    case JCS_EXT_ABGR:
    case JCS_EXT_ARGB:
      jpeg_set_colorspace(cinfo, cinfo->master->lossless ? JCS_RGB : JCS_YCbCr);
      break;
    case JCS_YCbCr:
      jpeg_set_colorspace(cinfo, JCS_YCbCr);
      break;
    case JCS_CMYK:
      jpeg_set_colorspace(cinfo, JCS_CMYK);
      break;
    case JCS_YCCK:
      jpeg_set_colorspace(cinfo, JCS_YCCK);
      break;
    case JCS_UNKNOWN:
      jpeg_set_colorspace(cinfo, JCS_UNKNOWN);
      break;
    default:
      ERREXIT(cinfo, JERR_BAD_IN_COLORSPACE);
  }
}

JEXPORT void jpeg_set_defaults(j_compress_ptr cinfo) {
  check_state(reinterpret_cast<j_common_ptr>(cinfo), CSTATE_START);
  if (!cinfo->comp_info) {
    cinfo->comp_info = static_cast<jpeg_component_info*>((*cinfo->mem->alloc_small)(
        reinterpret_cast<j_common_ptr>(cinfo), JPOOL_PERMANENT,
        MAX_COMPONENTS * sizeof(jpeg_component_info)));
    std::memset(cinfo->comp_info, 0, MAX_COMPONENTS * sizeof(jpeg_component_info));
  }
#if JPEG_LIB_VERSION >= 70
  cinfo->scale_num = 1;
  cinfo->scale_denom = 1;
#endif
  jpeg_set_quality(cinfo, 75, TRUE);
  std_huff_tables(cinfo);
  for (int i = 0; i < NUM_ARITH_TBLS; i++) {
    cinfo->arith_dc_L[i] = 0;
    cinfo->arith_dc_U[i] = 1;
    cinfo->arith_ac_K[i] = 5;
  }
  cinfo->scan_info = nullptr;
  cinfo->num_scans = 0;
  cinfo->master->lossless = false;
  cinfo->raw_data_in = FALSE;
  cinfo->arith_code = FALSE;
  // 12-bit data needs optimal tables (the standard ones may not fit)
  cinfo->optimize_coding = cinfo->data_precision == 12;
  cinfo->CCIR601_sampling = FALSE;
#if JPEG_LIB_VERSION >= 70
  cinfo->do_fancy_downsampling = TRUE;
#endif
  cinfo->smoothing_factor = 0;
  cinfo->dct_method = JDCT_DEFAULT;
  cinfo->restart_interval = 0;
  cinfo->restart_in_rows = 0;
  cinfo->JFIF_major_version = 1;
  cinfo->JFIF_minor_version = 1;
  cinfo->density_unit = 0;
  cinfo->X_density = 1;
  cinfo->Y_density = 1;
  jpeg_default_colorspace(cinfo);
}

JEXPORT void jpeg_enable_lossless(j_compress_ptr cinfo, int predictor_selection_value,
                                  int point_transform) {
  check_state(reinterpret_cast<j_common_ptr>(cinfo), CSTATE_START);
  cinfo->master->lossless = true;
  cinfo->Ss = predictor_selection_value;
  cinfo->Se = 0;
  cinfo->Ah = 0;
  cinfo->Al = point_transform;
  if (cinfo->Ss < 1 || cinfo->Ss > 7 || cinfo->Al < 0 || cinfo->Al >= cinfo->data_precision)
    ERREXIT4(cinfo, JERR_BAD_PROGRESSION, cinfo->Ss, cinfo->Se, cinfo->Ah, cinfo->Al);
}

JEXPORT void jpeg_simple_progression(j_compress_ptr cinfo) {
  check_state(reinterpret_cast<j_common_ptr>(cinfo), CSTATE_START);
  if (cinfo->master->lossless) {
    cinfo->master->lossless = false;
    jpeg_default_colorspace(cinfo);
  }
  int ncomps = cinfo->num_components;
  bool ycc = ncomps == 3 && cinfo->jpeg_color_space == JCS_YCbCr;
  int nscans = ycc ? 10 : ncomps > MAX_COMPS_IN_SCAN ? 6 * ncomps : 2 + 4 * ncomps;
  // The script lives in the permanent pool, reused by later calls.
  if (!cinfo->script_space || cinfo->script_space_size < nscans) {
    cinfo->script_space_size = std::max(nscans, 10);
    cinfo->script_space = static_cast<jpeg_scan_info*>((*cinfo->mem->alloc_small)(
        reinterpret_cast<j_common_ptr>(cinfo), JPOOL_PERMANENT,
        cinfo->script_space_size * sizeof(jpeg_scan_info)));
  }
  jpeg_scan_info* scanptr = cinfo->script_space;
  cinfo->scan_info = scanptr;
  cinfo->num_scans = nscans;
  auto a_scan = [&](int ci, int Ss, int Se, int Ah, int Al) {
    scanptr->comps_in_scan = 1;
    scanptr->component_index[0] = ci;
    scanptr->Ss = Ss;
    scanptr->Se = Se;
    scanptr->Ah = Ah;
    scanptr->Al = Al;
    scanptr++;
  };
  auto scans = [&](int Ss, int Se, int Ah, int Al) {
    for (int ci = 0; ci < ncomps; ci++) a_scan(ci, Ss, Se, Ah, Al);
  };
  auto dc_scans = [&](int Ah, int Al) {
    if (ncomps <= MAX_COMPS_IN_SCAN) {
      scanptr->comps_in_scan = ncomps;
      for (int ci = 0; ci < ncomps; ci++) scanptr->component_index[ci] = ci;
      scanptr->Ss = scanptr->Se = 0;
      scanptr->Ah = Ah;
      scanptr->Al = Al;
      scanptr++;
    } else {
      scans(0, 0, Ah, Al);
    }
  };
  if (ycc) {
    dc_scans(0, 1);
    a_scan(0, 1, 5, 0, 2);
    a_scan(2, 1, 63, 0, 1);
    a_scan(1, 1, 63, 0, 1);
    a_scan(0, 6, 63, 0, 2);
    a_scan(0, 1, 63, 2, 1);
    dc_scans(1, 0);
    a_scan(2, 1, 63, 1, 0);
    a_scan(1, 1, 63, 1, 0);
    a_scan(0, 1, 63, 1, 0);
  } else {
    dc_scans(0, 1);
    scans(1, 5, 0, 2);
    scans(6, 63, 0, 2);
    scans(1, 63, 2, 1);
    dc_scans(1, 0);
    scans(1, 63, 1, 0);
  }
}

#if JPEG_LIB_VERSION >= 70
JEXPORT void jpeg_calc_jpeg_dimensions(j_compress_ptr cinfo) {
  int data_unit = cinfo->master->lossless ? 1 : DCTSIZE;
  // Only 1:1 scaling is supported.
  cinfo->jpeg_width = cinfo->image_width;
  cinfo->jpeg_height = cinfo->image_height;
  cinfo->min_DCT_h_scaled_size = data_unit;
  cinfo->min_DCT_v_scaled_size = data_unit;
}
#endif

JEXPORT void jpeg_start_compress(j_compress_ptr cinfo, boolean write_all_tables) {
  auto common = reinterpret_cast<j_common_ptr>(cinfo);
  jpeg_comp_master* m = cinfo->master;
  check_state(common, CSTATE_START);
  if (write_all_tables) jpeg_suppress_tables(cinfo, FALSE);
  (*cinfo->err->reset_error_mgr)(common);
  (*cinfo->dest->init_destination)(cinfo);
  if (!CALL(cinfo, {
        load_params(cinfo, m->c.cs);
        m->c.start_compress(false);
        store_state(cinfo, m->c.cs);
      }))
    return;
  (*cinfo->mem->realize_virt_arrays)(common);
  if (cinfo->progress) {
    cinfo->progress->completed_passes = 0;
    cinfo->progress->total_passes = m->c.cs.total_passes;
  }
  flush_output(cinfo);
  cinfo->next_scanline = 0;
  cinfo->global_state = cinfo->raw_data_in ? CSTATE_RAW_OK : CSTATE_SCANNING;
}

JEXPORT JDIMENSION jpeg_write_scanlines(j_compress_ptr cinfo, JSAMPARRAY scanlines,
                                        JDIMENSION num_lines) {
  return write_scanlines<JSAMPLE>(cinfo, scanlines, num_lines);
}

JEXPORT JDIMENSION jpeg12_write_scanlines(j_compress_ptr cinfo, J12SAMPARRAY scanlines,
                                          JDIMENSION num_lines) {
  return write_scanlines<J12SAMPLE>(cinfo, scanlines, num_lines);
}

JEXPORT JDIMENSION jpeg16_write_scanlines(j_compress_ptr cinfo, J16SAMPARRAY scanlines,
                                          JDIMENSION num_lines) {
  return write_scanlines<J16SAMPLE>(cinfo, scanlines, num_lines);
}

JEXPORT JDIMENSION jpeg_write_raw_data(j_compress_ptr cinfo, JSAMPIMAGE data,
                                       JDIMENSION num_lines) {
  return write_raw_data<JSAMPLE>(cinfo, data, num_lines);
}

JEXPORT JDIMENSION jpeg12_write_raw_data(j_compress_ptr cinfo, J12SAMPIMAGE data,
                                         JDIMENSION num_lines) {
  return write_raw_data<J12SAMPLE>(cinfo, data, num_lines);
}

JEXPORT void jpeg_finish_compress(j_compress_ptr cinfo) {
  auto common = reinterpret_cast<j_common_ptr>(cinfo);
  jpeg_comp_master* m = cinfo->master;
  if (cinfo->global_state == CSTATE_SCANNING || cinfo->global_state == CSTATE_RAW_OK) {
    if (cinfo->next_scanline < cinfo->image_height) ERREXIT(cinfo, JERR_TOO_LITTLE_DATA);
  } else if (cinfo->global_state != CSTATE_WRCOEFS) {
    ERREXIT1(cinfo, JERR_BAD_STATE, cinfo->global_state);
  }
  if (cinfo->progress) {
    cinfo->progress->completed_passes = cinfo->progress->total_passes - 1;
    progress(common, 0, static_cast<long>(cinfo->total_iMCU_rows));
  }
  if (!CALL(cinfo, {
        m->c.finish_compress();
        store_tables(cinfo, m->c.cs);
      }))
    return;
  if (!flush_output(cinfo)) ERREXIT(cinfo, JERR_CANT_SUSPEND);
  (*cinfo->dest->term_destination)(cinfo);
  jpeg_abort(common);
}

JEXPORT void jpeg_write_marker(j_compress_ptr cinfo, int marker, const JOCTET* dataptr,
                               unsigned int datalen) {
  jpeg_comp_master* m = cinfo->master;
  check_marker_state(cinfo);
  CALL(cinfo, {
    m->c.cs.write_marker_header(marker, datalen);
    for (unsigned i = 0; i < datalen; i++) m->c.cs.emit_byte(dataptr[i]);
  });
  flush_output(cinfo);
}

JEXPORT void jpeg_write_m_header(j_compress_ptr cinfo, int marker, unsigned int datalen) {
  jpeg_comp_master* m = cinfo->master;
  check_marker_state(cinfo);
  CALL(cinfo, m->c.cs.write_marker_header(marker, datalen));
  flush_output(cinfo);
}

JEXPORT void jpeg_write_m_byte(j_compress_ptr cinfo, int val) {
  jpeg_comp_master* m = cinfo->master;
  CALL(cinfo, m->c.cs.emit_byte(val));
  flush_output(cinfo);
}

JEXPORT void jpeg_write_icc_profile(j_compress_ptr cinfo, const JOCTET* icc_data_ptr,
                                    unsigned int icc_data_len) {
  if (!icc_data_ptr || icc_data_len == 0) ERREXIT(cinfo, JERR_BUFFER_SIZE);
  if (cinfo->global_state < CSTATE_SCANNING) ERREXIT1(cinfo, JERR_BAD_STATE, cinfo->global_state);
  check_marker_state(cinfo);
  jpeg_comp_master* m = cinfo->master;
  CALL(cinfo, m->c.write_icc_profile(icc_data_ptr, icc_data_len));
  flush_output(cinfo);
}

JEXPORT void jpeg_write_tables(j_compress_ptr cinfo) {
  auto common = reinterpret_cast<j_common_ptr>(cinfo);
  jpeg_comp_master* m = cinfo->master;
  check_state(common, CSTATE_START);
  (*cinfo->err->reset_error_mgr)(common);
  (*cinfo->dest->init_destination)(cinfo);
  if (!CALL(cinfo, {
        load_params(cinfo, m->c.cs);
        m->c.write_tables();
        store_tables(cinfo, m->c.cs);
      }))
    return;
  if (!flush_output(cinfo)) ERREXIT(cinfo, JERR_CANT_SUSPEND);
  (*cinfo->dest->term_destination)(cinfo);
  jpeg_abort(common);
}

JEXPORT void jpeg_write_coefficients(j_compress_ptr cinfo, jvirt_barray_ptr* coef_arrays) {
  auto common = reinterpret_cast<j_common_ptr>(cinfo);
  jpeg_comp_master* m = cinfo->master;
  if (m->lossless) ERREXIT(cinfo, JERR_NOTIMPL);
  check_state(common, CSTATE_START);
  jpeg_suppress_tables(cinfo, FALSE);
  (*cinfo->err->reset_error_mgr)(common);
  (*cinfo->dest->init_destination)(cinfo);
  for (int ci = 0; ci < cinfo->num_components; ci++)
    if (!coef_arrays[ci] || !coef_arrays[ci]->rows) ERREXIT(cinfo, JERR_BAD_VIRTUAL_ACCESS);
  if (!CALL(cinfo, {
        load_params(cinfo, m->c.cs, true);
        m->coef_views.clear();
        m->coef_views.resize(static_cast<std::size_t>(std::max(cinfo->num_components, 1)));
        for (int ci = 0; ci < cinfo->num_components; ci++) {
          jvirt_barray_ptr a = coef_arrays[ci];
          m->coef_views[ci].view(reinterpret_cast<vio::jpeg::Block*>(a->base), a->blocksperrow,
                                 a->rows_in_array);
        }
        m->c.write_coefficients(m->coef_views.data());
        store_state(cinfo, m->c.cs);
      }))
    return;
  (*cinfo->mem->realize_virt_arrays)(common);
  flush_output(cinfo);
  cinfo->next_scanline = 0;
  cinfo->global_state = CSTATE_WRCOEFS;
}

}  // extern "C"

// ===========================================================================
// Decompression
// ===========================================================================

namespace {

// Gathers compressed data from the source: up to the end of the first SOS
// header, or (to_eoi) of the whole image.  Returns false if the source
// suspended.  Calls the source's methods, so it runs in the API frame.
bool gather(j_decompress_ptr cinfo, bool to_eoi) {
  jpeg_decomp_master* m = cinfo->master;
  if (m->input_done || (!to_eoi && m->scan.header_done)) return true;
  jpeg_source_mgr* src = cinfo->src;
  for (;;) {
    if (src->bytes_in_buffer == 0) {
      if (!(*src->fill_input_buffer)(cinfo)) return false;
      continue;
    }
    const JOCTET* p = src->next_input_byte;
    std::size_t n = src->bytes_in_buffer;
    if (m->buf.empty() && !m->source_set) {
      // Use the source's buffer in place if it holds the whole image (an
      // in-memory source always does: its end is the end of the data).
      Scanner probe = m->scan;
      std::size_t k = probe.feed(p, n);
      if (probe.done() || src->init_source == init_mem_source) {
        m->scan = probe;
        m->zero_copy = true;
        m->zc_base = p;
        m->zc_avail = n;
        m->data = p;
        m->size = k;
        m->input_done = true;
        return true;
      }
    }
    // A fake EOI inserted by a source at end of file
    bool fake_eoi = n == 2 && p[0] == 0xFF && p[1] == JPEG_EOI;
    std::size_t k = m->scan.feed(p, n);
    bool ok = true;
    try {
      m->buf.insert(m->buf.end(), p, p + k);
    } catch (...) {
      ok = false;
    }
    if (!ok) ERREXIT1(cinfo, JERR_OUT_OF_MEMORY, 12);
    src->next_input_byte += k;
    src->bytes_in_buffer -= k;
    if (m->scan.done() || fake_eoi) {
      m->input_done = true;
      return true;
    }
    if (!to_eoi && m->scan.header_done) return true;
  }
}

// Hands the gathered data to the decoder (again after more was gathered).
void present(jpeg_decomp_master* m) {
  vd::DState& st = m->d.st;
  const JOCTET* base = m->zero_copy ? m->data : m->buf.data();
  std::size_t size = m->zero_copy ? m->size : m->buf.size();
  if (!m->source_set) {
    m->d.set_source(base, size);
    m->source_set = true;
  } else if (st.next_input_byte >= m->data && st.next_input_byte <= m->data + m->size) {
    std::size_t off = static_cast<std::size_t>(st.next_input_byte - m->data);
    st.next_input_byte = base + off;
    st.bytes_in_buffer = size - off;
  }
  m->data = base;
  m->size = size;
}

// Keeps a zero-copy source positioned where the decoder is.
void sync_source(j_decompress_ptr cinfo) {
  jpeg_decomp_master* m = cinfo->master;
  if (!m->zero_copy) return;
  const vd::DState& st = m->d.st;
  const JOCTET* pos = m->data + m->size;
  if (st.next_input_byte >= m->data && st.next_input_byte <= m->data + m->size)
    pos = st.next_input_byte;
  jpeg_source_mgr* src = m->saved_src ? m->saved_src : cinfo->src;
  src->next_input_byte = pos;
  src->bytes_in_buffer = m->zc_avail - static_cast<std::size_t>(pos - m->zc_base);
}

// Source given to marker processors: the rest of the gathered data.
void ms_init(j_decompress_ptr) {}
boolean ms_fill(j_decompress_ptr cinfo) {
  WARNMS(cinfo, JWRN_JPEG_EOF);
  cinfo->src->next_input_byte = kFakeEOI;
  cinfo->src->bytes_in_buffer = 2;
  return TRUE;
}
void ms_term(j_decompress_ptr) {}

// Calls the application's processor for the pending marker.  Returns false
// if it suspended.
bool process_custom_marker(j_decompress_ptr cinfo) {
  jpeg_decomp_master* m = cinfo->master;
  vd::DState& st = m->d.st;
  int marker = st.unread_marker;
  jpeg_marker_parser_method routine =
      m->processors[marker == JPEG_COM ? 16 : marker - JPEG_APP0];
  jpeg_source_mgr& ms = m->marker_src;
  ms.next_input_byte = st.next_input_byte;
  ms.bytes_in_buffer = st.bytes_in_buffer;
  ms.init_source = ms_init;
  ms.fill_input_buffer = ms_fill;
  ms.skip_input_data = skip_input_data;
  ms.resync_to_restart = jpeg_resync_to_restart;
  ms.term_source = ms_term;
  m->saved_src = cinfo->src;
  cinfo->src = &ms;
  cinfo->unread_marker = marker;
  boolean ok = (*routine)(cinfo);
  cinfo->src = m->saved_src;
  m->saved_src = nullptr;
  if (!ok) return false;
  if (ms.next_input_byte == kFakeEOI || ms.next_input_byte == kFakeEOI + 2) {
    st.next_input_byte = m->data + m->size;
    st.bytes_in_buffer = 0;
  } else {
    st.next_input_byte = ms.next_input_byte;
    st.bytes_in_buffer = ms.bytes_in_buffer;
  }
  st.unread_marker = 0;
  cinfo->unread_marker = 0;
  return true;
}

void store_progress(j_decompress_ptr cinfo) {
  const vd::DState& st = cinfo->master->d.st;
  cinfo->input_scan_number = st.input_scan_number;
  cinfo->input_iMCU_row = st.input_iMCU_row;
  cinfo->output_scan_number = st.output_scan_number;
  cinfo->output_iMCU_row = st.output_iMCU_row;
  cinfo->unread_marker = st.unread_marker;
  if (cinfo->coef_bits && st.coef_bits.size() >= static_cast<std::size_t>(cinfo->num_components))
    for (std::size_t c = 0; c < st.coef_bits.size(); c++)
      for (int k = 0; k < DCTSIZE2; k++) cinfo->coef_bits[c][k] = st.coef_bits[c][k];
}

// One step of input: returns a JPEG_* code (custom markers are processed
// here); JPEG_SUSPENDED if a marker processor suspended.
int consume_step(j_decompress_ptr cinfo) {
  jpeg_decomp_master* m = cinfo->master;
  for (;;) {
    int ret = 0;
    if (!CALL(cinfo, ret = m->d.consume_input())) return JPEG_SUSPENDED;
    store_progress(cinfo);
    if (ret != vd::REACHED_CUSTOM_MARKER) {
      sync_source(cinfo);
      return ret;
    }
    if (!process_custom_marker(cinfo)) return JPEG_SUSPENDED;
  }
}

// Appends the markers saved since the last call to marker_list.
void store_markers(j_decompress_ptr cinfo) {
  jpeg_decomp_master* m = cinfo->master;
  const auto& list = m->d.st.marker_list;
  for (; m->markers_stored < list.size(); m->markers_stored++) {
    const vio::jpeg::SavedMarker& s = list[m->markers_stored];
    auto* p = static_cast<jpeg_saved_marker_ptr>(
        xalloc(reinterpret_cast<j_common_ptr>(cinfo), JPOOL_IMAGE,
               sizeof(jpeg_marker_struct) + s.data.size()));
    p->next = nullptr;
    p->marker = s.marker;
    p->original_length = s.original_length;
    p->data_length = static_cast<unsigned>(s.data.size());
    p->data = reinterpret_cast<JOCTET*>(p + 1);
    if (!s.data.empty()) std::memcpy(p->data, s.data.data(), s.data.size());
    if (m->marker_tail) m->marker_tail->next = p;
    else cinfo->marker_list = p;
    m->marker_tail = p;
  }
}

void store_dtables(j_decompress_ptr cinfo) {
  const vd::DState& st = cinfo->master->d.st;
  auto common = reinterpret_cast<j_common_ptr>(cinfo);
  for (int i = 0; i < NUM_QUANT_TBLS; i++) {
    if (!st.quant_tbl_ptrs[i]) continue;
    JQUANT_TBL*& t = cinfo->quant_tbl_ptrs[i];
    if (!t) {
      t = static_cast<JQUANT_TBL*>(xalloc(common, JPOOL_PERMANENT, sizeof(JQUANT_TBL)));
      std::memset(t, 0, sizeof(*t));
    }
    for (int k = 0; k < DCTSIZE2; k++) t->quantval[k] = st.quant_tbl_ptrs[i]->quantval[k];
  }
  for (int i = 0; i < NUM_HUFF_TBLS; i++) {
    for (int ac = 0; ac < 2; ac++) {
      const auto& h = ac ? st.ac_huff_tbl_ptrs[i] : st.dc_huff_tbl_ptrs[i];
      if (!h) continue;
      JHUFF_TBL*& t = ac ? cinfo->ac_huff_tbl_ptrs[i] : cinfo->dc_huff_tbl_ptrs[i];
      if (!t) {
        t = static_cast<JHUFF_TBL*>(xalloc(common, JPOOL_PERMANENT, sizeof(JHUFF_TBL)));
        std::memset(t, 0, sizeof(*t));
      }
      std::memcpy(t->bits, h->bits.data(), 17);
      std::memcpy(t->huffval, h->huffval.data(), 256);
    }
  }
}

// Per-component fields from the decoder; latched quantization tables are
// copied too when `latched`.
void store_components(j_decompress_ptr cinfo, bool latched) {
  const vd::DState& st = cinfo->master->d.st;
  if (!cinfo->comp_info) return;
  for (int ci = 0; ci < cinfo->num_components && ci < static_cast<int>(st.comp_info.size()); ci++) {
    const vd::ComponentInfo& c = st.comp_info[ci];
    jpeg_component_info& d = cinfo->comp_info[ci];
    d.component_id = c.component_id;
    d.component_index = ci;
    d.h_samp_factor = c.h_samp_factor;
    d.v_samp_factor = c.v_samp_factor;
    d.quant_tbl_no = c.quant_tbl_no;
    d.dc_tbl_no = c.dc_tbl_no;
    d.ac_tbl_no = c.ac_tbl_no;
    d.width_in_blocks = c.width_in_blocks;
    d.height_in_blocks = c.height_in_blocks;
    D_SET_DCT_SCALED(&d, c.DCT_scaled_size);
    d.downsampled_width = c.downsampled_width;
    d.downsampled_height = c.downsampled_height;
    d.component_needed = c.component_needed;
    d.MCU_width = c.MCU_width;
    d.MCU_height = c.MCU_height;
    d.MCU_blocks = c.MCU_blocks;
    d.MCU_sample_width = c.MCU_sample_width;
    d.last_col_width = c.last_col_width;
    d.last_row_height = c.last_row_height;
    if (latched && c.quant_table && !d.quant_table) {
      d.quant_table = static_cast<JQUANT_TBL*>(
          xalloc(reinterpret_cast<j_common_ptr>(cinfo), JPOOL_IMAGE, sizeof(JQUANT_TBL)));
      for (int k = 0; k < DCTSIZE2; k++) d.quant_table->quantval[k] = c.quant_table->quantval[k];
      d.quant_table->sent_table = FALSE;
    }
  }
  cinfo->comps_in_scan = st.comps_in_scan;
  for (int i = 0; i < MAX_COMPS_IN_SCAN; i++) {
    const vd::ComponentInfo* c = i < st.comps_in_scan ? st.cur_comp_info[i] : nullptr;
    cinfo->cur_comp_info[i] = c ? &cinfo->comp_info[c - st.comp_info.data()] : nullptr;
  }
  cinfo->MCUs_per_row = st.MCUs_per_row;
  cinfo->MCU_rows_in_scan = st.MCU_rows_in_scan;
  cinfo->blocks_in_MCU = st.blocks_in_MCU;
  for (int i = 0; i < D_MAX_BLOCKS_IN_MCU; i++) cinfo->MCU_membership[i] = st.MCU_membership[i];
  cinfo->Ss = st.Ss;
  cinfo->Se = st.Se;
  cinfo->Ah = st.Ah;
  cinfo->Al = st.Al;
}

// The image header and default decompression parameters -> public fields
void store_header(j_decompress_ptr cinfo) {
  jpeg_decomp_master* m = cinfo->master;
  const vd::DState& st = m->d.st;
  cinfo->image_width = st.image_width;
  cinfo->image_height = st.image_height;
  cinfo->num_components = st.num_components;
  cinfo->jpeg_color_space = static_cast<J_COLOR_SPACE>(st.jpeg_color_space);
  cinfo->out_color_space = static_cast<J_COLOR_SPACE>(st.out_color_space);
  cinfo->scale_num = st.scale_num;
  cinfo->scale_denom = st.scale_denom;
  cinfo->output_gamma = 1.0;
  cinfo->buffered_image = st.buffered_image;
  cinfo->raw_data_out = st.raw_data_out;
  cinfo->dct_method = static_cast<J_DCT_METHOD>(st.dct_method);
  cinfo->do_fancy_upsampling = st.do_fancy_upsampling;
  cinfo->do_block_smoothing = st.do_block_smoothing;
  cinfo->quantize_colors = st.quantize_colors;
  cinfo->dither_mode = static_cast<J_DITHER_MODE>(st.dither_mode);
  cinfo->two_pass_quantize = st.two_pass_quantize;
  cinfo->desired_number_of_colors = st.desired_number_of_colors;
  cinfo->enable_1pass_quant = st.enable_1pass_quant;
  cinfo->enable_external_quant = st.enable_external_quant;
  cinfo->enable_2pass_quant = st.enable_2pass_quant;
  cinfo->colormap = nullptr;
  cinfo->data_precision = st.data_precision;
  m->lossless = st.lossless;
  cinfo->comp_info = static_cast<jpeg_component_info*>(
      xalloc(reinterpret_cast<j_common_ptr>(cinfo), JPOOL_IMAGE,
             std::max(st.num_components, 1) * sizeof(jpeg_component_info)));
  std::memset(cinfo->comp_info, 0, std::max(st.num_components, 1) * sizeof(jpeg_component_info));
  store_components(cinfo, false);
#if JPEG_LIB_VERSION >= 80
  cinfo->is_baseline = st.is_baseline;
#endif
  cinfo->progressive_mode = st.progressive_mode;
  cinfo->arith_code = st.arith_code;
  std::memcpy(cinfo->arith_dc_L, st.arith_dc_L, NUM_ARITH_TBLS);
  std::memcpy(cinfo->arith_dc_U, st.arith_dc_U, NUM_ARITH_TBLS);
  std::memcpy(cinfo->arith_ac_K, st.arith_ac_K, NUM_ARITH_TBLS);
  cinfo->restart_interval = st.restart_interval;
  cinfo->saw_JFIF_marker = st.saw_JFIF_marker;
  cinfo->JFIF_major_version = st.JFIF_major_version;
  cinfo->JFIF_minor_version = st.JFIF_minor_version;
  cinfo->density_unit = st.density_unit;
  cinfo->X_density = st.X_density;
  cinfo->Y_density = st.Y_density;
  cinfo->saw_Adobe_marker = st.saw_Adobe_marker;
  cinfo->Adobe_transform = st.Adobe_transform;
  cinfo->CCIR601_sampling = st.CCIR601_sampling;
  cinfo->max_h_samp_factor = st.max_h_samp_factor;
  cinfo->max_v_samp_factor = st.max_v_samp_factor;
  D_SET_MIN_DCT_SCALED(cinfo, st.min_DCT_scaled_size);
  cinfo->total_iMCU_rows = st.total_iMCU_rows;
#if JPEG_LIB_VERSION >= 80
  cinfo->block_size = st.lossless ? 1 : DCTSIZE;
  cinfo->natural_order = jpeg_natural_order;
  cinfo->lim_Se = DCTSIZE2 - 1;
#endif
  store_dtables(cinfo);
  store_markers(cinfo);
  store_progress(cinfo);
}

// Public decompression parameters -> vio
void load_dparams(j_decompress_ptr cinfo) {
  vd::DState& st = cinfo->master->d.st;
  st.image_width = cinfo->image_width;
  st.image_height = cinfo->image_height;
  if (cinfo->num_components != st.num_components) {
    st.num_components = cinfo->num_components;
    st.comp_info.assign(static_cast<std::size_t>(std::max(cinfo->num_components, 0)),
                        vd::ComponentInfo{});
    for (int ci = 0; ci < cinfo->num_components && cinfo->comp_info; ci++) {
      st.comp_info[ci].component_index = ci;
      st.comp_info[ci].h_samp_factor = cinfo->comp_info[ci].h_samp_factor;
      st.comp_info[ci].v_samp_factor = cinfo->comp_info[ci].v_samp_factor;
    }
  }
  if (cinfo->jpeg_color_space < JCS_UNKNOWN || cinfo->jpeg_color_space > JCS_RGB565)
    fail(JERR_BAD_J_COLORSPACE);
  if (cinfo->out_color_space < JCS_UNKNOWN || cinfo->out_color_space > JCS_RGB565)
    fail(JERR_CONVERSION_NOTIMPL);
  st.jpeg_color_space = static_cast<ColorSpace>(cinfo->jpeg_color_space);
  st.out_color_space = static_cast<ColorSpace>(cinfo->out_color_space);
  st.scale_num = cinfo->scale_num;
  st.scale_denom = cinfo->scale_denom;
  st.buffered_image = cinfo->buffered_image != 0;
  st.raw_data_out = cinfo->raw_data_out != 0;
  st.dct_method = static_cast<vio::jpeg::DctMethod>(cinfo->dct_method);
  st.do_fancy_upsampling = cinfo->do_fancy_upsampling != 0;
  st.do_block_smoothing = cinfo->do_block_smoothing != 0;
  st.quantize_colors = cinfo->quantize_colors != 0;
  st.dither_mode = static_cast<vio::jpeg::DitherMode>(cinfo->dither_mode);
  st.two_pass_quantize = cinfo->two_pass_quantize != 0;
  st.desired_number_of_colors = cinfo->desired_number_of_colors;
  st.enable_1pass_quant = cinfo->enable_1pass_quant != 0;
  st.enable_external_quant = cinfo->enable_external_quant != 0;
  st.enable_2pass_quant = cinfo->enable_2pass_quant != 0;
  st.data_precision = cinfo->data_precision;
}

// An application-supplied colormap -> vio (needs out_color_components)
void load_colormap(j_decompress_ptr cinfo) {
  jpeg_decomp_master* m = cinfo->master;
  vd::DState& st = m->d.st;
  if (!cinfo->quantize_colors || !cinfo->colormap || cinfo->colormap == m->colormap_ours) return;
  int ncomps = st.out_color_components > 0 ? st.out_color_components : 3;
  int n = cinfo->actual_number_of_colors;
  st.colormap.assign(static_cast<std::size_t>(ncomps), std::vector<int>(static_cast<std::size_t>(std::max(n, 0))));
  for (int c = 0; c < ncomps; c++)
    for (int i = 0; i < n; i++)
      st.colormap[c][i] = cinfo->data_precision > 8
                              ? reinterpret_cast<J12SAMPARRAY>(cinfo->colormap)[c][i]
                              : cinfo->colormap[c][i];
  st.actual_number_of_colors = n;
  st.colormap_external = true;
}

template <class T>
JSAMPLE* range_limit(jpeg_decomp_master* m) {
  return reinterpret_cast<JSAMPLE*>(const_cast<T*>(m->d.pipeline<T>().range_.limit()));
}

// Output description (after jpeg_calc_output_dimensions()) -> public fields
void store_output_dims(j_decompress_ptr cinfo) {
  const vd::DState& st = cinfo->master->d.st;
  cinfo->output_width = st.output_width;
  cinfo->output_height = st.output_height;
  cinfo->out_color_components = st.out_color_components;
  cinfo->output_components = st.output_components;
  cinfo->rec_outbuf_height = st.rec_outbuf_height;
  D_SET_MIN_DCT_SCALED(cinfo, st.min_DCT_scaled_size);
  store_components(cinfo, false);
}

// State after starting decompression -> public fields
void store_output(j_decompress_ptr cinfo) {
  jpeg_decomp_master* m = cinfo->master;
  const vd::DState& st = m->d.st;
  auto common = reinterpret_cast<j_common_ptr>(cinfo);
  store_output_dims(cinfo);
  cinfo->quantize_colors = st.quantize_colors;
  cinfo->enable_1pass_quant = st.enable_1pass_quant;
  cinfo->enable_external_quant = st.enable_external_quant;
  cinfo->enable_2pass_quant = st.enable_2pass_quant;
  cinfo->output_scanline = st.output_scanline;
  if (st.quantize_colors && !st.colormap_external && !st.colormap.empty()) {
    int n = st.actual_number_of_colors;
    int ncomps = static_cast<int>(st.colormap.size());
    if (!m->colormap_ours || cinfo->colormap != m->colormap_ours) {
      m->colormap_ours = reinterpret_cast<JSAMPARRAY>(
          alloc_rows(common, JPOOL_IMAGE, sample_size(common), static_cast<JDIMENSION>(n),
                     static_cast<JDIMENSION>(ncomps), true));
      cinfo->colormap = m->colormap_ours;
    }
    for (int c = 0; c < ncomps; c++)
      for (int i = 0; i < n; i++) {
        if (cinfo->data_precision > 8)
          reinterpret_cast<J12SAMPARRAY>(m->colormap_ours)[c][i] =
              static_cast<J12SAMPLE>(st.colormap[c][i]);
        else
          m->colormap_ours[c][i] = static_cast<JSAMPLE>(st.colormap[c][i]);
      }
  }
  if (st.quantize_colors) cinfo->actual_number_of_colors = st.actual_number_of_colors;
  if (m->d.pipeline_base()) {
    int bits = m->d.pipeline_base()->sample_bits();
    cinfo->sample_range_limit = bits <= 8    ? range_limit<std::uint8_t>(m)
                                : bits <= 12 ? range_limit<std::int16_t>(m)
                                             : range_limit<std::uint16_t>(m);
  }
  if (st.progressive_mode && !cinfo->coef_bits && !st.coef_bits.empty()) {
    cinfo->coef_bits = static_cast<int(*)[DCTSIZE2]>(
        xalloc(common, JPOOL_IMAGE, st.coef_bits.size() * DCTSIZE2 * sizeof(int)));
  }
  store_progress(cinfo);
}

void init_dprogress(j_decompress_ptr cinfo) {
  const vd::DState& st = cinfo->master->d.st;
  if (!cinfo->progress) return;
  int nscans = st.has_multiple_scans
                   ? (st.progressive_mode ? 2 + 3 * st.num_components : st.num_components)
                   : 1;
  cinfo->progress->pass_counter = 0L;
  cinfo->progress->pass_limit = static_cast<long>(st.total_iMCU_rows) * nscans;
  cinfo->progress->completed_passes = 0;
  cinfo->progress->total_passes = st.enable_2pass_quant ? 3 : 2;
}

void advance_progress(j_decompress_ptr cinfo, int ret) {
  if (cinfo->progress && (ret == JPEG_ROW_COMPLETED || ret == JPEG_REACHED_SOS)) {
    if (++cinfo->progress->pass_counter >= cinfo->progress->pass_limit)
      cinfo->progress->pass_limit += static_cast<long>(cinfo->total_iMCU_rows);
  }
}

// Output pass setup, including any dummy pass (two-pass quantization).
bool output_pass_setup(j_decompress_ptr cinfo) {
  jpeg_decomp_master* m = cinfo->master;
  if (!CALL(cinfo, {
        if (cinfo->global_state != DSTATE_PRESCAN)
          m->d.state = vd::Decompressor::State::Preload;
        m->d.output_pass_setup();
        store_output(cinfo);
      }))
    return false;
  if (cinfo->progress) cinfo->progress->completed_passes++;
  cinfo->global_state = cinfo->raw_data_out ? DSTATE_RAW_OK : DSTATE_SCANNING;
  return true;
}

template <class T>
JDIMENSION read_scanlines(j_decompress_ptr cinfo, T** scanlines, JDIMENSION max_lines) {
  auto common = reinterpret_cast<j_common_ptr>(cinfo);
  jpeg_decomp_master* m = cinfo->master;
  check_precision<T>(common, cinfo->data_precision, m->lossless);
  check_state(common, DSTATE_SCANNING);
  if (cinfo->output_scanline >= cinfo->output_height) {
    WARNMS(cinfo, JWRN_TOO_MUCH_DATA);
    return 0;
  }
  progress(common, static_cast<long>(cinfo->output_scanline), static_cast<long>(cinfo->output_height));
  JDIMENSION rows = 0;
  for (;;) {
    if (!CALL(cinfo, rows = m->d.read_scanlines<T>(scanlines, max_lines))) return 0;
    // Buffered-image mode: input stops at markers left to the application.
    if (rows == 0 && m->d.st.unread_marker && cinfo->buffered_image) {
      int marker = m->d.st.unread_marker;
      bool custom = (marker == JPEG_COM && m->processors[16]) ||
                    (marker >= JPEG_APP0 && marker <= JPEG_APP0 + 15 &&
                     m->processors[marker - JPEG_APP0]);
      if (custom && m->d.st.consuming_markers && process_custom_marker(cinfo)) continue;
    }
    break;
  }
  cinfo->output_scanline = m->d.st.output_scanline;
  store_progress(cinfo);
  return rows;
}

template <class T>
JDIMENSION skip_scanlines(j_decompress_ptr cinfo, JDIMENSION num_lines) {
  auto common = reinterpret_cast<j_common_ptr>(cinfo);
  jpeg_decomp_master* m = cinfo->master;
  check_precision<T>(common, cinfo->data_precision, false);
  if (m->lossless) ERREXIT(cinfo, JERR_NOTIMPL);
  check_state(common, DSTATE_SCANNING);
  JDIMENSION rows = 0;
  CALL(cinfo, rows = m->d.skip_scanlines<T>(num_lines));
  cinfo->output_scanline = m->d.st.output_scanline;
  store_progress(cinfo);
  return rows;
}

template <class T>
void crop_scanline(j_decompress_ptr cinfo, JDIMENSION* xoffset, JDIMENSION* width) {
  auto common = reinterpret_cast<j_common_ptr>(cinfo);
  jpeg_decomp_master* m = cinfo->master;
  check_precision<T>(common, cinfo->data_precision, false);
  if (m->lossless || cinfo->raw_data_out) ERREXIT(cinfo, JERR_NOTIMPL);
  if (cinfo->global_state != DSTATE_SCANNING || cinfo->output_scanline != 0)
    ERREXIT1(cinfo, JERR_BAD_STATE, cinfo->global_state);
  if (!xoffset || !width) ERREXIT(cinfo, JERR_BAD_CROP_SPEC);
  CALL(cinfo, {
    m->d.crop_scanline<T>(xoffset, width);
    store_output_dims(cinfo);
  });
}

template <class T>
JDIMENSION read_raw_data(j_decompress_ptr cinfo, T*** data, JDIMENSION max_lines) {
  auto common = reinterpret_cast<j_common_ptr>(cinfo);
  jpeg_decomp_master* m = cinfo->master;
  check_precision<T>(common, cinfo->data_precision, false);
  check_state(common, DSTATE_RAW_OK);
  if (cinfo->output_scanline >= cinfo->output_height) {
    WARNMS(cinfo, JWRN_TOO_MUCH_DATA);
    return 0;
  }
  progress(common, static_cast<long>(cinfo->output_scanline), static_cast<long>(cinfo->output_height));
  JDIMENSION rows = 0;
  CALL(cinfo, rows = m->d.read_raw_data<T>(data, max_lines));
  cinfo->output_scanline = m->d.st.output_scanline;
  store_progress(cinfo);
  return rows;
}

}  // namespace

extern "C" {

JEXPORT void jpeg_CreateDecompress(j_decompress_ptr cinfo, int version, size_t structsize) {
  cinfo->mem = nullptr;
  if (version != JPEG_LIB_VERSION) ERREXIT2(cinfo, JERR_BAD_LIB_VERSION, JPEG_LIB_VERSION, version);
  if (structsize != sizeof(jpeg_decompress_struct))
    ERREXIT2(cinfo, JERR_BAD_STRUCT_SIZE, static_cast<int>(sizeof(jpeg_decompress_struct)),
             static_cast<int>(structsize));
  {
    jpeg_error_mgr* err = cinfo->err;
    void* client_data = cinfo->client_data;
    std::memset(static_cast<void*>(cinfo), 0, sizeof(jpeg_decompress_struct));
    cinfo->err = err;
    cinfo->client_data = client_data;
  }
  cinfo->is_decompressor = TRUE;
  init_memory_mgr(reinterpret_cast<j_common_ptr>(cinfo));
  cinfo->progress = nullptr;
  cinfo->src = nullptr;
  for (int i = 0; i < NUM_QUANT_TBLS; i++) cinfo->quant_tbl_ptrs[i] = nullptr;
  for (int i = 0; i < NUM_HUFF_TBLS; i++) {
    cinfo->dc_huff_tbl_ptrs[i] = nullptr;
    cinfo->ac_huff_tbl_ptrs[i] = nullptr;
  }
  cinfo->marker_list = nullptr;
  cinfo->data_precision = 8;
  cinfo->global_state = DSTATE_START;
  cinfo->master = new (std::nothrow) jpeg_decomp_master;
  if (!cinfo->master) ERREXIT1(cinfo, JERR_OUT_OF_MEMORY, 0);
}

JEXPORT void jpeg_destroy_decompress(j_decompress_ptr cinfo) {
  jpeg_destroy(reinterpret_cast<j_common_ptr>(cinfo));
}

JEXPORT void jpeg_abort_decompress(j_decompress_ptr cinfo) {
  jpeg_abort(reinterpret_cast<j_common_ptr>(cinfo));
}

JEXPORT void jpeg_save_markers(j_decompress_ptr cinfo, int marker_code, unsigned int length_limit) {
  jpeg_decomp_master* m = cinfo->master;
  long maxlength = cinfo->mem->max_alloc_chunk - static_cast<long>(sizeof(jpeg_marker_struct));
  if (static_cast<long>(length_limit) > maxlength) length_limit = static_cast<unsigned>(maxlength);
  if (marker_code != JPEG_COM && (marker_code < JPEG_APP0 || marker_code > JPEG_APP0 + 15))
    ERREXIT1(cinfo, JERR_UNKNOWN_MARKER, marker_code);
  CALL(cinfo, m->d.st.save_markers(marker_code, length_limit));
  int n = marker_code == JPEG_COM ? 16 : marker_code - JPEG_APP0;
  m->processors[n] = nullptr;
  m->d.st.custom_markers &= ~(1u << n);
}

JEXPORT void jpeg_set_marker_processor(j_decompress_ptr cinfo, int marker_code,
                                       jpeg_marker_parser_method routine) {
  jpeg_decomp_master* m = cinfo->master;
  if (marker_code != JPEG_COM && (marker_code < JPEG_APP0 || marker_code > JPEG_APP0 + 15))
    ERREXIT1(cinfo, JERR_UNKNOWN_MARKER, marker_code);
  int n = marker_code == JPEG_COM ? 16 : marker_code - JPEG_APP0;
  m->processors[n] = routine;
  if (routine) m->d.st.custom_markers |= 1u << n;
  else m->d.st.custom_markers &= ~(1u << n);
}

JEXPORT int jpeg_consume_input(j_decompress_ptr cinfo) {
  jpeg_decomp_master* m = cinfo->master;
  int retcode = JPEG_SUSPENDED;
  switch (cinfo->global_state) {
    case DSTATE_START:
      abort_decompress_state(cinfo);
      m->d.st.marker_list.clear();
      (*cinfo->src->init_source)(cinfo);
      cinfo->global_state = DSTATE_INHEADER;
      [[fallthrough]];
    case DSTATE_INHEADER:
      if (!gather(cinfo, false)) return JPEG_SUSPENDED;
      if (!CALL(cinfo, present(m))) return JPEG_SUSPENDED;
      retcode = consume_step(cinfo);
      if (retcode == JPEG_REACHED_SOS) {
        if (!CALL(cinfo, store_header(cinfo))) return JPEG_SUSPENDED;
        cinfo->global_state = DSTATE_READY;
      }
      break;
    case DSTATE_READY:
      retcode = JPEG_REACHED_SOS;
      break;
    case DSTATE_PRELOAD:
    case DSTATE_PRESCAN:
    case DSTATE_SCANNING:
    case DSTATE_RAW_OK:
    case DSTATE_BUFIMAGE:
    case DSTATE_BUFPOST:
    case DSTATE_STOPPING:
      if (!gather(cinfo, true)) return JPEG_SUSPENDED;
      if (!CALL(cinfo, present(m))) return JPEG_SUSPENDED;
      retcode = consume_step(cinfo);
      CALL(cinfo, store_markers(cinfo));
      break;
    default:
      ERREXIT1(cinfo, JERR_BAD_STATE, cinfo->global_state);
  }
  return retcode;
}

JEXPORT int jpeg_read_header(j_decompress_ptr cinfo, boolean require_image) {
  if (cinfo->global_state != DSTATE_START && cinfo->global_state != DSTATE_INHEADER)
    ERREXIT1(cinfo, JERR_BAD_STATE, cinfo->global_state);
  int retcode = jpeg_consume_input(cinfo);
  switch (retcode) {
    case JPEG_REACHED_SOS:
      retcode = JPEG_HEADER_OK;
      break;
    case JPEG_REACHED_EOI:
      if (require_image) ERREXIT(cinfo, JERR_NO_IMAGE);
      // Tables-only datastream: the tables are kept for the next image.
      jpeg_abort(reinterpret_cast<j_common_ptr>(cinfo));
      retcode = JPEG_HEADER_TABLES_ONLY;
      break;
    case JPEG_SUSPENDED:
      break;
  }
  return retcode;
}

JEXPORT boolean jpeg_has_multiple_scans(j_decompress_ptr cinfo) {
  if (cinfo->global_state < DSTATE_READY || cinfo->global_state > DSTATE_STOPPING)
    ERREXIT1(cinfo, JERR_BAD_STATE, cinfo->global_state);
  return cinfo->master->d.st.has_multiple_scans;
}

JEXPORT boolean jpeg_input_complete(j_decompress_ptr cinfo) {
  if (cinfo->global_state < DSTATE_START || cinfo->global_state > DSTATE_STOPPING)
    ERREXIT1(cinfo, JERR_BAD_STATE, cinfo->global_state);
  return cinfo->master->d.st.eoi_reached;
}

#if JPEG_LIB_VERSION >= 80
JEXPORT void jpeg_core_output_dimensions(j_decompress_ptr cinfo) {
  jpeg_decomp_master* m = cinfo->master;
  check_state(reinterpret_cast<j_common_ptr>(cinfo), DSTATE_READY);
  CALL(cinfo, {
    load_dparams(cinfo);
    m->d.state = vd::Decompressor::State::Ready;
    m->d.calc_output_dimensions();
    const vd::DState& st = m->d.st;
    cinfo->output_width = st.output_width;
    cinfo->output_height = st.output_height;
    D_SET_MIN_DCT_SCALED(cinfo, st.min_DCT_scaled_size);
    store_components(cinfo, false);
  });
}
#endif

JEXPORT void jpeg_calc_output_dimensions(j_decompress_ptr cinfo) {
  jpeg_decomp_master* m = cinfo->master;
  check_state(reinterpret_cast<j_common_ptr>(cinfo), DSTATE_READY);
  CALL(cinfo, {
    load_dparams(cinfo);
    m->d.state = vd::Decompressor::State::Ready;
    m->d.calc_output_dimensions();
    store_output_dims(cinfo);
  });
}

JEXPORT boolean jpeg_start_decompress(j_decompress_ptr cinfo) {
  auto common = reinterpret_cast<j_common_ptr>(cinfo);
  jpeg_decomp_master* m = cinfo->master;
  if (cinfo->global_state == DSTATE_READY) {
    if (!gather(cinfo, true)) return FALSE;
    if (!CALL(cinfo, {
          present(m);
          load_dparams(cinfo);
          m->d.state = vd::Decompressor::State::Ready;
          m->d.calc_output_dimensions();
          load_colormap(cinfo);
          m->d.master_selection();
          m->d.state = vd::Decompressor::State::Preload;
          store_output_dims(cinfo);
        }))
      return FALSE;
    (*cinfo->mem->realize_virt_arrays)(common);
    init_dprogress(cinfo);
    if (cinfo->buffered_image) {
      CALL(cinfo, store_output(cinfo));
      cinfo->global_state = DSTATE_BUFIMAGE;
      return TRUE;
    }
    cinfo->global_state = DSTATE_PRELOAD;
  }
  if (cinfo->global_state == DSTATE_PRELOAD) {
    if (m->d.st.has_multiple_scans) {
      for (;;) {
        if (cinfo->progress) (*cinfo->progress->progress_monitor)(common);
        int retcode = consume_step(cinfo);
        if (retcode == JPEG_SUSPENDED) return FALSE;
        if (retcode == JPEG_REACHED_EOI) break;
        advance_progress(cinfo, retcode);
      }
    }
    m->d.st.output_scan_number = m->d.st.input_scan_number;
  } else if (cinfo->global_state != DSTATE_PRESCAN) {
    ERREXIT1(cinfo, JERR_BAD_STATE, cinfo->global_state);
  }
  if (!output_pass_setup(cinfo)) return FALSE;
  CALL(cinfo, store_markers(cinfo));
  return TRUE;
}

JEXPORT JDIMENSION jpeg_read_scanlines(j_decompress_ptr cinfo, JSAMPARRAY scanlines,
                                       JDIMENSION max_lines) {
  return read_scanlines<JSAMPLE>(cinfo, scanlines, max_lines);
}

JEXPORT JDIMENSION jpeg12_read_scanlines(j_decompress_ptr cinfo, J12SAMPARRAY scanlines,
                                         JDIMENSION max_lines) {
  return read_scanlines<J12SAMPLE>(cinfo, scanlines, max_lines);
}

JEXPORT JDIMENSION jpeg16_read_scanlines(j_decompress_ptr cinfo, J16SAMPARRAY scanlines,
                                         JDIMENSION max_lines) {
  return read_scanlines<J16SAMPLE>(cinfo, scanlines, max_lines);
}

JEXPORT JDIMENSION jpeg_skip_scanlines(j_decompress_ptr cinfo, JDIMENSION num_lines) {
  return skip_scanlines<JSAMPLE>(cinfo, num_lines);
}

JEXPORT JDIMENSION jpeg12_skip_scanlines(j_decompress_ptr cinfo, JDIMENSION num_lines) {
  return skip_scanlines<J12SAMPLE>(cinfo, num_lines);
}

JEXPORT void jpeg_crop_scanline(j_decompress_ptr cinfo, JDIMENSION* xoffset, JDIMENSION* width) {
  crop_scanline<JSAMPLE>(cinfo, xoffset, width);
}

JEXPORT void jpeg12_crop_scanline(j_decompress_ptr cinfo, JDIMENSION* xoffset,
                                  JDIMENSION* width) {
  crop_scanline<J12SAMPLE>(cinfo, xoffset, width);
}

JEXPORT JDIMENSION jpeg_read_raw_data(j_decompress_ptr cinfo, JSAMPIMAGE data,
                                      JDIMENSION max_lines) {
  return read_raw_data<JSAMPLE>(cinfo, data, max_lines);
}

JEXPORT JDIMENSION jpeg12_read_raw_data(j_decompress_ptr cinfo, J12SAMPIMAGE data,
                                        JDIMENSION max_lines) {
  return read_raw_data<J12SAMPLE>(cinfo, data, max_lines);
}

JEXPORT boolean jpeg_finish_decompress(j_decompress_ptr cinfo) {
  auto common = reinterpret_cast<j_common_ptr>(cinfo);
  jpeg_decomp_master* m = cinfo->master;
  if ((cinfo->global_state == DSTATE_SCANNING || cinfo->global_state == DSTATE_RAW_OK) &&
      !cinfo->buffered_image) {
    if (cinfo->output_scanline < cinfo->output_height) ERREXIT(cinfo, JERR_TOO_LITTLE_DATA);
    if (!CALL(cinfo, m->d.pipeline_base()->finish_output_pass())) return FALSE;
    cinfo->global_state = DSTATE_STOPPING;
  } else if (cinfo->global_state == DSTATE_BUFIMAGE) {
    cinfo->global_state = DSTATE_STOPPING;
  } else if (cinfo->global_state != DSTATE_STOPPING) {
    ERREXIT1(cinfo, JERR_BAD_STATE, cinfo->global_state);
  }
  m->d.state = vd::Decompressor::State::Stopping;
  if (!gather(cinfo, true)) return FALSE;
  if (!CALL(cinfo, present(m))) return FALSE;
  while (!m->d.st.eoi_reached) {
    if (consume_step(cinfo) == JPEG_SUSPENDED) return FALSE;
  }
  sync_source(cinfo);
  (*cinfo->src->term_source)(cinfo);
  jpeg_abort(common);
  return TRUE;
}

JEXPORT boolean jpeg_start_output(j_decompress_ptr cinfo, int scan_number) {
  jpeg_decomp_master* m = cinfo->master;
  if (cinfo->global_state != DSTATE_BUFIMAGE && cinfo->global_state != DSTATE_PRESCAN)
    ERREXIT1(cinfo, JERR_BAD_STATE, cinfo->global_state);
  vd::DState& st = m->d.st;
  if (scan_number <= 0) scan_number = 1;
  if (st.eoi_reached && scan_number > st.input_scan_number) scan_number = st.input_scan_number;
  st.output_scan_number = scan_number;
  // Parameters that may change between output passes
  st.dct_method = static_cast<vio::jpeg::DctMethod>(cinfo->dct_method);
  st.do_block_smoothing = cinfo->do_block_smoothing != 0;
  st.dither_mode = static_cast<vio::jpeg::DitherMode>(cinfo->dither_mode);
  st.two_pass_quantize = cinfo->two_pass_quantize != 0;
  st.quantize_colors = cinfo->quantize_colors != 0;
  return output_pass_setup(cinfo);
}

JEXPORT boolean jpeg_finish_output(j_decompress_ptr cinfo) {
  jpeg_decomp_master* m = cinfo->master;
  if ((cinfo->global_state == DSTATE_SCANNING || cinfo->global_state == DSTATE_RAW_OK) &&
      cinfo->buffered_image) {
    if (!CALL(cinfo, m->d.pipeline_base()->finish_output_pass())) return FALSE;
    cinfo->global_state = DSTATE_BUFPOST;
  } else if (cinfo->global_state != DSTATE_BUFPOST) {
    ERREXIT1(cinfo, JERR_BAD_STATE, cinfo->global_state);
  }
  // Read markers up to the next scan (or the end of the image)
  while (m->d.st.input_scan_number <= m->d.st.output_scan_number && !m->d.st.eoi_reached) {
    if (consume_step(cinfo) == JPEG_SUSPENDED) return FALSE;
  }
  CALL(cinfo, store_markers(cinfo));
  cinfo->global_state = DSTATE_BUFIMAGE;
  return TRUE;
}

JEXPORT void jpeg_new_colormap(j_decompress_ptr cinfo) {
  jpeg_decomp_master* m = cinfo->master;
  check_state(reinterpret_cast<j_common_ptr>(cinfo), DSTATE_BUFIMAGE);
  if (!cinfo->quantize_colors || !cinfo->enable_external_quant || !cinfo->colormap)
    ERREXIT(cinfo, JERR_MODE_CHANGE);
  CALL(cinfo, {
    m->colormap_ours = nullptr;
    load_colormap(cinfo);
    auto select = [&](auto& p) {
      if (!p.quant2_) fail(JERR_MODE_CHANGE);
      p.quant2_->set_external_colormap(m->d.st.colormap);
      p.cquantize = p.quant2_.get();
    };
    int bits = m->d.pipeline_base()->sample_bits();
    if (bits <= 8) select(m->d.pipeline<std::uint8_t>());
    else if (bits <= 12) select(m->d.pipeline<std::int16_t>());
    else fail(JERR_MODE_CHANGE);
    m->d.st.is_dummy_pass = false;
  });
}

JEXPORT jvirt_barray_ptr* jpeg_read_coefficients(j_decompress_ptr cinfo) {
  auto common = reinterpret_cast<j_common_ptr>(cinfo);
  jpeg_decomp_master* m = cinfo->master;
  if (m->lossless) ERREXIT(cinfo, JERR_NOTIMPL);
  if (cinfo->global_state == DSTATE_READY) {
    if (!gather(cinfo, true)) return nullptr;
    if (!CALL(cinfo, {
          present(m);
          load_dparams(cinfo);
          m->d.state = vd::Decompressor::State::Ready;
          m->d.begin_read_coefficients();
          store_components(cinfo, true);
        }))
      return nullptr;
    cinfo->buffered_image = TRUE;
    (*cinfo->mem->realize_virt_arrays)(common);
    if (cinfo->progress) {
      int nscans = cinfo->progressive_mode ? 2 + 3 * cinfo->num_components
                   : m->d.st.has_multiple_scans ? cinfo->num_components : 1;
      cinfo->progress->pass_counter = 0L;
      cinfo->progress->pass_limit = static_cast<long>(cinfo->total_iMCU_rows) * nscans;
      cinfo->progress->completed_passes = 0;
      cinfo->progress->total_passes = 1;
    }
    cinfo->global_state = DSTATE_RDCOEFS;
  }
  if (cinfo->global_state == DSTATE_RDCOEFS) {
    for (;;) {
      if (cinfo->progress) (*cinfo->progress->progress_monitor)(common);
      int retcode = consume_step(cinfo);
      if (retcode == JPEG_SUSPENDED) return nullptr;
      if (retcode == JPEG_REACHED_EOI) break;
      advance_progress(cinfo, retcode);
    }
    cinfo->global_state = DSTATE_STOPPING;
  }
  if ((cinfo->global_state == DSTATE_STOPPING || cinfo->global_state == DSTATE_BUFIMAGE) &&
      cinfo->buffered_image && m->d.st.coef && m->d.st.coef->coef_arrays) {
    if (!m->coef_arrays) {
      CALL(cinfo, {
        store_components(cinfo, true);
        store_markers(cinfo);
        vd::BlockArray* arrays = m->d.st.coef->coef_arrays;
        int n = cinfo->num_components;
        auto* list = static_cast<jvirt_barray_ptr*>(
            xalloc(common, JPOOL_IMAGE, std::max(n, 1) * sizeof(jvirt_barray_ptr)));
        for (int ci = 0; ci < n; ci++) {
          vd::BlockArray& a = arrays[ci];
          auto* ctl = static_cast<jvirt_barray_ptr>(
              xalloc(common, JPOOL_IMAGE, sizeof(jvirt_barray_control)));
          ctl->rows_in_array = static_cast<JDIMENSION>(a.num_rows());
          ctl->blocksperrow = static_cast<JDIMENSION>(a.blocks_per_row());
          ctl->maxaccess = ctl->rows_in_array;
          ctl->first_undef_row = ctl->rows_in_array;
          ctl->pre_zero = FALSE;
          ctl->next = nullptr;
          ctl->base = reinterpret_cast<JBLOCKROW>(a.row(0));
          ctl->rows = static_cast<JBLOCKARRAY>(xalloc(
              common, JPOOL_IMAGE, std::max<JDIMENSION>(ctl->rows_in_array, 1) * sizeof(JBLOCKROW)));
          for (JDIMENSION r = 0; r < ctl->rows_in_array; r++)
            ctl->rows[r] = reinterpret_cast<JBLOCKROW>(a.row(r));
          list[ci] = ctl;
        }
        m->coef_arrays = list;
      });
    }
    return m->coef_arrays;
  }
  ERREXIT1(cinfo, JERR_BAD_STATE, cinfo->global_state);
  return nullptr;
}

JEXPORT void jpeg_copy_critical_parameters(j_decompress_ptr srcinfo, j_compress_ptr dstinfo) {
  if (srcinfo->master->lossless) ERREXIT(dstinfo, JERR_NOTIMPL);
  check_state(reinterpret_cast<j_common_ptr>(dstinfo), CSTATE_START);
  dstinfo->image_width = srcinfo->image_width;
  dstinfo->image_height = srcinfo->image_height;
  dstinfo->input_components = srcinfo->num_components;
  dstinfo->in_color_space = srcinfo->jpeg_color_space;
#if JPEG_LIB_VERSION >= 70
  dstinfo->jpeg_width = srcinfo->output_width;
  dstinfo->jpeg_height = srcinfo->output_height;
  dstinfo->min_DCT_h_scaled_size = srcinfo->min_DCT_h_scaled_size;
  dstinfo->min_DCT_v_scaled_size = srcinfo->min_DCT_v_scaled_size;
#endif
  jpeg_set_defaults(dstinfo);
  // jpeg_set_defaults() may choose the wrong colorspace (e.g. YCbCr for RGB)
  jpeg_set_colorspace(dstinfo, srcinfo->jpeg_color_space);
  dstinfo->data_precision = srcinfo->data_precision;
  dstinfo->CCIR601_sampling = srcinfo->CCIR601_sampling;
  for (int tblno = 0; tblno < NUM_QUANT_TBLS; tblno++) {
    if (!srcinfo->quant_tbl_ptrs[tblno]) continue;
    JQUANT_TBL** qtblptr = &dstinfo->quant_tbl_ptrs[tblno];
    if (!*qtblptr) *qtblptr = jpeg_alloc_quant_table(reinterpret_cast<j_common_ptr>(dstinfo));
    std::memcpy((*qtblptr)->quantval, srcinfo->quant_tbl_ptrs[tblno]->quantval,
                sizeof((*qtblptr)->quantval));
    (*qtblptr)->sent_table = FALSE;
  }
  dstinfo->num_components = srcinfo->num_components;
  if (dstinfo->num_components < 1 || dstinfo->num_components > MAX_COMPONENTS)
    ERREXIT2(dstinfo, JERR_COMPONENT_COUNT, dstinfo->num_components, MAX_COMPONENTS);
  for (int ci = 0; ci < dstinfo->num_components; ci++) {
    const jpeg_component_info* incomp = &srcinfo->comp_info[ci];
    jpeg_component_info* outcomp = &dstinfo->comp_info[ci];
    outcomp->component_id = incomp->component_id;
    outcomp->h_samp_factor = incomp->h_samp_factor;
    outcomp->v_samp_factor = incomp->v_samp_factor;
    outcomp->quant_tbl_no = incomp->quant_tbl_no;
    // The table latched for the component must still be in its slot (an
    // encoder cannot redefine a table between scans).
    int tblno = outcomp->quant_tbl_no;
    if (tblno < 0 || tblno >= NUM_QUANT_TBLS || !srcinfo->quant_tbl_ptrs[tblno])
      ERREXIT1(dstinfo, JERR_NO_QUANT_TABLE, tblno);
    const JQUANT_TBL* slot_quant = srcinfo->quant_tbl_ptrs[tblno];
    const JQUANT_TBL* c_quant = incomp->quant_table;
    if (c_quant)
      for (int k = 0; k < DCTSIZE2; k++)
        if (c_quant->quantval[k] != slot_quant->quantval[k])
          ERREXIT1(dstinfo, JERR_MISMATCHED_QUANT_TABLE, tblno);
  }
  // JFIF version (unless mislabeled) and resolution
  if (srcinfo->saw_JFIF_marker) {
    if (srcinfo->JFIF_major_version == 1) {
      dstinfo->JFIF_major_version = srcinfo->JFIF_major_version;
      dstinfo->JFIF_minor_version = srcinfo->JFIF_minor_version;
    }
    dstinfo->density_unit = srcinfo->density_unit;
    dstinfo->X_density = srcinfo->X_density;
    dstinfo->Y_density = srcinfo->Y_density;
  }
}

JEXPORT boolean jpeg_read_icc_profile(j_decompress_ptr cinfo, JOCTET** icc_data_ptr,
                                      unsigned int* icc_data_len) {
  constexpr unsigned kOverhead = 14;  // "ICC_PROFILE\0", sequence number, count
  if (!icc_data_ptr || !icc_data_len) ERREXIT(cinfo, JERR_BUFFER_SIZE);
  if (cinfo->global_state < DSTATE_READY) ERREXIT1(cinfo, JERR_BAD_STATE, cinfo->global_state);
  *icc_data_ptr = nullptr;
  *icc_data_len = 0;
  auto is_icc = [](jpeg_saved_marker_ptr mk) {
    static const char id[12] = {'I', 'C', 'C', '_', 'P', 'R', 'O', 'F', 'I', 'L', 'E', 0};
    return mk->marker == JPEG_APP0 + 2 && mk->data_length >= kOverhead &&
           std::memcmp(mk->data, id, sizeof(id)) == 0;
  };
  int num_markers = 0;
  char marker_present[256] = {};
  unsigned data_length[256] = {}, data_offset[256] = {};
  for (jpeg_saved_marker_ptr mk = cinfo->marker_list; mk; mk = mk->next) {
    if (!is_icc(mk)) continue;
    if (num_markers == 0) {
      num_markers = mk->data[13];
    } else if (num_markers != mk->data[13]) {
      WARNMS(cinfo, JWRN_BOGUS_ICC);
      return FALSE;
    }
    int seq_no = mk->data[12];
    if (seq_no <= 0 || seq_no > num_markers || marker_present[seq_no]) {
      WARNMS(cinfo, JWRN_BOGUS_ICC);
      return FALSE;
    }
    marker_present[seq_no] = 1;
    data_length[seq_no] = mk->data_length - kOverhead;
  }
  if (num_markers == 0) return FALSE;
  unsigned total_length = 0;
  for (int seq_no = 1; seq_no <= num_markers; seq_no++) {
    if (!marker_present[seq_no]) {
      WARNMS(cinfo, JWRN_BOGUS_ICC);
      return FALSE;
    }
    data_offset[seq_no] = total_length;
    total_length += data_length[seq_no];
  }
  if (total_length == 0) {
    WARNMS(cinfo, JWRN_BOGUS_ICC);
    return FALSE;
  }
  auto* icc_data = static_cast<JOCTET*>(std::malloc(total_length));
  if (!icc_data) ERREXIT1(cinfo, JERR_OUT_OF_MEMORY, 11);
  for (jpeg_saved_marker_ptr mk = cinfo->marker_list; mk; mk = mk->next) {
    if (!is_icc(mk)) continue;
    int seq_no = mk->data[12];
    std::memcpy(icc_data + data_offset[seq_no], mk->data + kOverhead, data_length[seq_no]);
  }
  *icc_data_ptr = icc_data;
  *icc_data_len = total_length;
  return TRUE;
}

}  // extern "C"
