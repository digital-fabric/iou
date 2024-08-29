# frozen_string_literal: true

require 'bundler/setup'
require_relative './coverage' if ENV['COVERAGE']
require 'iou'
require 'minitest/autorun'

module ::Kernel
  def debug(**h)
    k, v = h.first
    h.delete(k)

    rest = h.inject(+'') { |s, (k, v)| s << "  #{k}: #{v.inspect}\n" }
    STDOUT.orig_write("#{k}=>#{v} #{caller[0]}\n#{rest}")
  end

  def trace(*args)
    STDOUT.orig_write(format_trace(args))
  end

  def format_trace(args)
    if args.first.is_a?(String)
      if args.size > 1
        format("%s: %p\n", args.shift, args)
      else
        format("%s\n", args.first)
      end
    else
      format("%p\n", args.size == 1 ? args.first : args)
    end
  end

  def monotonic_clock
    ::Process.clock_gettime(::Process::CLOCK_MONOTONIC)
  end
end

module Minitest::Assertions
  def setup
    sleep 0.0001
  end

  def assert_in_range exp_range, act
    msg = message(msg) { "Expected #{mu_pp(act)} to be in range #{mu_pp(exp_range)}" }
    assert exp_range.include?(act), msg
  end
end
