#include "iou.h"

VALUE cOpCtx;

static void OpCtx_mark(void *ptr) {
  OpCtx_t *ctx = ptr;
  rb_gc_mark_movable(ctx->spec);
  rb_gc_mark_movable(ctx->proc);
}

static void OpCtx_compact(void *ptr) {
  OpCtx_t *ctx = ptr;
  ctx->spec = rb_gc_location(ctx->spec);
  ctx->proc = rb_gc_location(ctx->proc);
}

static size_t OpCtx_size(const void *ptr) {
  return sizeof(OpCtx_t);
}

static const rb_data_type_t OpCtx_type = {
    "OpCtx",
    {OpCtx_mark, 0, OpCtx_size, OpCtx_compact},
    0, 0, RUBY_TYPED_FREE_IMMEDIATELY | RUBY_TYPED_WB_PROTECTED
};

static VALUE OpCtx_allocate(VALUE klass) {
  OpCtx_t *osd = ALLOC(OpCtx_t);

  return TypedData_Wrap_Struct(klass, &OpCtx_type, osd);
}

VALUE OpCtx_initialize(VALUE self, VALUE spec, VALUE proc) {
  OpCtx_t *osd = RTYPEDDATA_DATA(self);
  RB_OBJ_WRITE(self, &osd->spec, spec);
  RB_OBJ_WRITE(self, &osd->proc, proc);
  memset(&osd->data, 0, sizeof(osd->data));
  osd->stop_signal = 0;
  return self;
}

VALUE OpCtx_spec(VALUE self) {
  OpCtx_t *osd = RTYPEDDATA_DATA(self);
  return osd->spec;
}

inline enum op_type OpCtx_type_get(VALUE self) {
  OpCtx_t *osd = RTYPEDDATA_DATA(self);
  return osd->type;
}

inline void OpCtx_type_set(VALUE self, enum op_type type) {
  OpCtx_t *osd = RTYPEDDATA_DATA(self);
  osd->type = type;
}

inline VALUE OpCtx_spec_get(VALUE self) {
  OpCtx_t *osd = RTYPEDDATA_DATA(self);
  return osd->spec;
}

inline VALUE OpCtx_proc_get(VALUE self) {
  OpCtx_t *osd = RTYPEDDATA_DATA(self);
  return osd->proc;
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

inline void OpCtx_ts_set(VALUE self, VALUE value) {
  OpCtx_t *osd = RTYPEDDATA_DATA(self);
  osd->data.ts = value_to_timespec(value);
}

inline struct sa_data *OpCtx_sa_get(VALUE self) {
  OpCtx_t *osd = RTYPEDDATA_DATA(self);
  return &osd->data.sa;
}

inline struct read_data *OpCtx_rd_get(VALUE self) {
  OpCtx_t *osd = RTYPEDDATA_DATA(self);
  return &osd->data.rd;
}

inline void OpCtx_rd_set(VALUE self, VALUE buffer, int buffer_offset, unsigned bg_id, int utf8_encoding) {
  OpCtx_t *osd = RTYPEDDATA_DATA(self);
  osd->data.rd.buffer = buffer;
  osd->data.rd.buffer_offset = buffer_offset;
  osd->data.rd.bg_id = bg_id;
  osd->data.rd.utf8_encoding = utf8_encoding;
}

inline int OpCtx_stop_signal_p(VALUE self) {
  OpCtx_t *osd = RTYPEDDATA_DATA(self);
  return osd->stop_signal;
}

inline void OpCtx_stop_signal_set(VALUE self) {
  OpCtx_t *osd = RTYPEDDATA_DATA(self);
  osd->stop_signal = 1;
}

void Init_OpCtx(void) {
  mIOU = rb_define_module("IOU");
  cOpCtx = rb_define_class_under(mIOU, "OpCtx", rb_cObject);
  rb_define_alloc_func(cOpCtx, OpCtx_allocate);

  rb_define_method(cOpCtx, "initialize", OpCtx_initialize, 2);
  rb_define_method(cOpCtx, "spec", OpCtx_spec, 0);
}
