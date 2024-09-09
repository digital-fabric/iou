require_relative '../lib/iou'
require 'socket'
require 'http/parser'

socket = TCPServer.open('127.0.0.1', 1234)
puts 'Listening on port 1234... (multishot read)'

@ring = IOU::Ring.new
@buffer_group = @ring.setup_buffer_ring(count: 1024, size: 4096)

@ring.prep_accept(fd: socket.fileno, multishot: true) do |c|
  http_handle_connection(c[:result]) if c[:result] > 0
end

def http_handle_connection(fd)
  parser = Http::Parser.new
  parser.on_message_complete = -> { http_send_response(fd, "Hello, world!\n") }

  @ring.prep_read(fd: fd, multishot: true, buffer_group: @buffer_group) do |c|
    if c[:result] > 0
      parser << c[:buffer]
    else
      puts "Connection closed on fd #{fd}"
    end
  end
end

def http_send_response(fd, body)
  msg = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nConnection: keep-alive\r\nContent-Length: #{body.bytesize}\r\n\r\n#{body}"
  @ring.prep_write(fd: fd, buffer: msg)
end

trap('SIGINT') { exit! }
@ring.process_completions_loop
