require_relative './lib/iou/version'

Gem::Specification.new do |s|
  s.name        = 'iou'
  s.version     = IOU::VERSION
  s.licenses    = ['MIT']
  s.summary     = 'io_uring for Ruby'
  s.author      = 'Sharon Rosner'
  s.email       = 'sharon@noteflakes.com'
  s.files       = `git ls-files --recurse-submodules`.split.reject { |fn| fn =~ /liburing\/man/ }
  s.homepage    = 'https://github.com/digital-fabric/iou'
  s.metadata    = {
    "source_code_uri" => "https://github.com/digital-fabric/iou",
    "documentation_uri" => "https://www.rubydoc.info/gems/iou",
    "changelog_uri" => "https://github.com/digital-fabric/iou/blob/master/CHANGELOG.md"
  }
  s.rdoc_options = ["--title", "IOU", "--main", "README.md"]
  s.extra_rdoc_files = ["README.md"]
  s.extensions = ["ext/iou/extconf.rb"]
  s.require_paths = ["lib"]
  s.required_ruby_version = '>= 3.3'

  s.add_development_dependency  'rake-compiler',        '1.2.7'
  s.add_development_dependency  'minitest',             '5.25.1'
  # s.add_development_dependency  'simplecov',            '0.22.0'
  # s.add_development_dependency  'rubocop',              '1.45.1'
  # s.add_development_dependency  'pry',                  '0.14.2'

  # s.add_development_dependency  'msgpack',              '1.6.0'
  # s.add_development_dependency  'httparty',             '0.21.0'
  # s.add_development_dependency  'localhost',            '1.1.10'
  # s.add_development_dependency  'debug',                '1.8.0'
  s.add_development_dependency  'benchmark-ips',        '2.10.0'
end
