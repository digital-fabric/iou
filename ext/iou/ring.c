#include "iou.h"
#include "ruby/thread.h"
#include <sys/mman.h>

VALUE mIOU;
VALUE cRing;
VALUE cArgumentError;

VALUE SYM_accept;
VALUE SYM_block;
VALUE SYM_buffer;
VALUE SYM_buffer_group;
VALUE SYM_buffer_offset;
VALUE SYM_close;
VALUE SYM_count;
VALUE SYM_emit;
VALUE SYM_fd;
VALUE SYM_id;
VALUE SYM_interval;
VALUE SYM_len;
VALUE SYM_multishot;
VALUE SYM_op;
VALUE SYM_read;
VALUE SYM_result;
VALUE SYM_signal;
VALUE SYM_size;
VALUE SYM_spec_data;
VALUE SYM_stop;
VALUE SYM_timeout;
VALUE SYM_utf8;
VALUE SYM_write;

static void IOURing_mark(void *ptr) {
  IOURing_t *iour = ptr;
  rb_gc_mark_movable(iour->pending_ops);
}

static void IOURing_compact(void *ptr) {
  IOURing_t *iour = ptr;
  iour->pending_ops = rb_gc_location(iour->pending_ops);
}

void cleanup_iour(IOURing_t *iour) {
  if (!iour->ring_initialized) return;

  for (unsigned i = 0; i < iour->br_counter; i++) {
    struct buf_ring_descriptor *desc = iour->brs + i;
    io_uring_free_buf_ring(&iour->ring, desc->br, desc->buf_count, i);
    free(desc->buf_base);
  }
  iour->br_counter = 0;
  io_uring_queue_exit(&iour->ring);
  iour->ring_initialized = 0;
}

static void IOU_free(void *ptr) {
  cleanup_iour((IOURing_t *)ptr);
}

static size_t IOURing_size(const void *ptr) {
  return sizeof(IOURing_t);
}

static const rb_data_type_t IOURing_type = {
    "IOURing",
    {IOURing_mark, IOU_free, IOURing_size, IOURing_compact},
    0, 0, RUBY_TYPED_FREE_IMMEDIATELY | RUBY_TYPED_WB_PROTECTED
};

static VALUE IOURing_allocate(VALUE klass) {
  IOURing_t *iour = ALLOC(IOURing_t);

  return TypedData_Wrap_Struct(klass, &IOURing_type, iour);
}

VALUE IOURing_initialize(VALUE self) {
  IOURing_t *iour = RTYPEDDATA_DATA(self);

  iour->ring_initialized = 0;
  iour->op_counter = 0;
  iour->unsubmitted_sqes = 0;
  iour->br_counter = 0;

  iour->pending_ops = rb_hash_new();

  unsigned prepared_limit = 1024;
  int flags = 0;
  #ifdef HAVE_IORING_SETUP_SUBMIT_ALL
  flags |= IORING_SETUP_SUBMIT_ALL;
  #endif
  #ifdef HAVE_IORING_SETUP_COOP_TASKRUN
  flags |= IORING_SETUP_COOP_TASKRUN;
  #endif

  while (1) {
    int ret = io_uring_queue_init(prepared_limit, &iour->ring, flags);
    if (likely(!ret)) break;

    // if ENOMEM is returned, try with half as much entries
    if (unlikely(ret == -ENOMEM && prepared_limit > 64))
      prepared_limit = prepared_limit / 2;
    else
      rb_syserr_fail(-ret, strerror(-ret));
  }
  iour->ring_initialized = 1;

  return self;
}

VALUE IOURing_close(VALUE self) {
  IOURing_t *iour = RTYPEDDATA_DATA(self);
  cleanup_iour(iour);
  return self;
}

VALUE IOURing_closed_p(VALUE self) {
  IOURing_t *iour = RTYPEDDATA_DATA(self);
  return iour->ring_initialized ? Qfalse : Qtrue;
}

