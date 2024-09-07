# frozen_string_literal: true

require_relative 'helper'
require 'socket'

class IOURingTest < IOURingBaseTest
  def test_close
    ring2 = IOU::Ring.new
    refute ring2.closed?

    ring2.close
    assert ring2.closed?
  end

  def test_pending_ops
    assert_equal({}, ring.pending_ops)

    id = ring.prep_timeout(interval: 1)
    spec = ring.pending_ops[id]
    assert_equal id, spec[:id]
    assert_equal :timeout, spec[:op]
    assert_equal 1, spec[:interval]

    ring.prep_cancel(id)
    ring.submit
    ring.process_completions(true)

    assert_nil ring.pending_ops[id]
  end
end

class PrepTimeoutTest < IOURingBaseTest
  def test_prep_timeout
    interval = 0.03

    t0 = monotonic_clock
    id = ring.prep_timeout(interval: interval)
    assert_equal 1, id

    ring.submit
    c = ring.wait_for_completion
    elapsed = monotonic_clock - t0
    assert_in_range interval..(interval + 0.02), elapsed

    assert_kind_of Hash, c
    assert_equal id, c[:id]
    assert_equal :timeout, c[:op]
    assert_equal interval, c[:interval]
    assert_equal -Errno::ETIME::Errno, c[:result]
  end

  def test_prep_timeout_invalid_args
    assert_raises(ArgumentError) { ring.prep_timeout() }
    assert_raises(ArgumentError) { ring.prep_timeout(foo: 1) }
    assert_raises(ArgumentError) { ring.prep_timeout(1) }
  end
end

class PrepCancelTest < IOURingBaseTest
  def test_prep_cancel
    interval = 15
    timeout_id = ring.prep_timeout(interval: interval)
    assert_equal 1, timeout_id

    cancel_id = ring.prep_cancel(timeout_id)
    assert_equal 2, cancel_id

    ring.submit
    c = ring.wait_for_completion
    assert_equal cancel_id, c[:id]
    assert_equal 0, c[:result]

    c = ring.wait_for_completion
    assert_equal timeout_id, c[:id]
    assert_equal :timeout, c[:op]
    assert_equal interval, c[:interval]
    assert_equal -Errno::ECANCELED::Errno, c[:result]
  end

  def test_prep_cancel_kw
    interval = 15
    timeout_id = ring.prep_timeout(interval: interval)
    assert_equal 1, timeout_id

    cancel_id = ring.prep_cancel(id: timeout_id)
    assert_equal 2, cancel_id

    ring.submit
    c = ring.wait_for_completion
    assert_equal cancel_id, c[:id]
    assert_equal 0, c[:result]

    c = ring.wait_for_completion
    assert_equal timeout_id, c[:id]
    assert_equal :timeout, c[:op]
    assert_equal interval, c[:interval]
    assert_equal -Errno::ECANCELED::Errno, c[:result]
  end

  def test_prep_cancel_invalid_args
    assert_raises(ArgumentError) { ring.prep_cancel() }
    assert_raises(ArgumentError) { ring.prep_cancel('foo') }
    assert_raises(ArgumentError) { ring.prep_cancel({}) }
    assert_raises(TypeError) { ring.prep_cancel(id: 'bar') }
  end

  def test_prep_cancel_invalid_id
    cancel_id = ring.prep_cancel(id: 42)
    assert_equal 1, cancel_id

    ring.submit
    c = ring.wait_for_completion
    assert_equal cancel_id, c[:id]
    assert_equal -Errno::ENOENT::Errno, c[:result]
  end
end

