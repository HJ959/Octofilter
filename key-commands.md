# embed new background
xxd -i "ui/skins/background.png" | sed '1s/.*/static const unsigned char kBgImageData[] = {/' | sed 's/^unsigned int.*/static const unsigned int
  kBgImageDataSize = 432647;/' > ui/BgImage.hpp
  
# build and move
cmake --build $bld --config Release --parallel
Remove-Item -Recurse -Force "C:\Program Files (x86)\Common Files\VST3\Octofilter.vst3" -ErrorAction SilentlyContinue
Copy-Item -Recurse -Force "$bld\bin\Octofilter.vst3" "C:\Program Files (x86)\Common Files\VST3\"