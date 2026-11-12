cask "ex-gf" do
  version "1.1.0"
  sha256 "8ee0f1d7c1922c7d3f3ba6a4ea4af4221944dc386f0671b9ae8d50021627d443"

  url "https://github.com/femifleming/ex-gf/releases/download/v#{version}/EX-GF-#{version}-macOS.zip"
  name "EX-GF"
  desc "Disting EX-style instruments and effects"
  homepage "https://github.com/femifleming/ex-gf"

  depends_on :macos

  plugins = ["EX Multisample", "EX Poly Wavetable", "EX Poly FM", "EX Poly Exciter", "EX Granulator", "EX Multi-FX",
             "EX Resonator", "EX Spectral Freeze", "EX Macro Oscillator 2"]
  plugins.each do |p|
    app "EX-GF-#{version}-macOS/Applications/#{p}.app"
    vst3_plugin "EX-GF-#{version}-macOS/VST3/#{p}.vst3"
    audio_unit_plugin "EX-GF-#{version}-macOS/Components/#{p}.component"
  end

  # the builds are ad-hoc signed (not notarised): clear the download quarantine so they open/load
  postflight_steps do
    run "/usr/bin/xattr",
        args:         [
          "-dr", "com.apple.quarantine",
          "{{appdir}}/EX Multisample.app",
          "/Users/{{user}}/Library/Audio/Plug-Ins/VST3/EX Multisample.vst3",
          "/Users/{{user}}/Library/Audio/Plug-Ins/Components/EX Multisample.component",
          "{{appdir}}/EX Poly Wavetable.app",
          "/Users/{{user}}/Library/Audio/Plug-Ins/VST3/EX Poly Wavetable.vst3",
          "/Users/{{user}}/Library/Audio/Plug-Ins/Components/EX Poly Wavetable.component",
          "{{appdir}}/EX Poly FM.app",
          "/Users/{{user}}/Library/Audio/Plug-Ins/VST3/EX Poly FM.vst3",
          "/Users/{{user}}/Library/Audio/Plug-Ins/Components/EX Poly FM.component",
          "{{appdir}}/EX Poly Exciter.app",
          "/Users/{{user}}/Library/Audio/Plug-Ins/VST3/EX Poly Exciter.vst3",
          "/Users/{{user}}/Library/Audio/Plug-Ins/Components/EX Poly Exciter.component",
          "{{appdir}}/EX Granulator.app",
          "/Users/{{user}}/Library/Audio/Plug-Ins/VST3/EX Granulator.vst3",
          "/Users/{{user}}/Library/Audio/Plug-Ins/Components/EX Granulator.component",
          "{{appdir}}/EX Multi-FX.app",
          "/Users/{{user}}/Library/Audio/Plug-Ins/VST3/EX Multi-FX.vst3",
          "/Users/{{user}}/Library/Audio/Plug-Ins/Components/EX Multi-FX.component",
          "{{appdir}}/EX Resonator.app",
          "/Users/{{user}}/Library/Audio/Plug-Ins/VST3/EX Resonator.vst3",
          "/Users/{{user}}/Library/Audio/Plug-Ins/Components/EX Resonator.component",
          "{{appdir}}/EX Spectral Freeze.app",
          "/Users/{{user}}/Library/Audio/Plug-Ins/VST3/EX Spectral Freeze.vst3",
          "/Users/{{user}}/Library/Audio/Plug-Ins/Components/EX Spectral Freeze.component",
          "{{appdir}}/EX Macro Oscillator 2.app",
          "/Users/{{user}}/Library/Audio/Plug-Ins/VST3/EX Macro Oscillator 2.vst3",
          "/Users/{{user}}/Library/Audio/Plug-Ins/Components/EX Macro Oscillator 2.component"
        ],
        must_succeed: false
  end

  zap trash: "~/Library/Application Support/EX-GF"

  caveats <<~EOS
    The plug-ins are not notarised. If your DAW refuses to load them, run:
      xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/EX*.vst3 ~/Library/Audio/Plug-Ins/Components/EX*.component

    The free sample library is a separate cask:
      brew install --cask ex-gf-sd-card
    (or choose any folder - e.g. a real disting EX MicroSD card - with the SD button in the plug-ins)
  EOS
end
