cask "ex-gf-sd-card" do
  version "1.0.1"
  sha256 "60a340fddabd1c7ad24ff6167ec40fd085166e39aec08d0b688753cac0cae58f"

  url "https://github.com/femifleming/ex-gf/releases/download/v#{version}/EX-SD-Card.zip"
  name "EX-GF SD card"
  desc "Free sample library (multisamples, wavetables, exciters) for EX-GF"
  homepage "https://github.com/femifleming/ex-gf"

  artifact "EX SD Card", target: "~/Music/EX SD Card"
end