class PrepTimeoutMultishotTest < IOURingBaseTest
  def test_prep_timeout_multishot
    interval = 0.03
    count = 0
    cancelled = false

    t0 = monotonic_clock
    id = ring.prep_timeout(interval: interval, multishot: true) do |c|
      case c[:result]
      when -Errno::ETIME::Errno
        count += 1
      when -Errno::ECANCELED::Errno
        cancelled = true
      end
    end
    ring.submit

    ring.process_completions(true)
    elapsed = monotonic_clock - t0
    assert_in_range interval..(interval + 0.02), elapsed
    assert_equal 1, count

    t0 = monotonic_clock
    ring.process_completions(true)
    elapsed = monotonic_clock - t0
    assert_in_range (interval - 0.01)..(interval + 0.02), elapsed
    assert_equal 2, count

    t0 = monotonic_clock
    ring.process_completions(true)
    elapsed = monotonic_clock - t0
    assert_in_range (interval - 0.01)..(interval + 0.02), elapsed
    assert_equal 3, count

    ring.prep_cancel(id)
    ring.submit
    c = ring.process_completions(true)
    assert_equal true, cancelled
    assert_equal 3, count
    assert_nil ring.pending_ops[id]
  end
end

class PrepWriteTest < IOURingBaseTest
  def test_prep_write
    r, w = IO.pipe
    s = 'foobar'

    id = ring.prep_write(fd: w.fileno, buffer: s)
    assert_equal 1, id

    ring.submit
    c = ring.wait_for_completion

    assert_kind_of Hash, c
    assert_equal id, c[:id]
    assert_equal :write, c[:op]
    assert_equal w.fileno, c[:fd]
    assert_equal s.bytesize, c[:result]

    w.close
    assert_equal s, r.read
  end

  def test_prep_write_with_len
    r, w = IO.pipe
    s = 'foobar'

    id = ring.prep_write(fd: w.fileno, buffer: s, len: 3)
    assert_equal 1, id

    ring.submit
    c = ring.wait_for_completion

    assert_kind_of Hash, c
    assert_equal id, c[:id]
    assert_equal :write, c[:op]
    assert_equal w.fileno, c[:fd]
    assert_equal 3, c[:result]

    w.close
    assert_equal s[0..2], r.read
  end

  def test_prep_write_invalid_args
    assert_raises(ArgumentError) { ring.prep_write() }
    assert_raises(ArgumentError) { ring.prep_write(foo: 1) }
    assert_raises(ArgumentError) { ring.prep_write(fd: 'bar') }
    assert_raises(ArgumentError) { ring.prep_write({}) }
  end

  def test_prep_write_invalid_fd
    r, w = IO.pipe
    s = 'foobar'

    id = ring.prep_write(fd: r.fileno, buffer: s)
    assert_equal 1, id

    ring.submit
    c = ring.wait_for_completion

    assert_kind_of Hash, c
    assert_equal id, c[:id]
    assert_equal :write, c[:op]
    assert_equal r.fileno, c[:fd]
    assert_equal -Errno::EBADF::Errno, c[:result]
  end
end

class PrepNopTest < IOURingBaseTest
  def test_prep_nop
    id = ring.prep_nop
    assert_equal 1, id

    ring.submit
    c = ring.wait_for_completion

    assert_kind_of Hash, c
    assert_equal id, c[:id]
    assert_nil c[:op]
    assert_equal 0, c[:result]
  end

  def test_nop_as_signal
    s1 = Queue.new
    s2 = Queue.new
    s3 = Queue.new
    s4 = Queue.new

    signaller = Thread.new do
      s1.pop
      id = ring.prep_nop
      ring.submit
      s2 << id
    end

    waiter = Thread.new do
      s3.pop
      s4 << ring.wait_for_completion
    end

    s3 << 'go'
    s1 << 'go'
    id = s2.pop
    c = s4.pop

    assert_kind_of Hash, c
    assert_equal id, c[:id]
    assert_nil c[:op]
    assert_equal 0, c[:result]    
  ensure
    signaller.kill rescue nil
  end
end

