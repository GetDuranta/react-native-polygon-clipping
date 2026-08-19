require "json"

package = JSON.parse(File.read(File.join(__dir__, "package.json")))

Pod::Spec.new do |s|
  s.name         = "PolygonClipping"
  s.version      = package["version"]
  s.summary      = package["description"]
  s.homepage     = package["homepage"]
  s.license      = package["license"]
  s.authors      = package["author"]

  s.platforms    = { :ios => min_ios_version_supported }
  s.source       = { :git => "https://github.com/getduranta/react-native-polygon-clipping.git", :tag => "#{s.version}" }

  s.source_files = "ios/**/*.{h,m,mm}", "cpp/**/*.{hpp,cpp,c,h}", "ios/generated/**/*.{h,cpp,mm}"
  s.private_header_files = "ios/**/*.h"

  # The clipping core relies on exact IEEE-754 double semantics; keep FMA
  # contraction off.
  s.pod_target_xcconfig = {
    "OTHER_CPLUSPLUSFLAGS" => "$(inherited) -ffp-contract=off"
  }

  install_modules_dependencies(s)
end
