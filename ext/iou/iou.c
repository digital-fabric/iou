#include "iou.h"
#include "ruby/thread.h"

VALUE mIOU;
VALUE cRing;
VALUE cArgumentError;

VALUE SYM_accept;
VALUE SYM_block;
VALUE SYM_buffer;
VALUE SYM_buffer_offset;
VALUE SYM_close;
VALUE SYM_fd;
VALUE SYM_id;
VALUE SYM_len;
VALUE SYM_multishot;
VALUE SYM_op;
VALUE SYM_interval;
VALUE SYM_read;
VALUE SYM_result;
VALUE SYM_timeout;
VALUE SYM_ts;
VALUE SYM_write;

static void IOU_mark(void *ptr) {
  IOU_t *iou = ptr;
  rb_gc_mark_movable(iou->pending_ops);
}

static void IOU_compact(void *ptr) {
  IOU_t *iou = ptr;
  iou->pending_ops = rb_gc_location(iou->pending_ops);
}

static void IOU_free(void *ptr) {
  IOU_t *iou = ptr;
  if (iou->ring_initialized) io_uring_queue_exit(&iou->ring);
}

static size_t IOU_size(const void *ptr) {
  return sizeof(IOU_t);
}

static const rb_data_type_t IOU_type = {
    "IOURing",
    {IOU_mark, IOU_free, IOU_size, IOU_compact},
    0, 0, RUBY_TYPED_FREE_IMMEDIATELY | RUBY_TYPED_WB_PROTECTED
};

static VALUE IOU_allocate(VALUE klass) {
  IOU_t *iou = ALLOC(IOU_t);

  return TypedData_Wrap_Struct(klass, &IOU_type, iou);
}

VALUE IOU_initialize(VALUE self) {
  IOU_t *iou = RTYPEDDATA_DATA(self);

  iou->ring_initialized = 0;
  iou->op_counter = 0;
  iou->unsubmitted_sqes = 0;

  iou->pending_ops = rb_hash_new();

  unsigned prepared_limit = 1024;
  int flags = 0;
  #ifdef HAVE_IORING_SETUP_SUBMIT_ALL
  flags |= IORING_SETUP_SUBMIT_ALL;
  #endif
  #ifdef HAVE_IORING_SETUP_COOP_TASKRUN
  flags |= IORING_SETUP_COOP_TASKRUN;
  #endif

  while (1) {
    int ret = io_uring_queue_init(prepared_limit, &iou->ring, flags);
    if (likely(!ret)) break;

    // if ENOMEM is returned, try with half as much entries
    if (unlikely(ret == -ENOMEM && prepared_limit > 64))
      prepared_limit = prepared_limit / 2;
    else
      rb_syserr_fail(-ret, strerror(-ret));
  }
  iou->ring_initialized = 1;

  return self;
}

VALUE IOU_close(VALUE self) {
  IOU_t *iou = RTYPEDDATA_DATA(self);
  if (!iou->ring_initialized) goto done;

  io_uring_queue_exit(&iou->ring);
  iou->ring_initialized = 0;
done:
  return self;
}

VALUE IOU_closed_p(VALUE self) {
  IOU_t *iou = RTYPEDDATA_DATA(self);
  return iou->ring_initialized ? Qfalse : Qtrue;
}

VALUE IOU_pending_ops(VALUE self) {
  IOU_t *iou = RTYPEDDATA_DATA(self);
  return iou->pending_ops;
}

inline IOU_t *get_iou(VALUE self) {
  IOU_t *iou = RTYPEDDATA_DATA(self);
  if (!iou->ring_initialized)
    rb_raise(rb_eRuntimeError, "IOU ring was not initialized");
  return iou;
}

