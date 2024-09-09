#include "iou.h"

VALUE cOpCtx;

static size_t OpCtx_size(const void *ptr) {
  return sizeof(OpCtx_t);
}

static const rb_data_type_t OpCtx_type = {
    "OpCtx",
    {0, 0, OpCtx_size, 0},
    0, 0, RUBY_TYPED_FREE_IMMEDIATELY | RUBY_TYPED_WB_PROTECTED
};

static VALUE OpCtx_allocate(VALUE klass) {
  OpCtx_t *osd = ALLOC(OpCtx_t);

  return TypedData_Wrap_Struct(klass, &OpCtx_type, osd);
}

VALUE OpCtx_initialize(VALUE self) {
  OpCtx_t *osd = RTYPEDDATA_DATA(self);
  memset(&osd->data, 0, sizeof(osd->data));
  return self;
}

struct __kernel_timespec *OpCtx_ts_get(VALUE self) {
  OpCtx_t *osd = RTYPEDDATA_DATA(self);
  return &osd->data.ts;
}

inline struct __kernel_timespec double_to_timespec(double value) {
  double integral;
  double fraction = modf(value, &integral);
  struct __kernel_timespec ts;
  ts.tv_sec = integral;
  ts.tv_nsec = floor(fraction * 1000000000);
  return ts;
}

inline struct __kernel_timespec value_to_timespec(VALUE value) {
  return double_to_timespec(NUM2DBL(value));
}

void OpCtx_ts_set(VALUE self, VALUE value) {
  OpCtx_t *osd = RTYPEDDATA_DATA(self);
  osd->data.ts = value_to_timespec(value);
}

struct sa_data *OpCtx_sa_get(VALUE self) {
  OpCtx_t *osd = RTYPEDDATA_DATA(self);
  return &osd->data.sa;
}

void Init_OpCtx(void) {
  mIOU = rb_define_module("IOU");
  cOpCtx = rb_define_class_under(mIOU, "OpCtx", rb_cObject);
  rb_define_alloc_func(cOpCtx, OpCtx_allocate);

  rb_define_method(cOpCtx, "initialize", OpCtx_initialize, 0);
}