class ProcessCompletionsTest < IOURingBaseTest
  def test_process_completions_no_wait
    ret = ring.process_completions
    assert_equal 0, ret

    (1..3).each do |i|
      id = ring.prep_nop
      assert_equal i, id
    end

    ring.submit
    sleep 0.001

    ret = ring.process_completions
    assert_equal 3, ret
  end

  def test_process_completions_wait
    (1..3).each do |i|
      id = ring.prep_nop
      assert_equal i, id
    end

    ring.submit
    ret = ring.process_completions(true)
    assert_equal 3, ret
  end

  def test_process_completions_with_block
    r, w = IO.pipe

    id1 = ring.prep_write(fd: w.fileno, buffer: 'foo')
    id2 = ring.prep_write(fd: w.fileno, buffer: 'bar')
    id3 = ring.prep_write(fd: w.fileno, buffer: 'baz')
    ring.submit
    sleep 0.01

    completions = []

    ret = ring.process_completions do |c|
      completions << c
    end

    assert_equal 3, ret
    assert_equal 3, completions.size
    assert_equal [1, 2, 3], completions.map { _1[:id] }
    assert_equal [:write], completions.map { _1[:op] }.uniq
    assert_equal 9, completions.inject(0) { |t, c| t + c[:result] }

    w.close
    assert_equal 'foobarbaz', r.read
  end

  def test_process_completions_op_with_block
    cc = []

    id1 = ring.prep_timeout(interval: 0.01) { cc << 1 }
    id2 = ring.prep_timeout(interval: 0.02) { cc << 2 }
    ring.submit

    ret = ring.process_completions
    assert_equal 0, ret
    assert_equal [], cc

    sleep 0.02
    ret = ring.process_completions(true)

    assert_equal 2, ret
    assert_equal [1, 2], cc
  end

  def test_process_completions_op_with_block_no_submit
    cc = []

    id1 = ring.prep_timeout(interval: 0.01) { cc << 1 }
    id2 = ring.prep_timeout(interval: 0.02) { cc << 2 }

    ret = ring.process_completions
    assert_equal 0, ret
    assert_equal [], cc

    sleep 0.02
    ret = ring.process_completions(true)
    assert_equal 2, ret
    assert_equal [1, 2], cc
  end
end

class PrepReadTest < IOURingBaseTest
  def test_prep_read
    r, w = IO.pipe
    s = 'foobar'

    id = ring.prep_read(fd: r.fileno, buffer: +'', len: 8192)
    assert_equal 1, id
    ring.submit

    w << s

    c = ring.wait_for_completion

    assert_kind_of Hash, c
    assert_equal id, c[:id]
    assert_equal :read, c[:op]
    assert_equal r.fileno, c[:fd]
    assert_equal s.bytesize, c[:result]
    assert_equal s, c[:buffer]
  end

  def test_prep_read_empty
    r, w = IO.pipe

    id = ring.prep_read(fd: r.fileno, buffer: +'', len: 8192)
    assert_equal 1, id
    ring.submit

    w.close

    c = ring.wait_for_completion

    assert_kind_of Hash, c
    assert_equal id, c[:id]
    assert_equal :read, c[:op]
    assert_equal r.fileno, c[:fd]
    assert_equal 0, c[:result]
    assert_equal '', c[:buffer]
  end

  def test_prep_read_invalid_args
    assert_raises(ArgumentError) { ring.prep_read() }
    assert_raises(ArgumentError) { ring.prep_read(foo: 1) }
    assert_raises(ArgumentError) { ring.prep_read(fd: 'bar', buffer: +'') }
    assert_raises(ArgumentError) { ring.prep_read({}) }
  end

  def test_prep_read_bad_fd
    r, w = IO.pipe

    id = ring.prep_read(fd: w.fileno, buffer: +'', len: 8192)
    assert_equal 1, id

    ring.submit
    c = ring.wait_for_completion

    assert_kind_of Hash, c
    assert_equal id, c[:id]
    assert_equal :read, c[:op]
    assert_equal w.fileno, c[:fd]
    assert_equal -Errno::EBADF::Errno, c[:result]
  end

  def test_prep_read_with_block
    r, w = IO.pipe
    w << 'foobar'

    cc = nil
    id = ring.prep_read(fd: r.fileno, buffer: +'', len: 3) do |c|
      cc = c
    end

    ring.submit
    ring.process_completions

    assert_kind_of Hash, cc
    assert_equal id, cc[:id]
    assert_equal :read, cc[:op]
    assert_equal r.fileno, cc[:fd]
    assert_equal 3, cc[:result]
    assert_equal 'foo', cc[:buffer]

    id = ring.prep_read(fd: r.fileno, buffer: +'', len: 5) do |c|
      cc = c
    end
    assert_equal 'foo', cc[:buffer]

    ring.submit
    ring.process_completions

    assert_kind_of Hash, cc
    assert_equal id, cc[:id]
    assert_equal :read, cc[:op]
    assert_equal r.fileno, cc[:fd]
    assert_equal 3, cc[:result]
    assert_equal 'bar', cc[:buffer]
  end

  def test_prep_read_with_buffer_offset
    buffer = +'foo'

    r, w = IO.pipe
    w << 'bar'

    id = ring.prep_read(fd: r.fileno, buffer: buffer, len: 100, buffer_offset: buffer.bytesize)
    ring.submit
    cc = ring.wait_for_completion

    assert_kind_of Hash, cc
    assert_equal id, cc[:id]
    assert_equal :read, cc[:op]
    assert_equal r.fileno, cc[:fd]
    assert_equal 3, cc[:result]
    assert_equal 'foobar', buffer
  end

  def test_prep_read_with_negative_buffer_offset
    buffer = +'foo'

    r, w = IO.pipe
    w << 'bar'

    id = ring.prep_read(fd: r.fileno, buffer: buffer, len: 100, buffer_offset: -1)
    ring.submit
    cc = ring.wait_for_completion

    assert_kind_of Hash, cc
    assert_equal id, cc[:id]
    assert_equal :read, cc[:op]
    assert_equal r.fileno, cc[:fd]
    assert_equal 3, cc[:result]
    assert_equal 'foobar', buffer



    buffer = +'foogrr'

    r, w = IO.pipe
    w << 'bar'

    id = ring.prep_read(fd: r.fileno, buffer: buffer, len: 100, buffer_offset: -4)
    ring.submit
    cc = ring.wait_for_completion

    assert_kind_of Hash, cc
    assert_equal id, cc[:id]
    assert_equal :read, cc[:op]
    assert_equal r.fileno, cc[:fd]
    assert_equal 3, cc[:result]
    assert_equal 'foobar', buffer
  end
