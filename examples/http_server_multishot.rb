require_relative '../lib/iou'
require 'socket'
require 'http/parser'

def log(msg)
  # return
  STDERR.puts msg
end

socket = TCPServer.open('127.0.0.1', 1234)
log 'Listening on port 1234... (multishot read)'

@ring = IOU::Ring.new
@bg_id = @ring.setup_buffer_ring(count: 1024, size: 4096)

@ring.prep_accept(fd: socket.fileno, multishot: true) do |c|
  setup_connection(c[:result]) if c[:result] > 0
end

def setup_connection(fd)
  log "Connection accepted fd #{fd}"

  parser = Http::Parser.new
  parser.on_message_complete = -> {
    http_send_response(fd, "Hello, world!\n")
  }

  http_prep_read(fd, parser)
end

def http_prep_read(fd, parser)
  id = @ring.prep_read(fd: fd, multishot: true, buffer_group: @bg_id) do |c|
    if c[:result] > 0
      parser << c[:buffer]
    elsif c[:result] == 0
      log "Connection closed by client on fd #{fd}"
    else
      if c[:result] != -Errno::ECANCELED::Errno
        log "Got error #{c[:result]} on fd #{fd}, closing connection..."
      end
      @ring.prep_close(fd: fd) do |c|
        log "Connection closed on fd #{fd}, result #{c[:result]}"
      end
    end
  rescue HTTP::Parser::Error
    puts "Error parsing, closing connection..."
    @ring.prep_cancel(id)
  end
end

def http_send_response(fd, body)
  msg = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nConnection: keep-alive\r\nContent-Length: #{body.bytesize}\r\n\r\n#{body}"
  @ring.prep_write(fd: fd, buffer: msg)
end

trap('SIGINT') { exit! }
@ring.process_completions_loop
