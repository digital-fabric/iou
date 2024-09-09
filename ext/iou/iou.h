#ifndef IOU_H
#define IOU_H

#include "ruby.h"
#include <liburing.h>

// debugging
#define OBJ_ID(obj) (NUM2LONG(rb_funcall(obj, rb_intern("object_id"), 0)))
#define INSPECT(str, obj) { printf(str); VALUE s = rb_funcall(obj, rb_intern("inspect"), 0); printf(": %s\n", StringValueCStr(s)); }
#define CALLER() rb_funcall(rb_mKernel, rb_intern("caller"), 0)
#define TRACE_CALLER() INSPECT("caller: ", CALLER())
#define TRACE_FREE(ptr) //printf("Free %p %s:%d\n", ptr, __FILE__, __LINE__)

// branching
#ifndef unlikely
#define unlikely(cond)	__builtin_expect(!!(cond), 0)
#endif

#ifndef likely
#define likely(cond)	__builtin_expect(!!(cond), 1)
#endif

struct buf_ring_descriptor {
  struct io_uring_buf_ring *br;
  size_t br_size;
  // struct io_uring_buf_ring *buf_ring;
  unsigned buf_count;
  unsigned buf_size;
	char *buf_base;
  // size_t buf_ring_size;
};

#define BUFFER_RING_MAX_COUNT 10

typedef struct IOU_t {
  struct io_uring ring;
  unsigned int    ring_initialized;
  unsigned int    op_counter;
  unsigned int    unsubmitted_sqes;
  VALUE           pending_ops;

  struct buf_ring_descriptor brs[BUFFER_RING_MAX_COUNT];
  unsigned int br_counter;
} IOU_t;

struct sa_data {
  struct sockaddr addr;
  socklen_t len;
};

struct read_data {
  VALUE buffer;
  int buffer_offset;
  unsigned bg_id;
  int utf8_encoding;
};

enum op_type {
  OP_accept,
  OP_cancel,
  OP_close,
  OP_emit,
  OP_nop,
  OP_read,
  OP_timeout,
  OP_write
};

typedef struct OpCtx_t {
  enum op_type type;
  VALUE spec;
  VALUE proc;
  union {
    struct __kernel_timespec ts;
    struct sa_data sa;
    struct read_data rd;
  } data;
  int stop_signal;
} OpCtx_t;

extern VALUE mIOU;
extern VALUE cOpCtx;

enum op_type OpCtx_type_get(VALUE self);
void OpCtx_type_set(VALUE self, enum op_type type);

VALUE OpCtx_spec_get(VALUE self);
VALUE OpCtx_proc_get(VALUE self);

struct __kernel_timespec *OpCtx_ts_get(VALUE self);
void OpCtx_ts_set(VALUE self, VALUE value);

struct sa_data *OpCtx_sa_get(VALUE self);

struct read_data *OpCtx_rd_get(VALUE self);
void OpCtx_rd_set(VALUE self, VALUE buffer, int buffer_offset, unsigned bg_id, int utf8_encoding);

int OpCtx_stop_signal_p(VALUE self);
void OpCtx_stop_signal_set(VALUE self);

#endif // IOU_H
