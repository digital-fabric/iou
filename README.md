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

IOU is a Ruby gem for working with the io_uring API. IOU provides a simple and
idiomatic API for working with io_uring. IOU does not make any assumptions about
the concurrency model in your application. It can be used in multi-threaded,
multi-fibered, or callback-based apps. It largely follows the
[liburing](https://github.com/axboe/liburing/) API, but provides certain
Ruby-esque amenities to make it easier to use io_uring in Ruby apps.

## Features

- Prepare and submit operations: accept, read, write, timeout, nop.
- Cancel operations.
- Multishot timeout, accept, read.
- Setup buffer ring for multishot read (provides a nice boost for read performance).
- Associate arbitrary data with operations.
- Run callback on completion of operations.
- Emit arbitrary values for in-app signalling.

## Basic Usage

Operations are performed by preparing them (using the different `#prep_xxx`
methods), submitting them, then waiting or polling for completion of those
operations. Here's a simple example of how IOU is used:

```ruby
ring = IOU::Ring.new
# prepare a 3 second timeout operation
ring.prep_timeout(interval: 3)

# submit all unsubmitted operations to io_uring
ring.submit

# wait for completion, this will block for 3 seconds!
ring.wait_for_completion
#=> { id: 1, op: :timeout, interval: 3}
```

## Cancelling operations

Any operation can be cancelled by calling `#prep_cancel`:

```ruby
id = ring.prep_timeout(interval: 3)
...
ring.prep_cancel(id)
```

## Callback-style completions

Callback-style handling of completions can be done using `#process_completions`:

```ruby
timeout_id = ring.prep_timeout(interval: 3)
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
timeout_id = ring.prep_timeout(interval: 3) do
  puts "timeout elapsed"
end

# wait for completion and trigger the callback
ring.process_completions(true)
```

## I/O with IOU

I/O operations, such as `read`, `write`, `recv`, `send`, `accept` etc are done
using raw fd's.

```ruby
# write to STDOUT using IOU
ring.prep_write(fd: STDOUT.fileno, buffer: 'Hello world!')
ring.submit
ring.wait_for_completion
```

## Examples

Examples for using IOU can be found in the examples directory:

- Echo server
- HTTP server
- Event loop (in the style of EventMachine)
- Fiber-based concurrency
