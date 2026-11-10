cask "ex-suite-sd-card" do
  version "1.0.0"
  sha256 "60a340fddabd1c7ad24ff6167ec40fd085166e39aec08d0b688753cac0cae58f"

  url "https://github.com/femifleming/ex-suite/releases/download/v#{version}/EX-SD-Card.zip"
  name "EX Suite SD card"
  desc "Free multisamples, wavetables, exciters and DX7 banks for EX Suite (disting EX format)"
  homepage "https://github.com/femifleming/ex-suite"

  artifact "EX SD Card", target: "~/Music/EX SD Card"
end
