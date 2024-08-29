# frozen_string_literal: true

require_relative 'helper'

class IOUTest < Minitest::Test
  def test_timeout_simple
    ring = IOU::Ring.new
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
  end
end
