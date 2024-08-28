#include "iou.h"

VALUE mIOU;

void Init_IOU(void) {
  mIOU = rb_define_module("IOU");
}