static inline struct io_uring_sqe *get_sqe(IOU_t *iou) {
  struct io_uring_sqe *sqe;
  sqe = io_uring_get_sqe(&iou->ring);
  if (likely(sqe)) goto done;

  rb_raise(rb_eRuntimeError, "Failed to get SQE");

  // TODO: retry getting SQE?

  // if (likely(backend->pending_sqes))
  //   io_uring_backend_immediate_submit(backend);
  // else {
  //   VALUE resume_value = backend_snooze(&backend->base);
  //   RAISE_IF_EXCEPTION(resume_value);
  // }
done:
  return sqe;
}

static inline void get_required_kwargs(VALUE spec, VALUE *values, int argc, ...) {
  if (TYPE(spec) != T_HASH)
    rb_raise(cArgumentError, "Expected keyword arguments");

  va_list ptr;
  va_start(ptr, argc);
  for (int i = 0; i < argc; i++) {
    VALUE k = va_arg(ptr, VALUE);
    VALUE v = rb_hash_aref(spec, k);
    if (NIL_P(v))
      rb_raise(cArgumentError, "Missing %"PRIsVALUE" value", k);
    values[i] = v;
  }
  va_end(ptr);
}

inline void store_spec(IOU_t *iou, VALUE spec, VALUE id, VALUE op) {
  rb_hash_aset(spec, SYM_id, id);
  rb_hash_aset(spec, SYM_op, op);
  if (rb_block_given_p())
    rb_hash_aset(spec, SYM_block, rb_block_proc());
  rb_hash_aset(iou->pending_ops, id, spec);
}

VALUE prep_cancel_id(IOU_t *iou, unsigned op_id_i) {
  unsigned id_i = ++iou->op_counter;
  VALUE id = UINT2NUM(id_i);

  struct io_uring_sqe *sqe = get_sqe(iou);
  io_uring_prep_cancel64(sqe, op_id_i, 0);
  sqe->user_data = id_i;
  iou->unsubmitted_sqes++;

  return id;
}

VALUE IOU_prep_accept(VALUE self, VALUE spec) {
  // IOU_t *iou = get_iou(self);
  // unsigned id_i = ++iou->op_counter;
  // VALUE id = UINT2NUM(id_i);

  // VALUE values[1];
  // get_required_kwargs(spec, values, 1, SYM_fd);
  // VALUE fd = values[0];
  // VALUE multishot = rb_hash_aref(spec, SYM_multishot);
  // // unsigned flags = RTEST(multishot) ? IORING_TIMEOUT_MULTISHOT : 0;

  // struct io_uring_sqe *sqe = get_sqe(iou);
  // sqe->user_data = id_i;

  // store_spec(iou, spec, id, SYM_accept);

  // io_uring_prep_timeout(sqe, OpSpecData_ts_ptr(time_spec), 0, flags);
  // iou->unsubmitted_sqes++;
  // return id;
  return self;
}

VALUE IOU_prep_cancel(VALUE self, VALUE spec) {
  IOU_t *iou = get_iou(self);

  if (TYPE(spec) == T_FIXNUM)
    return prep_cancel_id(iou, NUM2UINT(spec));
  
  if (TYPE(spec) != T_HASH)
    rb_raise(cArgumentError, "Expected operation id or keyword arguments");

  VALUE id = rb_hash_aref(spec, SYM_id);
  if (!NIL_P(id))
    return prep_cancel_id(iou, NUM2UINT(id));

  rb_raise(cArgumentError, "Missing operation id");
}

VALUE IOU_prep_close(VALUE self, VALUE spec) {
  IOU_t *iou = get_iou(self);
  unsigned id_i = ++iou->op_counter;
  VALUE id = UINT2NUM(id_i);

  VALUE values[1];
  get_required_kwargs(spec, values, 1, SYM_fd);
  VALUE fd = values[0];

  struct io_uring_sqe *sqe = get_sqe(iou);
  sqe->user_data = id_i;

  store_spec(iou, spec, id, SYM_close);

  io_uring_prep_close(sqe, NUM2INT(fd));
  iou->unsubmitted_sqes++;
  return id;
}

