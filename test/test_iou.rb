# frozen_string_literal: true

require_relative 'helper'

class IOURingBaseTest < Minitest::Test
  attr_accessor :ring
  
  def setup
    @ring = IOU::Ring.new
  end

  def teardown
    ring.close
  end
end

class IOURingTest < IOURingBaseTest
  def test_close
    ring2 = IOU::Ring.new
    refute ring2.closed?

    ring2.close
    assert ring2.closed?
  end
end

class PrepTimeoutTest < IOURingBaseTest
  def test_prep_timeout
    period = 0.3

    t0 = monotonic_clock
    id = ring.prep_timeout(period: period)
    assert_equal 1, id

    ring.submit
    c = ring.wait_for_completion
    elapsed = monotonic_clock - t0
    assert_in_range period..(period + 0.2), elapsed

    assert_kind_of Hash, c
    assert_equal id, c[:id]
    assert_equal :timeout, c[:op]
    assert_equal period, c[:period]
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
    period = 15
    timeout_id = ring.prep_timeout(period: period)
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
    assert_equal period, c[:period]
    assert_equal -Errno::ECANCELED::Errno, c[:result]
  end

  def test_prep_cancel_kw
    period = 15
    timeout_id = ring.prep_timeout(period: period)
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
    assert_equal period, c[:period]
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

class PrepWriteTest < IOURingBaseTest
  def test_prep_write
    ring = IOU::Ring.new
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
    ring = IOU::Ring.new
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
    ring = IOU::Ring.new
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
