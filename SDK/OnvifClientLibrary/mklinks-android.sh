#! /bin/sh

CUR=$PWD

if [ ! -f $CUR/openssl/lib/armeabi-v7a/libssl.so ]; then 
ln -s $CUR/openssl/lib/armeabi-v7a/libssl.so.1.1 $CUR/openssl/lib/armeabi-v7a/libssl.so
fi

if [ ! -f $CUR/openssl/lib/armeabi-v7a/libcrypto.so ]; then 
ln -s $CUR/openssl/lib/armeabi-v7a/libcrypto.so.1.1 $CUR/openssl/lib/armeabi-v7a/libcrypto.so
fi

if [ ! -f $CUR/openssl/lib/arm64-v8a/libssl.so ]; then 
ln -s $CUR/openssl/lib/arm64-v8a/libssl.so.1.1 $CUR/openssl/lib/arm64-v8a/libssl.so
fi

if [ ! -f $CUR/openssl/lib/arm64-v8a/libcrypto.so ]; then 
ln -s $CUR/openssl/lib/arm64-v8a/libcrypto.so.1.1 $CUR/openssl/lib/arm64-v8a/libcrypto.so
fi

echo "make symbolic link finish!"
