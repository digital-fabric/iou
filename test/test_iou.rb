# frozen_string_literal: true

require_relative 'helper'

class IOUTest < Minitest::Test
  attr_accessor :ring
  
  def setup
    @ring = IOU::Ring.new
  end

  def teardown
    ring.close
  end

  def test_close
    ring2 = IOU::Ring.new
    refute ring2.closed?

    ring2.close
    assert ring2.closed?
  end

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


  # def test_write
  #   ring = IOU::Ring.new
  #   r, w = IO.pipe
  #   s = 'foobar'

  #   ring.prep_write(fd: w.fileno, buffer: s)

  # end
end
