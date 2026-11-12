class ExGf < Formula
  desc "Disting EX-style instruments and effects (VST3/AU/LV2 + standalone apps)"
  homepage "https://github.com/femifleming/ex-gf"
  url "https://github.com/femifleming/ex-gf.git",
      tag:      "v1.1.0",
      revision: "bd5a757bc0577a599aea8af8d878ac510eb89d2b"
  license "AGPL-3.0-only"
  head "https://github.com/femifleming/ex-gf.git", branch: "main"

  depends_on "cmake" => :build
  depends_on "ninja" => :build

  on_linux do
    depends_on "pkgconf" => :build
    depends_on "alsa-lib"
    depends_on "fontconfig"
    depends_on "freetype"
    depends_on "libx11"
    depends_on "libxcomposite"
    depends_on "libxcursor"
    depends_on "libxext"
    depends_on "libxi"
    depends_on "libxinerama"
    depends_on "libxrandr"
    depends_on "libxrender"
  end

  PLUGINS = %w[
    EXMultisample EXPolyWavetable EXPolyFM EXPolyExciter
    EXGranulator EXMultiFX EXResonator EXSpectralFreeze EXMacroOsc2
  ].freeze

  def install
    args = %w[-DEX_COPY_AFTER_BUILD=OFF -DEX_BUILD_TESTS=OFF]
    args << "-DCMAKE_OSX_ARCHITECTURES=#{Hardware::CPU.arch}" if OS.mac?
    system "cmake", "-S", ".", "-B", "build", "-G", "Ninja", *args, *std_cmake_args
    system "cmake", "--build", "build"
    PLUGINS.each do |t|
      art = "build/#{t}_artefacts/Release"
      (lib/"vst3").install Dir["#{art}/VST3/*.vst3"]
      if OS.mac?
        (lib/"Components").install Dir["#{art}/AU/*.component"]
        prefix.install Dir["#{art}/Standalone/*.app"]
      else
        (lib/"lv2").install Dir["#{art}/LV2/*.lv2"]
        bin.install Dir["#{art}/Standalone/*"]
      end
    end
  end

  def caveats
    if OS.mac?
      <<~EOS
        Link the plug-ins and apps into place:
          mkdir -p ~/Library/Audio/Plug-Ins/VST3 ~/Library/Audio/Plug-Ins/Components
          ln -sf #{opt_lib}/vst3/*.vst3 ~/Library/Audio/Plug-Ins/VST3/
          ln -sf #{opt_lib}/Components/*.component ~/Library/Audio/Plug-Ins/Components/
          ln -sf #{opt_prefix}/*.app ~/Applications/
        (Prebuilt binaries: brew install --cask ex-gf)
      EOS
    else
      <<~EOS
        Link the plug-ins into place:
          mkdir -p ~/.vst3 ~/.lv2
          ln -sf #{opt_lib}/vst3/*.vst3 ~/.vst3/
          ln -sf #{opt_lib}/lv2/*.lv2 ~/.lv2/
        The standalone apps ("EX Poly FM" etc.) are on your PATH.
      EOS
    end
  end

  test do
    assert_path_exists lib/"vst3/EX Poly FM.vst3"
  end
end