VALUE IOURing_pending_ops(VALUE self) {
  IOURing_t *iour = RTYPEDDATA_DATA(self);
  return iour->pending_ops;
}

inline IOURing_t *get_iou(VALUE self) {
  IOURing_t *iour = RTYPEDDATA_DATA(self);
  if (!iour->ring_initialized)
    rb_raise(rb_eRuntimeError, "IOU ring was not initialized");
  return iour;
}

static inline struct io_uring_sqe *get_sqe(IOURing_t *iour) {
  struct io_uring_sqe *sqe;
  sqe = io_uring_get_sqe(&iour->ring);
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

VALUE IOURing_setup_buffer_ring(VALUE self, VALUE opts) {
  IOURing_t *iour = get_iou(self);

  if (iour->br_counter == BUFFER_RING_MAX_COUNT)
    rb_raise(rb_eRuntimeError, "Cannot setup more than BUFFER_RING_MAX_COUNT buffer rings");

  VALUE values[2];
  get_required_kwargs(opts, values, 2, SYM_count, SYM_size);
  VALUE count = values[0];
  VALUE size = values[1];

  struct buf_ring_descriptor *desc = iour->brs + iour->br_counter;
  desc->buf_count = NUM2UINT(count);
  desc->buf_size = NUM2UINT(size);

  desc->br_size = sizeof(struct io_uring_buf) * desc->buf_count;
	void *mapped = mmap(
    NULL, desc->br_size, PROT_READ | PROT_WRITE,
		MAP_ANONYMOUS | MAP_PRIVATE, 0, 0
  );
  if (mapped == MAP_FAILED)
    rb_raise(rb_eRuntimeError, "Failed to allocate buffer ring");

  desc->br = (struct io_uring_buf_ring *)mapped;
  io_uring_buf_ring_init(desc->br);

  unsigned bg_id = iour->br_counter;
  struct io_uring_buf_reg reg = {
    .ring_addr = (unsigned long)desc->br,
		.ring_entries = desc->buf_count,
		.bgid = bg_id
  };
	int ret = io_uring_register_buf_ring(&iour->ring, &reg, 0);
	if (ret) {
    munmap(desc->br, desc->br_size);
    rb_syserr_fail(-ret, strerror(-ret));
	}

  desc->buf_base = malloc(desc->buf_count * desc->buf_size);
  if (!desc->buf_base) {
    io_uring_free_buf_ring(&iour->ring, desc->br, desc->buf_count, bg_id);
    rb_raise(rb_eRuntimeError, "Failed to allocate buffers");
  }

  int mask = io_uring_buf_ring_mask(desc->buf_count);
	for (unsigned i = 0; i < desc->buf_count; i++) {
		io_uring_buf_ring_add(
      desc->br, desc->buf_base + i * desc->buf_size, desc->buf_size,
      i, mask, i);
	}
	io_uring_buf_ring_advance(desc->br, desc->buf_count);
  iour->br_counter++;
  return UINT2NUM(bg_id);
}

static inline VALUE setup_op_ctx(IOURing_t *iour, enum op_type type, VALUE op, VALUE id, VALUE spec) {
  rb_hash_aset(spec, SYM_id, id);
  rb_hash_aset(spec, SYM_op, op);
  VALUE block_proc = rb_block_given_p() ? rb_block_proc() : Qnil;
  if (block_proc != Qnil)
    rb_hash_aset(spec, SYM_block, block_proc);
  VALUE ctx = rb_funcall(cOpCtx, rb_intern("new"), 2, spec, block_proc);
  OpCtx_type_set(ctx, type);
  rb_hash_aset(iour->pending_ops, id, ctx);
  return ctx;
}

VALUE IOURing_emit(VALUE self, VALUE spec) {
  IOURing_t *iour = get_iou(self);
  unsigned id_i = ++iour->op_counter;
  VALUE id = UINT2NUM(id_i);

  struct io_uring_sqe *sqe = get_sqe(iour);
  sqe->user_data = id_i;
  VALUE ctx = setup_op_ctx(iour, OP_emit, SYM_emit, id, spec);
  if (rb_hash_aref(spec, SYM_signal) == SYM_stop)
    OpCtx_stop_signal_set(ctx);

  io_uring_prep_nop(sqe);

  // immediately submit
  io_uring_submit(&iour->ring);
  iour->unsubmitted_sqes = 0;

  return id;
}

VALUE IOURing_prep_accept(VALUE self, VALUE spec) {
  IOURing_t *iour = get_iou(self);
  unsigned id_i = ++iour->op_counter;
  VALUE id = UINT2NUM(id_i);

  VALUE values[1];
  get_required_kwargs(spec, values, 1, SYM_fd);
  VALUE fd = values[0];
  VALUE multishot = rb_hash_aref(spec, SYM_multishot);

  struct io_uring_sqe *sqe = get_sqe(iour);
  sqe->user_data = id_i;

  VALUE ctx = setup_op_ctx(iour, OP_accept, SYM_accept, id, spec);
  struct sa_data *sa = OpCtx_sa_get(ctx);
  if (RTEST(multishot))
    io_uring_prep_multishot_accept(sqe, NUM2INT(fd), &sa->addr, &sa->len, 0);
  else
    io_uring_prep_accept(sqe, NUM2INT(fd), &sa->addr, &sa->len, 0);
  iour->unsubmitted_sqes++;
  return id;
}

VALUE prep_cancel_id(IOURing_t *iour, unsigned op_id_i) {
  unsigned id_i = ++iour->op_counter;
  VALUE id = UINT2NUM(id_i);

  struct io_uring_sqe *sqe = get_sqe(iour);
  io_uring_prep_cancel64(sqe, op_id_i, 0);
  sqe->user_data = id_i;
  iour->unsubmitted_sqes++;

  return id;
}

VALUE IOURing_prep_cancel(VALUE self, VALUE spec) {
  IOURing_t *iour = get_iou(self);

  if (TYPE(spec) == T_FIXNUM)
    return prep_cancel_id(iour, NUM2UINT(spec));
  
  if (TYPE(spec) != T_HASH)
    rb_raise(cArgumentError, "Expected operation id or keyword arguments");

  VALUE id = rb_hash_aref(spec, SYM_id);
  if (!NIL_P(id))
    return prep_cancel_id(iour, NUM2UINT(id));

  rb_raise(cArgumentError, "Missing operation id");
}

VALUE IOURing_prep_close(VALUE self, VALUE spec) {
  IOURing_t *iour = get_iou(self);
  unsigned id_i = ++iour->op_counter;
  VALUE id = UINT2NUM(id_i);

  VALUE values[1];
  get_required_kwargs(spec, values, 1, SYM_fd);
  VALUE fd = values[0];

  struct io_uring_sqe *sqe = get_sqe(iour);
  sqe->user_data = id_i;

  setup_op_ctx(iour, OP_close, SYM_close, id, spec);

  io_uring_prep_close(sqe, NUM2INT(fd));
  iour->unsubmitted_sqes++;
  return id;
}

VALUE IOURing_prep_nop(VALUE self) {
  IOURing_t *iour = get_iou(self);
  unsigned id_i = ++iour->op_counter;
  VALUE id = UINT2NUM(id_i);

  struct io_uring_sqe *sqe = get_sqe(iour);
  io_uring_prep_nop(sqe);
  sqe->user_data = id_i;
  iour->unsubmitted_sqes++;

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

VALUE prep_read_multishot(IOURing_t *iour, VALUE spec) {
  unsigned id_i = ++iour->op_counter;
  VALUE id = UINT2NUM(id_i);

  VALUE values[2];
  get_required_kwargs(spec, values, 2, SYM_fd, SYM_buffer_group);
  int fd = NUM2INT(values[0]);
  unsigned bg_id = NUM2UINT(values[1]);
  int utf8 = RTEST(rb_hash_aref(spec, SYM_utf8));

  struct io_uring_sqe *sqe = get_sqe(iour);
  sqe->user_data = id_i;

  VALUE ctx = setup_op_ctx(iour, OP_read, SYM_read, id, spec);
  OpCtx_rd_set(ctx, Qnil, 0, bg_id, utf8);

  io_uring_prep_read_multishot(sqe, fd, 0, -1, bg_id);
  iour->unsubmitted_sqes++;
  return id;
}

VALUE IOURing_prep_read(VALUE self, VALUE spec) {
  IOURing_t *iour = get_iou(self);

  if (RTEST(rb_hash_aref(spec, SYM_multishot)))
    return prep_read_multishot(iour, spec);

  unsigned id_i = ++iour->op_counter;
  VALUE id = UINT2NUM(id_i);

  VALUE values[3];
  get_required_kwargs(spec, values, 3, SYM_fd, SYM_buffer, SYM_len);

  VALUE fd = values[0];
  VALUE buffer = values[1];
  VALUE len = values[2];
  unsigned len_i = NUM2UINT(len);

  VALUE buffer_offset = rb_hash_aref(spec, SYM_buffer_offset);
  int buffer_offset_i = NIL_P(buffer_offset) ? 0 : NUM2INT(buffer_offset);
  int utf8 = RTEST(rb_hash_aref(spec, SYM_utf8));

  struct io_uring_sqe *sqe = get_sqe(iour);
  sqe->user_data = id_i;

  VALUE ctx = setup_op_ctx(iour, OP_read, SYM_read, id, spec);
  OpCtx_rd_set(ctx, buffer, buffer_offset_i, 0, utf8);

  void *ptr = prepare_read_buffer(buffer, len_i, buffer_offset_i);
  io_uring_prep_read(sqe, NUM2INT(fd), ptr, len_i, -1);
  iour->unsubmitted_sqes++;
  return id;
}

VALUE IOURing_prep_timeout(VALUE self, VALUE spec) {
  IOURing_t *iour = get_iou(self);
  unsigned id_i = ++iour->op_counter;
  VALUE id = UINT2NUM(id_i);

  VALUE values[1];
  get_required_kwargs(spec, values, 1, SYM_interval);
  VALUE interval = values[0];
  VALUE multishot = rb_hash_aref(spec, SYM_multishot);
  unsigned flags = RTEST(multishot) ? IORING_TIMEOUT_MULTISHOT : 0;

  struct io_uring_sqe *sqe = get_sqe(iour);
  sqe->user_data = id_i;

  VALUE ctx = setup_op_ctx(iour, OP_timeout, SYM_timeout, id, spec);
  OpCtx_ts_set(ctx, interval);

  io_uring_prep_timeout(sqe, OpCtx_ts_get(ctx), 0, flags);
  iour->unsubmitted_sqes++;
  return id;
}

VALUE IOURing_prep_write(VALUE self, VALUE spec) {
  IOURing_t *iour = get_iou(self);
  unsigned id_i = ++iour->op_counter;
  VALUE id = UINT2NUM(id_i);

  VALUE values[2];
  get_required_kwargs(spec, values, 2, SYM_fd, SYM_buffer);
  VALUE fd = values[0];
  VALUE buffer = values[1];
  VALUE len = rb_hash_aref(spec, SYM_len);
  unsigned nbytes = NIL_P(len) ? RSTRING_LEN(buffer) : NUM2UINT(len);

  struct io_uring_sqe *sqe = get_sqe(iour);
  sqe->user_data = id_i;

  setup_op_ctx(iour, OP_write, SYM_write, id, spec);

  io_uring_prep_write(sqe, NUM2INT(fd), RSTRING_PTR(buffer), nbytes, -1);
  iour->unsubmitted_sqes++;
  return id;
}

VALUE IOURing_submit(VALUE self) {
  IOURing_t *iour = get_iou(self);
  if (!iour->unsubmitted_sqes) goto done;

  iour->unsubmitted_sqes = 0;
  int ret = io_uring_submit(&iour->ring);
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
  IOURing_t *iour;
  struct io_uring_cqe *cqe;
  int ret;
}  wait_for_completion_ctx_t;

void *wait_for_completion_without_gvl(void *ptr) {
  wait_for_completion_ctx_t *ctx = (wait_for_completion_ctx_t *)ptr;
  ctx->ret = io_uring_wait_cqe(&ctx->iour->ring, &ctx->cqe);
  return NULL;
}

static inline void update_read_buffer_from_buffer_ring(IOURing_t *iour, VALUE ctx, struct io_uring_cqe *cqe) {
  VALUE buf = Qnil;
  if (cqe->res == 0) {
    buf = rb_str_new_literal("");
    goto done;
  }
  
  struct read_data *rd = OpCtx_rd_get(ctx);
  unsigned buf_idx = cqe->flags >> IORING_CQE_BUFFER_SHIFT;

  struct buf_ring_descriptor *desc = iour->brs + rd->bg_id;
  char *src = desc->buf_base + desc->buf_size * buf_idx;
  buf = rd->utf8_encoding ? rb_utf8_str_new(src, cqe->res) : rb_str_new(src, cqe->res);
  
  // add buffer back to buffer ring
  io_uring_buf_ring_add(
    desc->br, src, desc->buf_size, buf_idx,
		io_uring_buf_ring_mask(desc->buf_count), 0
  );
  io_uring_buf_ring_advance(desc->br, 1);
done:
  rb_hash_aset(OpCtx_spec_get(ctx), SYM_buffer, buf);
  RB_GC_GUARD(buf);
  return;
}

static inline void update_read_buffer(IOURing_t *iour, VALUE ctx, struct io_uring_cqe *cqe) {
  if (cqe->res < 0) return;

  if (cqe->flags & IORING_CQE_F_BUFFER) {
    update_read_buffer_from_buffer_ring(iour, ctx, cqe);
    return;
  }

  if (cqe->res == 0) return;

  struct read_data *rd = OpCtx_rd_get(ctx);
  adjust_read_buffer_len(rd->buffer, cqe->res, rd->buffer_offset);
}

static inline VALUE get_cqe_ctx(IOURing_t *iour, struct io_uring_cqe *cqe, int *stop_flag, VALUE *spec) {
  VALUE id = UINT2NUM(cqe->user_data);
  VALUE ctx = rb_hash_aref(iour->pending_ops, id);
  VALUE result = INT2NUM(cqe->res);
  if (NIL_P(ctx)) {
    *spec = make_empty_op_with_result(id, result);
    return Qnil;
  }

  // post completion work
  switch (OpCtx_type_get(ctx)) {
    case OP_read:
      update_read_buffer(iour, ctx, cqe);
      break;
    case OP_emit:
      if (stop_flag && OpCtx_stop_signal_p(ctx))
        *stop_flag = 1;
      break;
    default:
  }
  
  // for multishot ops, the IORING_CQE_F_MORE flag indicates more completions
  // will be coming, so we need to keep the spec. Otherwise, we remove it.
  if (!(cqe->flags & IORING_CQE_F_MORE))
    rb_hash_delete(iour->pending_ops, id);

  *spec = OpCtx_spec_get(ctx);
  rb_hash_aset(*spec, SYM_result, result);
  RB_GC_GUARD(ctx);
  return ctx;
}

VALUE IOURing_wait_for_completion(VALUE self) {
  IOURing_t *iour = get_iou(self);

  wait_for_completion_ctx_t cqe_ctx = {
    .iour = iour
  };

  rb_thread_call_without_gvl(wait_for_completion_without_gvl, (void *)&cqe_ctx, RUBY_UBF_IO, 0);

  if (unlikely(cqe_ctx.ret < 0)) {
    rb_syserr_fail(-cqe_ctx.ret, strerror(-cqe_ctx.ret));
  }
  io_uring_cqe_seen(&iour->ring, cqe_ctx.cqe);

  VALUE spec = Qnil;
  get_cqe_ctx(iour, cqe_ctx.cqe, 0, &spec);
  return spec;
}

static inline void process_cqe(IOURing_t *iour, struct io_uring_cqe *cqe, int block_given, int *stop_flag) {
  if (stop_flag) *stop_flag = 0;
  VALUE spec;
  VALUE ctx = get_cqe_ctx(iour, cqe, stop_flag, &spec);
  if (stop_flag && *stop_flag) return;

  if (block_given)
    rb_yield(spec);
  else if (ctx != Qnil) {
    VALUE proc = OpCtx_proc_get(ctx);
    if (RTEST(proc))
      rb_proc_call_with_block_kw(proc, 1, &spec, Qnil, Qnil);
  }

  RB_GC_GUARD(ctx);
}

// copied from liburing/queue.c
static inline bool cq_ring_needs_flush(struct io_uring *ring) {
  return IO_URING_READ_ONCE(*ring->sq.kflags) & IORING_SQ_CQ_OVERFLOW;
}

// adapted from io_uring_peek_batch_cqe in liburing/queue.c
// this peeks at cqes and handles each available cqe
static inline int process_ready_cqes(IOURing_t *iour, int block_given, int *stop_flag) {
  unsigned total_count = 0;

iterate:
  bool overflow_checked = false;
  struct io_uring_cqe *cqe;
  unsigned head;
  unsigned count = 0;
  io_uring_for_each_cqe(&iour->ring, head, cqe) {
    ++count;
    if (stop_flag) *stop_flag = 0;
    process_cqe(iour, cqe, block_given, stop_flag);
    if (stop_flag && *stop_flag)
      break;
  }
  io_uring_cq_advance(&iour->ring, count);
  total_count += count;

  if (overflow_checked) goto done;
  if (stop_flag && *stop_flag) goto done;

  if (cq_ring_needs_flush(&iour->ring)) {
    io_uring_enter(iour->ring.ring_fd, 0, 0, IORING_ENTER_GETEVENTS, NULL);
    overflow_checked = true;
    goto iterate;
  }

done:
  return total_count;
}

VALUE IOURing_process_completions(int argc, VALUE *argv, VALUE self) {
  IOURing_t *iour = get_iou(self);
  int block_given = rb_block_given_p();
  VALUE wait;

  rb_scan_args(argc, argv, "01", &wait);
  int wait_i = RTEST(wait);
  unsigned count = 0;

  // automatically submit any unsubmitted SQEs
  if (iour->unsubmitted_sqes) {
    io_uring_submit(&iour->ring);
    iour->unsubmitted_sqes = 0;
  }

  if (wait_i) {
    wait_for_completion_ctx_t ctx = { .iour = iour };

    rb_thread_call_without_gvl(wait_for_completion_without_gvl, (void *)&ctx, RUBY_UBF_IO, 0);
    if (unlikely(ctx.ret < 0)) {
      rb_syserr_fail(-ctx.ret, strerror(-ctx.ret));
    }
    ++count;
    io_uring_cqe_seen(&iour->ring, ctx.cqe);
    process_cqe(iour, ctx.cqe, block_given, 0);
  }

  count += process_ready_cqes(iour, block_given, 0);
  return UINT2NUM(count);
}

VALUE IOURing_process_completions_loop(VALUE self) {
  IOURing_t *iour = get_iou(self);
  int block_given = rb_block_given_p();
  int stop_flag = 0;
  wait_for_completion_ctx_t ctx = { .iour = iour };

  while (1) {
    // automatically submit any unsubmitted SQEs
    if (iour->unsubmitted_sqes) {
      io_uring_submit(&iour->ring);
      iour->unsubmitted_sqes = 0;
    }

    rb_thread_call_without_gvl(wait_for_completion_without_gvl, (void *)&ctx, RUBY_UBF_IO, 0);
    if (unlikely(ctx.ret < 0)) {
      rb_syserr_fail(-ctx.ret, strerror(-ctx.ret));
    }
    io_uring_cqe_seen(&iour->ring, ctx.cqe);
    process_cqe(iour, ctx.cqe, block_given, &stop_flag);
    if (stop_flag) goto done;

    process_ready_cqes(iour, block_given, &stop_flag);
    if (stop_flag) goto done;
  }
done:
  return self;
}

#define MAKE_SYM(sym) ID2SYM(rb_intern(sym))

void Init_IOURing(void) {
  mIOU = rb_define_module("IOU");
  cRing = rb_define_class_under(mIOU, "Ring", rb_cObject);
  rb_define_alloc_func(cRing, IOURing_allocate);

  rb_define_method(cRing, "initialize", IOURing_initialize, 0);
  rb_define_method(cRing, "close", IOURing_close, 0);
  rb_define_method(cRing, "closed?", IOURing_closed_p, 0);
  rb_define_method(cRing, "pending_ops", IOURing_pending_ops, 0);
  rb_define_method(cRing, "setup_buffer_ring", IOURing_setup_buffer_ring, 1);

  rb_define_method(cRing, "emit", IOURing_emit, 1);

  rb_define_method(cRing, "prep_accept", IOURing_prep_accept, 1);
  rb_define_method(cRing, "prep_cancel", IOURing_prep_cancel, 1);
  rb_define_method(cRing, "prep_close", IOURing_prep_close, 1);
  rb_define_method(cRing, "prep_nop", IOURing_prep_nop, 0);
  rb_define_method(cRing, "prep_read", IOURing_prep_read, 1);
  rb_define_method(cRing, "prep_timeout", IOURing_prep_timeout, 1);
  rb_define_method(cRing, "prep_write", IOURing_prep_write, 1);

  rb_define_method(cRing, "submit", IOURing_submit, 0);
  rb_define_method(cRing, "wait_for_completion", IOURing_wait_for_completion, 0);
  rb_define_method(cRing, "process_completions", IOURing_process_completions, -1);
  rb_define_method(cRing, "process_completions_loop", IOURing_process_completions_loop, 0);

  cArgumentError = rb_const_get(rb_cObject, rb_intern("ArgumentError"));

  SYM_accept        = MAKE_SYM("accept");
  SYM_block         = MAKE_SYM("block");
  SYM_buffer        = MAKE_SYM("buffer");
  SYM_buffer_group  = MAKE_SYM("buffer_group");
  SYM_buffer_offset = MAKE_SYM("buffer_offset");
  SYM_close         = MAKE_SYM("close");
  SYM_count         = MAKE_SYM("count");
  SYM_emit          = MAKE_SYM("emit");
  SYM_fd            = MAKE_SYM("fd");
  SYM_id            = MAKE_SYM("id");
  SYM_interval      = MAKE_SYM("interval");
  SYM_len           = MAKE_SYM("len");
  SYM_multishot     = MAKE_SYM("multishot");
  SYM_op            = MAKE_SYM("op");
  SYM_read          = MAKE_SYM("read");
  SYM_result        = MAKE_SYM("result");
  SYM_signal        = MAKE_SYM("signal");
  SYM_size          = MAKE_SYM("size");
  SYM_spec_data     = MAKE_SYM("spec_data");
  SYM_stop          = MAKE_SYM("stop");
  SYM_timeout       = MAKE_SYM("timeout");
  SYM_utf8          = MAKE_SYM("utf8");
  SYM_write         = MAKE_SYM("write");
}