VALUE IOU_prep_nop(VALUE self) {
  IOU_t *iou = get_iou(self);
  unsigned id_i = ++iou->op_counter;
  VALUE id = UINT2NUM(id_i);

  struct io_uring_sqe *sqe = get_sqe(iou);
  io_uring_prep_nop(sqe);
  sqe->user_data = id_i;
  iou->unsubmitted_sqes++;

  return id;
}

static inline void * prepare_read_buffer(VALUE buffer, unsigned len, int ofs) {
  unsigned current_len = RSTRING_LEN(buffer);
  if (ofs < 0) ofs = current_len + ofs + 1;
  unsigned new_len = len + (unsigned)ofs;

  if (current_len < new_len)
    rb_str_modify_expand(buffer, new_len);
  else
    rb_str_modify(buffer);
  return RSTRING_PTR(buffer) + ofs;
}

static inline void adjust_read_buffer_len(VALUE buffer, int result, int ofs) {
  rb_str_modify(buffer);
  unsigned len = result > 0 ? (unsigned)result : 0;
  unsigned current_len = RSTRING_LEN(buffer);
  if (ofs < 0) ofs = current_len + ofs + 1;
  rb_str_set_len(buffer, len + (unsigned)ofs);
}

VALUE IOU_prep_read(VALUE self, VALUE spec) {
  IOU_t *iou = get_iou(self);
  unsigned id_i = ++iou->op_counter;
  VALUE id = UINT2NUM(id_i);

  VALUE values[3];
  get_required_kwargs(spec, values, 3, SYM_fd, SYM_buffer, SYM_len);

  VALUE fd = values[0];
  VALUE buffer = values[1];
  VALUE len = values[2];
  unsigned len_i = NUM2UINT(len);

  VALUE buffer_offset = rb_hash_aref(spec, SYM_buffer_offset);
  int buffer_offset_i = NIL_P(buffer_offset) ? 0 : NUM2INT(buffer_offset);

  struct io_uring_sqe *sqe = get_sqe(iou);
  sqe->user_data = id_i;

  store_spec(iou, spec, id, SYM_read);

  void *ptr = prepare_read_buffer(buffer, len_i, buffer_offset_i);
  io_uring_prep_read(sqe, NUM2INT(fd), ptr, len_i, -1);
  iou->unsubmitted_sqes++;
  return id;
}

VALUE IOU_prep_timeout(VALUE self, VALUE spec) {
  IOU_t *iou = get_iou(self);
  unsigned id_i = ++iou->op_counter;
  VALUE id = UINT2NUM(id_i);

  VALUE values[1];
  get_required_kwargs(spec, values, 1, SYM_interval);
  VALUE interval = values[0];
  VALUE multishot = rb_hash_aref(spec, SYM_multishot);
  unsigned flags = RTEST(multishot) ? IORING_TIMEOUT_MULTISHOT : 0;

  VALUE time_spec = rb_funcall(cOpSpecData, rb_intern("new"), 1, interval);
  struct io_uring_sqe *sqe = get_sqe(iou);
  sqe->user_data = id_i;

  rb_hash_aset(spec, SYM_ts, time_spec);
  store_spec(iou, spec, id, SYM_timeout);

  io_uring_prep_timeout(sqe, OpSpecData_ts_ptr(time_spec), 0, flags);
  iou->unsubmitted_sqes++;
  return id;
}

VALUE IOU_prep_write(VALUE self, VALUE spec) {
  IOU_t *iou = get_iou(self);
  unsigned id_i = ++iou->op_counter;
  VALUE id = UINT2NUM(id_i);

  VALUE values[2];
  get_required_kwargs(spec, values, 2, SYM_fd, SYM_buffer);
  VALUE fd = values[0];
  VALUE buffer = values[1];
  VALUE len = rb_hash_aref(spec, SYM_len);
  unsigned nbytes = NIL_P(len) ? RSTRING_LEN(buffer) : NUM2UINT(len);

  struct io_uring_sqe *sqe = get_sqe(iou);
  sqe->user_data = id_i;

  store_spec(iou, spec, id, SYM_write);

  io_uring_prep_write(sqe, NUM2INT(fd), RSTRING_PTR(buffer), nbytes, -1);
  iou->unsubmitted_sqes++;
  return id;
}

