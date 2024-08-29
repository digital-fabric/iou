#include "iou.h"

VALUE mIOU;

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

static VALUE IOU_initialize(VALUE self) {
  IOU_t *iou = RTYPEDDATA_DATA(self);

  iou->ring_initialized = 0;
  iou->op_counter = 0;
  iou->unsubmitted_sqes = 0;

  unsigned int prepared_limit = 1024;
  int flags = 0;
  // #ifdef HAVE_IORING_SETUP_SUBMIT_ALL
  // flags |= IORING_SETUP_SUBMIT_ALL;
  // #endif
  // #ifdef HAVE_IORING_SETUP_COOP_TASKRUN
  // flags |= IORING_SETUP_COOP_TASKRUN;
  // #endif
  // #ifdef HAVE_IORING_SETUP_SINGLE_ISSUER
  // flags |= IORING_SETUP_SINGLE_ISSUER;
  // #endif

  printf("0\n");
  while (1) {
    printf("limit: %d\n", prepared_limit);
    int ret = io_uring_queue_init(prepared_limit, &iou->ring, flags);
    printf("ret: %d %s\n", ret, strerror(-ret));

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

void Init_IOU(void) {
  mIOU = rb_define_module("IOU");
  VALUE cRing = rb_define_class_under(mIOU, "Ring", rb_cObject);
  rb_define_alloc_func(cRing, IOU_allocate);

  rb_define_method(cRing, "initialize", IOU_initialize, 0);
}
