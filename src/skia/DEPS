use_relative_paths = True

vars = {
  "checkout_chromium": False,
}

deps = {
  "third_party/externals/expat"           : "https://chromium.googlesource.com/external/github.com/libexpat/libexpat.git@8e49998f003d693213b538ef765814c7d21abada",
  "third_party/externals/freetype"        : "https://skia.googlesource.com/third_party/freetype2.git@0a3d2bb99b45b72e1d45185ab054efa993d97210",
  "third_party/externals/libgifcodec"     : "https://skia.googlesource.com/libgifcodec@d06d2a6d42baf6c0c91cacc28df2542a911d05fe",
  "third_party/externals/libjpeg-turbo"   : "https://chromium.googlesource.com/chromium/deps/libjpeg_turbo.git@e14cbfaa85529d47f9f55b0f104a579c1061f9ad",
  "third_party/externals/libpng"          : "https://skia.googlesource.com/third_party/libpng.git@ed217e3e601d8e462f7fd1e04bed43ac42212429",
  "third_party/externals/libwebp"         : "https://chromium.googlesource.com/webm/libwebp.git@845d5476a866141ba35ac133f856fa62f0b7445f",
  "third_party/externals/zlib"            : "https://chromium.googlesource.com/chromium/src/third_party/zlib@646b7f569718921d7d4b5b8e22572ff6c76f2596",

  "../src": {
    "url": "https://chromium.googlesource.com/chromium/src.git@c0b367bf9cab0b8a8a05ac0e39f88407a0c290c9",
    "condition": "checkout_chromium",
  },
}

recursedeps = [
  "common",
  "../src",
]

gclient_gn_args_from = 'src'
