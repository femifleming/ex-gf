cask "ex-girlfriend" do
  version "1.0.0"
  sha256 "f0b1808fcda0dfda39316599f9c747eb45e55a921857ddaa508bfd52552e6feb"

  url "https://github.com/femifleming/ex-girlfriend/releases/download/v#{version}/ex-girlfriend-#{version}-macOS.zip"
  name "ex-girlfriend"
  desc "disting EX-style instruments: multisample + auto-sampler, poly wavetable, poly FM, poly exciter"
  homepage "https://github.com/femifleming/ex-girlfriend"

  depends_on macos: ">= :big_sur"

  plugins = ["EX Multisample", "EX Poly Wavetable", "EX Poly FM", "EX Poly Exciter"]
  plugins.each do |p|
    app "ex-girlfriend-#{version}-macOS/Applications/#{p}.app"
    vst3_plugin "ex-girlfriend-#{version}-macOS/VST3/#{p}.vst3"
    audio_unit_plugin "ex-girlfriend-#{version}-macOS/Components/#{p}.component"
  end

  # the builds are ad-hoc signed (not notarised): clear the download quarantine so they open/load
  postflight do
    paths = plugins.flat_map do |p|
      ["#{appdir}/#{p}.app",
       File.expand_path("~/Library/Audio/Plug-Ins/VST3/#{p}.vst3"),
       File.expand_path("~/Library/Audio/Plug-Ins/Components/#{p}.component")]
    end
    system_command "/usr/bin/xattr", args: ["-dr", "com.apple.quarantine", *paths], must_succeed: false
  end

  zap trash: "~/Library/Application Support/ex-girlfriend"

  caveats <<~EOS
    The free sample library is a separate cask:
      brew install --cask ex-girlfriend-sd-card
    (or choose any folder - e.g. a real disting EX MicroSD card - with the SD button in the plug-ins)
  EOS
end
