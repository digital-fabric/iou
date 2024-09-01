#include "iou.h"

VALUE mIOU;
VALUE cRing;

VALUE SYM_id;
VALUE SYM_op;
VALUE SYM_period;
VALUE SYM_result;
VALUE SYM_timeout;
VALUE SYM_ts;

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

  unsigned int prepared_limit = 1024;
  int flags = 0;
  #ifdef HAVE_IORING_SETUP_SUBMIT_ALL
  flags |= IORING_SETUP_SUBMIT_ALL;
  #endif
  #ifdef HAVE_IORING_SETUP_COOP_TASKRUN
  flags |= IORING_SETUP_COOP_TASKRUN;
  #endif
  #ifdef HAVE_IORING_SETUP_SINGLE_ISSUER
  flags |= IORING_SETUP_SINGLE_ISSUER;
  #endif

  while (1) {
    int ret = io_uring_queue_init(prepared_limit, &iou->ring, flags);

    if (likely(!ret)) break;

    // if ENOMEM is returned, use a smaller limit
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

VALUE IOU_prep_cancel(VALUE self, VALUE op_id) {
  IOU_t *iou = get_iou(self);
  unsigned int op_id_i = NUM2UINT(op_id);
  
  unsigned int id_i = ++iou->op_counter;
  VALUE id = UINT2NUM(id_i);

  struct io_uring_sqe *sqe = get_sqe(iou);
  io_uring_prep_cancel64(sqe, op_id_i, 0);
  sqe->user_data = id_i;
  iou->unsubmitted_sqes++;

  return id;
}

VALUE IOU_prep_timeout(VALUE self, VALUE op) {
  IOU_t *iou = get_iou(self);
  unsigned int id_i = ++iou->op_counter;
  VALUE id = UINT2NUM(id_i);
  iou->unsubmitted_sqes++;

  VALUE period = rb_hash_aref(op, SYM_period);
  if (NIL_P(period))
    rb_raise(rb_eRuntimeError, "Missing period value");
  VALUE time_spec = rb_funcall(cTimeSpec, rb_intern("new"), 1, period);

  struct io_uring_sqe *sqe = get_sqe(iou);
  sqe->user_data = id_i;

  // annotate op
  rb_hash_aset(op, SYM_id, id);
  rb_hash_aset(op, SYM_op, SYM_timeout);
  rb_hash_aset(op, SYM_ts, time_spec);

  // add to pending ops hash
  rb_hash_aset(iou->pending_ops, id, op);

  io_uring_prep_timeout(sqe, TimeSpec_ts_ptr(time_spec), 0, 0);
  return id;
}

VALUE IOU_submit(VALUE self) {
  IOU_t *iou = get_iou(self);
  iou->unsubmitted_sqes = 0;

  io_uring_submit(&iou->ring);
  return self;
}

inline VALUE make_empty_op_with_result(VALUE id, VALUE result) {
  VALUE hash = rb_hash_new();
  rb_hash_aset(hash, SYM_id, id);
  rb_hash_aset(hash, SYM_result, result);
  RB_GC_GUARD(hash);
  return hash;
}

VALUE IOU_wait_for_completion(VALUE self) {
  IOU_t *iou = get_iou(self);
  struct io_uring_cqe *cqe;
  int ret;

  ret = io_uring_wait_cqe(&iou->ring, &cqe);
  if (unlikely(ret < 0)) {
    rb_syserr_fail(-ret, strerror(-ret));
  }
  io_uring_cqe_seen(&iou->ring, cqe);

  VALUE id = UINT2NUM(cqe->user_data);
  VALUE op = rb_hash_aref(iou->pending_ops, id);
  VALUE result = INT2NUM(cqe->res);
  if (NIL_P(op))
    return make_empty_op_with_result(id, result);

  rb_hash_aset(op, SYM_result, result);
  RB_GC_GUARD(op);
  return op;
}

#define MAKE_SYM(sym) ID2SYM(rb_intern(sym))

void Init_IOU(void) {
  mIOU = rb_define_module("IOU");
  cRing = rb_define_class_under(mIOU, "Ring", rb_cObject);
  rb_define_alloc_func(cRing, IOU_allocate);

  rb_define_method(cRing, "initialize", IOU_initialize, 0);
  rb_define_method(cRing, "close", IOU_close, 0);
  rb_define_method(cRing, "closed?", IOU_closed_p, 0);
  
  rb_define_method(cRing, "prep_cancel", IOU_prep_cancel, 1);
  rb_define_method(cRing, "prep_timeout", IOU_prep_timeout, 1);

  rb_define_method(cRing, "submit", IOU_submit, 0);
  rb_define_method(cRing, "wait_for_completion", IOU_wait_for_completion, 0);

  SYM_id      = MAKE_SYM("id");
  SYM_op      = MAKE_SYM("op");
  SYM_period  = MAKE_SYM("period");
  SYM_result  = MAKE_SYM("result");
  SYM_timeout = MAKE_SYM("timeout");
  SYM_ts      = MAKE_SYM("ts");
}
