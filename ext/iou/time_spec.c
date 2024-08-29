#include "iou.h"

VALUE cTimeSpec;

static size_t TimeSpec_size(const void *ptr) {
  return sizeof(TimeSpec_t);
}

static const rb_data_type_t TimeSpec_type = {
    "TSWrapper",
    {0, 0, TimeSpec_size, 0},
    0, 0, RUBY_TYPED_FREE_IMMEDIATELY | RUBY_TYPED_WB_PROTECTED
};

static VALUE TimeSpec_allocate(VALUE klass) {
  TimeSpec_t *tsw = ALLOC(TimeSpec_t);

  return TypedData_Wrap_Struct(klass, &TimeSpec_type, tsw);
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

VALUE TimeSpec_initialize(VALUE self, VALUE value) {
  TimeSpec_t *tsw = RTYPEDDATA_DATA(self);
  tsw->ts = value_to_timespec(value);
  return self;
}

struct __kernel_timespec *TimeSpec_ts_ptr(VALUE self) {
  TimeSpec_t *tsw = RTYPEDDATA_DATA(self);
  return &tsw->ts;
}

void Init_TimeSpec(void) {
  mIOU = rb_define_module("IOU");
  cTimeSpec = rb_define_class_under(mIOU, "TS", rb_cObject);
  rb_define_alloc_func(cTimeSpec, TimeSpec_allocate);

  rb_define_method(cTimeSpec, "initialize", TimeSpec_initialize, 1);
}
