- Add UTF8 encoding option for multishot read.
- Implement `OpCtx` as wrapper for op specs. This removes the need to call
  `rb_hash_aref` upon completion (except for pulling the ctx from the
  `pending_ops` hash), leading to a significant performance improvement.

# 2024-09-08 Version 0.1

- First working version.