end

class PrepCloseTest < IOURingBaseTest
  def test_prep_close
    r, w = IO.pipe
    fd = w.fileno

    id = ring.prep_close(fd: fd)
    assert_equal 1, id
    ring.submit

    c = ring.wait_for_completion
    assert_kind_of Hash, c
    assert_equal id, c[:id]
    assert_equal :close, c[:op]
    assert_equal fd, c[:fd]
    assert_equal 0, c[:result]

    assert_raises(Errno::EBADF) { w << 'fail' }

    id = ring.prep_close(fd: fd)
    assert_equal 2, id
    ring.submit

    c = ring.wait_for_completion
    assert_kind_of Hash, c
    assert_equal id, c[:id]
    assert_equal :close, c[:op]
    assert_equal fd, c[:fd]
    assert_equal -Errno::EBADF::Errno, c[:result]

  end

  def test_prep_close_invalid_args
    assert_raises(ArgumentError) { ring.prep_close() }
    assert_raises(ArgumentError) { ring.prep_close({}) }
    assert_raises(ArgumentError) { ring.prep_close(foo: 1) }
    assert_raises(TypeError) { ring.prep_close(fd: 'bar') }
  end

  def test_prep_close_invalid_fd
    id = ring.prep_close(fd: 9999)
    assert_equal 1, id

    ring.submit
    c = ring.wait_for_completion

    assert_kind_of Hash, c
    assert_equal id, c[:id]
    assert_equal :close, c[:op]
    assert_equal 9999, c[:fd]
    assert_equal -Errno::EBADF::Errno, c[:result]
  end
end

