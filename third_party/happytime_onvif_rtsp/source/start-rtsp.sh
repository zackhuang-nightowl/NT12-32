#! /bin/sh

CUR=$(cd $(dirname $0); pwd)
$CUR/mklinks.sh
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$CUR/ffmpeg/lib/linux
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$CUR/zlib/lib/linux
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$CUR/openssl/lib/linux
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$CUR/libsrt/lib/linux
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$CUR/libsrtp/lib/linux
$CUR/rtspserver