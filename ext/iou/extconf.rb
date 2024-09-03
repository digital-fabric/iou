# frozen_string_literal: true

require 'rubygems'
require 'mkmf'
require 'rbconfig'

dir_config 'iou_ext'

KERNEL_INFO_RE = /Linux (\d)\.(\d+)(?:\.)?((?:\d+\.?)*)(?:\-)?([\w\-]+)?/
def get_config
  config = { linux: !!(RUBY_PLATFORM =~ /linux/) }
  raise "IOU only works on Linux!" if !config[:linux]

  kernel_info = `uname -sr`
  m = kernel_info.match(KERNEL_INFO_RE)
  raise "Could not parse Linux kernel information (#{kernel_info.inspect})" if !m

  version, major_revision, distribution = m[1].to_i, m[2].to_i, m[4]

  combined_version = version.to_i * 100 + major_revision.to_i
  raise "IOU requires kernel version 6.4 or newer!" if combined_version < 604

  config[:kernel_version]     = combined_version
  config[:pidfd_open]         = combined_version > 503
  config[:multishot_accept]   = combined_version >= 519
  config[:multishot_recv]     = combined_version >= 600
  config[:multishot_recvmsg]  = combined_version >= 600
  config[:multishot_timeout]  = combined_version >= 604
  config[:submit_all_flag]    = combined_version >= 518
  config[:coop_taskrun_flag]  = combined_version >= 519
  config[:single_issuer_flag] = combined_version >= 600

  config
end

config = get_config
puts "Building IOU (\n#{config.map { |(k, v)| "  #{k}: #{v}\n"}.join})"

# require_relative 'zlib_conf'

liburing_path = File.expand_path('../../vendor/liburing', __dir__)
FileUtils.cd liburing_path do
  system('./configure', exception: true)
  FileUtils.cd File.join(liburing_path, 'src') do
    system('make', 'liburing.a', exception: true)
  end
end

if !find_header 'liburing.h', File.join(liburing_path, 'src/include')
  raise "Couldn't find liburing.h"
end

if !find_library('uring', nil, File.join(liburing_path, 'src'))
  raise "Couldn't find liburing.a"
end

def define_bool(name, value)
  $defs << "-D#{name}=#{value ? 1 : 0 }"
end

$defs << '-DHAVE_IO_URING_PREP_MULTISHOT_ACCEPT'  if config[:multishot_accept]
$defs << '-DHAVE_IO_URING_PREP_RECV_MULTISHOT'    if config[:multishot_recv]
$defs << '-DHAVE_IO_URING_PREP_RECVMSG_MULTISHOT' if config[:multishot_recvmsg]
$defs << '-DHAVE_IO_URING_TIMEOUT_MULTISHOT'      if config[:multishot_timeout]
$defs << '-DHAVE_IORING_SETUP_SUBMIT_ALL'         if config[:submit_all_flag]
$defs << '-DHAVE_IORING_SETUP_COOP_TASKRUN'       if config[:coop_taskrun_flag]
$CFLAGS << ' -Wno-pointer-arith'

CONFIG['optflags'] << ' -fno-strict-aliasing'

create_makefile 'iou_ext'
