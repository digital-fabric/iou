#include "iou.h"

VALUE cOpSpecData;

static size_t OpSpecData_size(const void *ptr) {
  return sizeof(OpSpecData_t);
}

static const rb_data_type_t OpSpecData_type = {
    "OpSpecData",
    {0, 0, OpSpecData_size, 0},
    0, 0, RUBY_TYPED_FREE_IMMEDIATELY | RUBY_TYPED_WB_PROTECTED
};

static VALUE OpSpecData_allocate(VALUE klass) {
  OpSpecData_t *tsw = ALLOC(OpSpecData_t);

  return TypedData_Wrap_Struct(klass, &OpSpecData_type, tsw);
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

VALUE OpSpecData_initialize(VALUE self, VALUE value) {
  OpSpecData_t *tsw = RTYPEDDATA_DATA(self);
  tsw->data.ts = value_to_timespec(value);
  return self;
}

struct __kernel_timespec *OpSpecData_ts_ptr(VALUE self) {
  OpSpecData_t *tsw = RTYPEDDATA_DATA(self);
  return &tsw->data.ts;
}

void Init_OpSpecData(void) {
  mIOU = rb_define_module("IOU");
  cOpSpecData = rb_define_class_under(mIOU, "OpSpecData", rb_cObject);
  rb_define_alloc_func(cOpSpecData, OpSpecData_allocate);

  rb_define_method(cOpSpecData, "initialize", OpSpecData_initialize, 1);
}
