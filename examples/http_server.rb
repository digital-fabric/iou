require_relative '../lib/iou'
require 'socket'
require 'http/parser'

def log(msg)
  # return
  STDERR.puts msg
end

socket = TCPServer.open('127.0.0.1', 1234)
log 'Listening on port 1234...'

@ring = IOU::Ring.new

@ring.prep_accept(fd: socket.fileno, multishot: true) do |c|
  setup_connection(c[:result]) if c[:result] > 0
end

def setup_connection(fd)
  log "Connection accepted fd #{fd}"

  parser = Http::Parser.new
  parser.on_message_complete = -> {
    http_send_response(fd, "Hello, world!\n") do
      @ring.prep_close(fd: fd)
    end
  }

  http_prep_read(fd, parser)
end

def http_prep_read(fd, parser)
  buffer = +''
  @ring.prep_read(fd: fd, buffer: buffer, len: 4096) do |c|
    if c[:result] > 0
      http_prep_read(fd, parser)
      parser << buffer
    elsif c[:result] == 0
      log "Connection closed by client on fd #{fd}"
    else
      log "Got error #{c[:result]} on fd #{fd}, closing connection..."
      @ring.prep_close(fd: fd) do |c|
        log "Connection closed on fd #{fd}, result #{c[:result]}"
      end
    end
  end
end

def http_send_response(fd, body)
  msg = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nConnection: keep-alive\r\nContent-Length: #{body.bytesize}\r\n\r\n#{body}"
  @ring.prep_write(fd: fd, buffer: msg)
end

trap('SIGINT') { exit! }
@ring.process_completions_loop
