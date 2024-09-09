require_relative '../lib/iou'
require 'socket'
require 'fiber'

class ::Fiber
  attr_accessor :__op_id
end

class Scheduler
  class Cancel < Exception
  end

  attr_reader :ring

  def initialize
    @ring = IOU::Ring.new
    @runqueue = []
  end

  def switchpoint
    while true
      f, v = @runqueue.shift
      if f
        return f.transfer(v)
      end

      @ring.process_completions
    end
  end

  def fiber_wait(op_id)
    Fiber.current.__op_id = op_id
    v = switchpoint
    Fiber.current.__op_id = nil
    raise v if v.is_a?(Exception)
    
    v
  end

  def read(**args)
    f = Fiber.current
    id = ring.prep_read(**args) do |c|
      if c[:result] < 0
        @runqueue << [f, RuntimeError.new('error')]
      else
        @runqueue << [f, c[:buffer]]
      end
    end
    fiber_wait(id)
  end

  def write(**args)
    f = Fiber.current
    id = ring.prep_write(**args) do |c|
      if c[:result] < 0
        @runqueue << [f, RuntimeError.new('error')]
      else
        @runqueue << [f, c[:result]]
      end
    end
    fiber_wait(id)
  end

  def sleep(interval)
    f = Fiber.current
    id = ring.prep_timeout(interval: interval) do |c|
      if c[:result] == Errno::ECANCELED::Errno
        @runqueue << [f, c[:result]]
      else
        @runqueue << [f, c[:result]]
      end
    end
    fiber_wait(id)
  end

  def cancel_fiber_op(f)
    op_id = f.__op_id
    if op_id
      ring.prep_cancel(op_id)
    end
  end

  def move_on_after(interval)
    f = Fiber.current
    cancel_id = ring.prep_timeout(interval: interval) do |c|
      if c[:result] != Errno::ECANCELED::Errno
        cancel_fiber_op(f)
      end
    end
    v = yield
    ring.prep_cancel(cancel_id)
    v
  end
end

s = Scheduler.new

puts "Going to sleep..."
s.sleep 3
puts "Woke up"

s.move_on_after(1) do
  puts "Going to sleep (move on after 1 second)"
  s.sleep 3
end
