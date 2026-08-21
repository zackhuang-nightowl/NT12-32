#! /bin/sh

CUR=$PWD

if [ ! -f $CUR/openssl/lib/linux/libssl.so ]; then 
ln -s $CUR/openssl/lib/linux/libssl.so.1.1 $CUR/openssl/lib/linux/libssl.so
fi

if [ ! -f $CUR/openssl/lib/linux/libcrypto.so ]; then 
ln -s $CUR/openssl/lib/linux/libcrypto.so.1.1 $CUR/openssl/lib/linux/libcrypto.so
fi

echo "make symbolic link finish!"