VALUE IOU_submit(VALUE self) {
  IOU_t *iou = get_iou(self);
  if (!iou->unsubmitted_sqes) goto done;

  iou->unsubmitted_sqes = 0;
  int ret = io_uring_submit(&iou->ring);
  if (ret < 0)
    rb_syserr_fail(-ret, strerror(-ret));

done:
  return self;
}

inline VALUE make_empty_op_with_result(VALUE id, VALUE result) {
  VALUE hash = rb_hash_new();
  rb_hash_aset(hash, SYM_id, id);
  rb_hash_aset(hash, SYM_result, result);
  RB_GC_GUARD(hash);
  return hash;
}

typedef struct {
  IOU_t *iou;
  struct io_uring_cqe *cqe;
  int ret;
}  wait_for_completion_ctx_t;

void *wait_for_completion_without_gvl(void *ptr) {
  wait_for_completion_ctx_t *ctx = (wait_for_completion_ctx_t *)ptr;
  ctx->ret = io_uring_wait_cqe(&ctx->iou->ring, &ctx->cqe);
  return NULL;
}

static inline VALUE pull_cqe_op_spec(IOU_t *iou, struct io_uring_cqe *cqe) {
  VALUE id = UINT2NUM(cqe->user_data);
  VALUE spec = rb_hash_aref(iou->pending_ops, id);
  VALUE result = INT2NUM(cqe->res);
  if (NIL_P(spec))
    return make_empty_op_with_result(id, result);

  // handle read buffer
  VALUE op = rb_hash_aref(spec, SYM_op);
  if (op == SYM_read) {
    VALUE buffer = rb_hash_aref(spec, SYM_buffer);
    VALUE buffer_offset = rb_hash_aref(spec, SYM_buffer_offset);
    int buffer_offset_i = NIL_P(buffer_offset) ? 0 : NUM2INT(buffer_offset);
    adjust_read_buffer_len(buffer, cqe->res, buffer_offset_i);
  }
  
  if (!(cqe->flags & IORING_CQE_F_MORE))
    rb_hash_delete(iou->pending_ops, id);

  rb_hash_aset(spec, SYM_result, result);
  RB_GC_GUARD(spec);
  return spec;
}

VALUE IOU_wait_for_completion(VALUE self) {
  IOU_t *iou = get_iou(self);

  wait_for_completion_ctx_t ctx = {
    .iou = iou
  };

  rb_thread_call_without_gvl(wait_for_completion_without_gvl, (void *)&ctx, RUBY_UBF_IO, 0);

  if (unlikely(ctx.ret < 0)) {
    rb_syserr_fail(-ctx.ret, strerror(-ctx.ret));
  }
  io_uring_cqe_seen(&iou->ring, ctx.cqe);
  return pull_cqe_op_spec(iou, ctx.cqe);
}

static inline void process_cqe(IOU_t *iou, struct io_uring_cqe *cqe) {
  VALUE spec = pull_cqe_op_spec(iou, cqe);
  VALUE block = rb_hash_aref(spec, SYM_block);

  if (rb_block_given_p())
    rb_yield(spec);
  else if (RTEST(block))
    rb_proc_call_with_block_kw(block, 1, &spec, Qnil, Qnil);
}

// copied from liburing/queue.c
static inline bool cq_ring_needs_flush(struct io_uring *ring) {
  return IO_URING_READ_ONCE(*ring->sq.kflags) & IORING_SQ_CQ_OVERFLOW;
}