class PrepAcceptTest < IOURingBaseTest
  def setup
    super
    @port = 9000 + rand(1000)
    @server = TCPServer.open('127.0.0.1', @port)
  end

  def teardown
    @server.close
    super
  end

  def test_prep_accept
    id = ring.prep_accept(fd: @server.fileno)
    ring.submit

    t = Thread.new do
      client = TCPSocket.new('127.0.0.1', @port)
    end

    c = ring.wait_for_completion
    assert_equal id, c[:id]
    assert_equal :accept, c[:op]
    fd = c[:result]
    assert fd > 0
  ensure
    t&.kill rescue nil
  end

  def test_prep_accept_invalid_args
    assert_raises(ArgumentError) { ring.prep_accept() }
    assert_raises(ArgumentError) { ring.prep_accept(foo: 1) }
    assert_raises(TypeError) { ring.prep_accept(fd: 'bar') }
    assert_raises(ArgumentError) { ring.prep_accept({}) }
  end

  def test_prep_accept_bad_fd
    id = ring.prep_accept(fd: STDIN.fileno)
    assert_equal 1, id

    ring.submit
    c = ring.wait_for_completion

    assert_kind_of Hash, c
    assert_equal id, c[:id]
    assert_equal :accept, c[:op]
    assert_equal STDIN.fileno, c[:fd]
    assert_equal -Errno::ENOTSOCK::Errno, c[:result]
  end

  def test_prep_accept_multishot
    id = ring.prep_accept(fd: @server.fileno, multishot: true)
    ring.submit

    tt = []

    connect = -> {
      tt << Thread.new do
        client = TCPSocket.new('127.0.0.1', @port)
      end
    }

    fds = []

    3.times do |i|
      connect.call
      c = ring.wait_for_completion
      assert_equal id, c[:id]
      assert_equal :accept, c[:op]
      fd = c[:result]
      assert fd > 0
      assert ring.pending_ops[id]

      fds << fd
    end

    assert_equal 3, fds.uniq.size

    ring.prep_cancel(id)
    ring.process_completions

    assert_nil ring.pending_ops[id]
  ensure
    tt.each { |t| t&.kill rescue nil }
  end
end

class EmitTest < IOURingBaseTest
  def test_emit
    o = { foo: 'bar' }
    id = ring.emit(o)
    assert_equal 1, id

    c = ring.wait_for_completion
    assert_equal o, c
    assert_equal id, c[:id]
    assert_equal :emit, c[:op]
    assert_equal 0, c[:result]
  end
end

class ProcessCompletionsLoopTest < IOURingBaseTest
  def test_loop_stop
    ring.emit(signal: :stop)

    cc = []
    ring.process_completions_loop do |c|
      p c
      cc << c
    end

    assert_equal [], cc
  end

  def test_loop
    ring.emit(value: 1)
    ring.emit(value: 2)
    ring.emit(value: 3)
    ring.emit(signal: :stop)
    ring.emit(value: 4)

    cc = []
    ring.process_completions_loop do |c|
      cc << c
    end
    assert_equal (1..3).to_a, cc.map { _1[:value] }

    c = ring.wait_for_completion
    assert_equal 4, c[:value]
  end
end

class PrepReadMultishotTest < IOURingBaseTest
  def test_prep_read_multishot
    r, w = IO.pipe

    bb = []
    bgid = ring.setup_buffer_ring(size: 4096, count: 1024)
    assert_equal 1, bgid

    id = ring.prep_read(fd: r.fileno, multishot: true, buffer_group: bgid)
    assert_equal 1, id
    ring.submit

    w << 'foo'
    c = ring.wait_for_completion
    assert_kind_of Hash, c
    assert_equal id, c[:id]
    assert_equal :read, c[:op]
    assert_equal r.fileno, c[:fd]
    assert_equal 3, c[:result]
    assert_equal 'foo', c[:buffer]
    refute_nil ring.pending_ops[id]

    w << 'bar'
    c = ring.wait_for_completion
    assert_kind_of Hash, c
    assert_equal id, c[:id]
    assert_equal :read, c[:op]
    assert_equal r.fileno, c[:fd]
    assert_equal 3, c[:result]
    assert_equal 'bar', c[:buffer]
    refute_nil ring.pending_ops[id]

    w.close
    c = ring.wait_for_completion
    assert_kind_of Hash, c
    assert_equal id, c[:id]
    assert_equal :read, c[:op]
    assert_equal r.fileno, c[:fd]
    assert_equal 0, c[:result]
    assert_nil ring.pending_ops[id]
  end
end
