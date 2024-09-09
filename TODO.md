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

- Add support for ractors

  https://news.ycombinator.com/item?id=41490988
