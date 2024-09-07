# io_uring ops

- [v] multishot timeout
- [v] close
- [v] accept
- [v] multishot accept
- [ ] buffer rings

  See https://github.com/axboe/liburing/wiki/io_uring-and-networking-in-2023#provided-buffers

  ```ruby
  # setup
  bgid = ring.setup_buffer_ring(size: 4096, count: 1024)

  # usage
  ring.prep_read(fd: socket_fd, buffer_group: bgid, multishot: true) do |c|
    if c[:result] > 0
      # data from the buffer ring is automatically copied over to the buffer
      process_read_data(c[:buffer])
    end
  end

  # we can also support writing to the end of a given buffer:
  buffer = +''
  ring.prep_read(fd: socket_fd, buffer_group: bgid, multishot: true, buffer: buffer, buffer_offset: -1) do |c|
    if c[:result] > 0
      read_lines_from_buffer(buffer)
    end
  end
  ```

- [ ] completion processing loop
  - [ ] loop method
  - [ ] signal method to stop loop (can also be used for other stuff)

  ```ruby
  Thread.new do
    sleep 3
    ring.signal(:stop)
  end
  ring.process_completions_loop
  ```

  Another possibility:

  ```ruby
  loop_stop = IOU::Signal.new
  Thread.new do
    sleep 3
    ring.emit(loop_stop)
  end
  ring.loop(signal: loop_stop)
  ```

  Take 3:

  ```ruby
  Thread.new do
    sleep 3
    ring.emit(signal: :stop)
  end
  ring.loop
  ```

- [ ] recv
- [ ] send
