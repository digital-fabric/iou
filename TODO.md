## io_uring ops

- [ ] recv
- [ ] send
- [ ] recvmsg
- [ ] sendmsg
- [ ] multishot recv
- [ ] multishot recvmsg
- [ ] poll
- [ ] multishot poll
- [ ] shutdown
- [ ] connect
- [ ] socket
- [ ] openat
- [ ] splice
- [ ] wait

- [ ] support for linking requests
  
  ```ruby
  ring.prep_write(fd: fd, buffer: 'foo', link: true)
  ring.prep_slice(fd: fd, src: src_fd, len: 4096)
  ```

- [ ] link timeout

  ```ruby
  # read or timeout in 3 seconds
  ring.prep_read(fd: fd, buffer: +'', len: 4096, timeout: 3)
  ```

## Add op ctx object

A ctx object is created for each operation, wraps the given spec hash, but adds
a fast C-level API for accessing op properties (the op type, etc), instead of
calling rb_hash_aref everytime we need to access properties. This should improve
performance (remains to be proven...)

This ctx object will replace the current OpCtx class.
