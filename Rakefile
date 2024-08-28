# frozen_string_literal: true

require "bundler/gem_tasks"
require "rake/clean"
require "rake/testtask"
require "rake/extensiontask"

Rake::ExtensionTask.new("iou_ext") do |ext|
  ext.ext_dir = "ext/iou"
end

task :recompile => [:clean, :compile]
task :default => [:compile, :test]

test_config = -> (t) {
  t.libs << "test"
  t.test_files = FileList["test/**/test_*.rb"]
}
Rake::TestTask.new(test: :compile, &test_config)

task :stress_test do
  exec 'ruby test/stress.rb'
end

CLEAN.include "**/*.o", "**/*.so", "**/*.so.*", "**/*.a", "**/*.bundle", "**/*.jar", "pkg", "tmp"

task :release do
  require_relative './lib/iou/version'
  version = IOU::VERSION

  puts 'Building iou...'
  `gem build iou.gemspec`

  puts "Pushing iou #{version}..."
  `gem push iou-#{version}.gem`

  puts "Cleaning up..."
  `rm *.gem`
end
