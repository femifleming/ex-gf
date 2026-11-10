class ExSuite < Formula
  desc "Disting EX-style instruments (VST3/AU/LV2 + standalone apps)"
  homepage "https://github.com/femifleming/ex-suite"
  url "https://github.com/femifleming/ex-suite.git",
      tag:      "v1.0.0",
      revision: "GIT_REVISION"
  license "AGPL-3.0-only"
  head "https://github.com/femifleming/ex-suite.git", branch: "main"

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
    depends_on "libxinerama"
    depends_on "libxrandr"
    depends_on "libxrender"
  end

  PLUGINS = %w[EXMultisample EXPolyWavetable EXPolyFM EXPolyExciter].freeze

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
        (Prebuilt binaries: brew install --cask ex-suite)
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
    assert_predicate lib/"vst3/EX Poly FM.vst3", :exist?
  end
end
