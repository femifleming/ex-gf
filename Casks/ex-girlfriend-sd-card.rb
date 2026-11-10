cask "ex-girlfriend-sd-card" do
  version "1.0.0"
  sha256 "60a340fddabd1c7ad24ff6167ec40fd085166e39aec08d0b688753cac0cae58f"

  url "https://github.com/femifleming/ex-girlfriend/releases/download/v#{version}/EX-SD-Card.zip"
  name "ex-girlfriend SD card"
  desc "Free sample library (multisamples, wavetables, exciters) for ex-girlfriend"
  homepage "https://github.com/femifleming/ex-girlfriend"

  artifact "EX SD Card", target: "~/Music/EX SD Card"
end
