# IOU: io_uring for Ruby

<a href="http://rubygems.org/gems/iou">
  <img src="https://badge.fury.io/rb/iou.svg" alt="Ruby gem">
</a>
<a href="https://github.com/digital-fabric/iou/actions?query=workflow%3ATests">
  <img src="https://github.com/digital-fabric/iou/workflows/Tests/badge.svg" alt="Tests">
</a>
<a href="https://github.com/digital-fabric/iou/blob/master/LICENSE">
  <img src="https://img.shields.io/badge/license-MIT-blue.svg" alt="MIT License">
</a>

## What is IOU?

IOU is a Ruby gem for working with the io_uring API. More information will be forthcoming...

## Basic Usage

IOU provides a simple and idiomatic API for working with io_uring. IOU does not
make any assumptions about the concurrency model in your application. It can be
used in multi-threaded, multi-fibered, or callback-based apps. It largely
follows the [liburing](https://github.com/axboe/liburing/) API, but provides
certain Ruby-esque amenities to make it easier to use io_uring in Ruby apps.

Operations are performed by preparing them (using the different `prep_xxx`
methods), submitting them, then waiting or polling for completion of those
operations. Here's a simple example of how IOU is used:

```ruby
ring = IOU::Ring.new
# prepare a 3 second timeout operation
ring.prep_timeout(period: 3)

# submit all unsubmitted operations to io_uring
ring.submit

# wait for completion, this will block for 3 seconds!
ring.wait_for_completion
#=> { id: 1, op: :timeout, period: 3}
```

## Callback-style completions

Callback-style handling of completions can be done using `#process_completions`:

```ruby
timeout_id = ring.prep_timeout(period: 3)
ring.submit

# passing true to process_completions means wait for at least one completion
# to be available
ring.process_completions(true) do |completion|
  # the completion is a hash containing the operation spec
  if completion[:id] == timeout_id
    puts "timeout elapsed!"
  end
end

# another way to do callbacks is to provide a block to prep_timeout:
timeout_id = ring.prep_timeout(period: 3) do
  puts "timeout elapsed"
end

# wait for completion and trigger the callback
ring.process_completions(true)
```

# I/O with IOU

I/O operations, such as `read`, `write`, `recv`, `send`, `accept` etc are done
using raw fd's.

```ruby
# write to STDOUT using IOU
ring.prep_write(fd: STDOUT.fileno, buffer: 'Hello world!')
ring.submit
ring.wait_for_completion
```

More examples will be added in the future.

# IOU used as an event loop

IOU can be used to implement an event loop, much in the style of
[EventMachine](https://github.com/eventmachine/eventmachine). Here's a sketch of
how this can be done:

```ruby
class IOUEventLoop
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
    pending.each(&:call)
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
    @stopped = false
    signal if @waiting      
  end

  def timeout(delay, &block)
    ring.prep_timeout(period: delay, &block)
    ring.submit
  end

  def interval(period, &block)
    ring.prep_timeout(period: period, recurring: true, &block)
    ring.submit
  end
end

# exaple usage
event_loop = IOUEventLoop.new
event_loop.run do
  event_loop.interval(1) do
    puts "The time is #{Time.now}"
  end
end
```
