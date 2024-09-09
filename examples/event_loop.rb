require_relative '../lib/iou'
require 'socket'

class IOUEventLoop
  attr_reader :ring

  def initialize
    @ring = IOU::Ring.new
  end

  def async_queue
    @async_queue ||= []
  end

  def async(&block)
    @async_queue << block
    signal if @waiting
  end

  def signal
    # generate an event to cause process_completions to return
    ring.prep_nop
    ring.submit
  end

  def run_async_tasks
    pending = @async_queue
    @async_queue = []
    pending&.each(&:call)
  end

  def run
    yield if block_given?
    while !@stopped
      run_async_tasks
      @waiting = true
      ring.process_completions
      @waiting = false
    end
  end

  def stop
    @stopped = true
    signal if @waiting
  end

  def timeout(delay, &block)
    ring.prep_timeout(interval: delay, &block)
    ring.submit
  end

  def interval(period, &block)
    ring.prep_timeout(interval: period, multishot: true, &block)
    ring.submit
  end
end

# exaple usage
event_loop = IOUEventLoop.new

trap('SIGINT') { event_loop.stop }

event_loop.run do
  event_loop.interval(1) do
    puts "The time is #{Time.now}"
  end
end

puts "Stopped"
