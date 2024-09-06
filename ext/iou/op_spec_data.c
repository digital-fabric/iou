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
  OpSpecData_t *osd = ALLOC(OpSpecData_t);

  return TypedData_Wrap_Struct(klass, &OpSpecData_type, osd);
}

VALUE OpSpecData_initialize(VALUE self) {
  OpSpecData_t *osd = RTYPEDDATA_DATA(self);
  memset(&osd->data, 0, sizeof(osd->data));
  return self;
}

struct __kernel_timespec *OpSpecData_ts_get(VALUE self) {
  OpSpecData_t *osd = RTYPEDDATA_DATA(self);
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

void OpSpecData_ts_set(VALUE self, VALUE value) {
  OpSpecData_t *osd = RTYPEDDATA_DATA(self);
  osd->data.ts = value_to_timespec(value);
}

struct sa_data *OpSpecData_sa_get(VALUE self) {
  OpSpecData_t *osd = RTYPEDDATA_DATA(self);
  return &osd->data.sa;
}

void Init_OpSpecData(void) {
  mIOU = rb_define_module("IOU");
  cOpSpecData = rb_define_class_under(mIOU, "OpSpecData", rb_cObject);
  rb_define_alloc_func(cOpSpecData, OpSpecData_allocate);

  rb_define_method(cOpSpecData, "initialize", OpSpecData_initialize, 0);
}
