#!/usr/bin/env bash
# O_DIRECT 聚合写并发回归测试(主机 + ASAN)。
# 用途: 锁死 rsdk_rec "唯一线程写 writer" 不变式 —— 见 odirect_concurrency_test.c 头注。
#
#   A       期望 PASS(单线程 O_DIRECT 逐字节一致)
#   Bfixed  期望 PASS(两线程串行化)
#   Brace   期望 ASAN heap-buffer-overflow(证明无串行会撕裂聚合缓冲 = 真机根因)
#
# 用法:  ./run.sh            # 依次跑 A / Bfixed / Brace
#        ./run.sh Brace      # 只跑某模式
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
REC="$HERE/.."
BUILD="${BUILD:-/tmp/rsdk_asan_build}"
IMGDIR="${IMGDIR:-/tmp}"

# 1) ASAN 编译 librsdk(关 metadata 免 sqlite 慢; O_DIRECT/聚合写不依赖它)
if [ ! -f "$BUILD/lib/librsdk.a" ] || [ "$REC/src/rsdk_rec.c" -nt "$BUILD/lib/librsdk.a" ]; then
  echo "== 构建 librsdk (ASAN) =="
  rm -rf "$BUILD"; mkdir -p "$BUILD"
  cmake -S "$REC" -B "$BUILD" -DCMAKE_BUILD_TYPE=Debug -DRSDK_BUILD_EXAMPLES=OFF \
        -DRSDK_WITH_METADATA=OFF \
        -DCMAKE_C_FLAGS="-fsanitize=address -fno-omit-frame-pointer -g -O1" >/dev/null || exit 1
  cmake --build "$BUILD" --target rsdk -j4 >/dev/null || exit 1
fi

# 2) 编译测试
cc -g -O1 -fsanitize=address -fno-omit-frame-pointer -rdynamic \
   -I"$REC/include" "$HERE/odirect_concurrency_test.c" "$BUILD/lib/librsdk.a" \
   -lpthread -o "$BUILD/odirect_test" || exit 1

run(){ echo "########## mode=$1 ##########"
  RSDK_DIO_FORCE_FILE=1 IMG_MB="${IMG_MB:-128}" \
  ASAN_OPTIONS=abort_on_error=1:halt_on_error=1:detect_leaks=0 \
    "$BUILD/odirect_test" "$1" "$IMGDIR/odirect_test.img" 2>&1 | grep -vE '^\[' ;   # 单镜像复用, 省磁盘
  rm -f "$IMGDIR/odirect_test.img" "$IMGDIR/odirect_test.img.b"; }

MODES=("${@:-A Bfixed Shard Brace}")
for m in ${MODES[@]}; do run "$m"; done