// adapted from io_uring_peek_batch_cqe in liburing/queue.c
// this peeks at cqes and handles each available cqe
static inline int process_ready_cqes(IOU_t *iou) {
  unsigned total_count = 0;

iterate:
  bool overflow_checked = false;
  struct io_uring_cqe *cqe;
  unsigned head;
  unsigned count = 0;
  io_uring_for_each_cqe(&iou->ring, head, cqe) {
    ++count;
    process_cqe(iou, cqe);
  }
  io_uring_cq_advance(&iou->ring, count);
  total_count += count;

  if (overflow_checked) goto done;

  if (cq_ring_needs_flush(&iou->ring)) {
    io_uring_enter(iou->ring.ring_fd, 0, 0, IORING_ENTER_GETEVENTS, NULL);
    overflow_checked = true;
    goto iterate;
  }

done:
  return total_count;
}

VALUE IOU_process_completions(int argc, VALUE *argv, VALUE self) {
  IOU_t *iou = get_iou(self);
  VALUE wait;

  rb_scan_args(argc, argv, "01", &wait);
  int wait_i = RTEST(wait);
  unsigned count = 0;

  if (iou->unsubmitted_sqes) {
    io_uring_submit(&iou->ring);
    iou->unsubmitted_sqes = 0;
  }

  if (wait_i) {
    wait_for_completion_ctx_t ctx = { .iou = iou };

    rb_thread_call_without_gvl(wait_for_completion_without_gvl, (void *)&ctx, RUBY_UBF_IO, 0);
    if (unlikely(ctx.ret < 0)) {
      rb_syserr_fail(-ctx.ret, strerror(-ctx.ret));
    }
    ++count;
    io_uring_cqe_seen(&iou->ring, ctx.cqe);
    process_cqe(iou, ctx.cqe);
  }

  count += process_ready_cqes(iou);
  return UINT2NUM(count);
}

#define MAKE_SYM(sym) ID2SYM(rb_intern(sym))

void Init_IOU(void) {
  mIOU = rb_define_module("IOU");
  cRing = rb_define_class_under(mIOU, "Ring", rb_cObject);
  rb_define_alloc_func(cRing, IOU_allocate);

  rb_define_method(cRing, "initialize", IOU_initialize, 0);
  rb_define_method(cRing, "close", IOU_close, 0);
  rb_define_method(cRing, "closed?", IOU_closed_p, 0);
  rb_define_method(cRing, "pending_ops", IOU_pending_ops, 0);
  
  rb_define_method(cRing, "prep_accept", IOU_prep_accept, 1);
  rb_define_method(cRing, "prep_cancel", IOU_prep_cancel, 1);
  rb_define_method(cRing, "prep_close", IOU_prep_close, 1);
  rb_define_method(cRing, "prep_nop", IOU_prep_nop, 0);
  rb_define_method(cRing, "prep_read", IOU_prep_read, 1);
  rb_define_method(cRing, "prep_timeout", IOU_prep_timeout, 1);
  rb_define_method(cRing, "prep_write", IOU_prep_write, 1);

  rb_define_method(cRing, "submit", IOU_submit, 0);
  rb_define_method(cRing, "wait_for_completion", IOU_wait_for_completion, 0);
  rb_define_method(cRing, "process_completions", IOU_process_completions, -1);

  cArgumentError = rb_const_get(rb_cObject, rb_intern("ArgumentError"));

  SYM_accept        = MAKE_SYM("accept");
  SYM_block         = MAKE_SYM("block");
  SYM_buffer        = MAKE_SYM("buffer");
  SYM_buffer_offset = MAKE_SYM("buffer_offset");
  SYM_close         = MAKE_SYM("close");
  SYM_fd            = MAKE_SYM("fd");
  SYM_id            = MAKE_SYM("id");
  SYM_len           = MAKE_SYM("len");
  SYM_multishot     = MAKE_SYM("multishot");
  SYM_op            = MAKE_SYM("op");
  SYM_interval      = MAKE_SYM("interval");
  SYM_read          = MAKE_SYM("read");
  SYM_result        = MAKE_SYM("result");
  SYM_timeout       = MAKE_SYM("timeout");
  SYM_ts            = MAKE_SYM("ts");
  SYM_write         = MAKE_SYM("write");
}
