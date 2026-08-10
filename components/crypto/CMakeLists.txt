# components/crypto — MD5/SHA/AES256 封装（OpenSSL libcrypto）+ 口令派生占位。计划 §B1.2。
cmake_minimum_required(VERSION 3.13)
project(nvr_crypto C)

set(CMAKE_C_STANDARD 11)

add_library(nvr_crypto STATIC src/nvr_crypto.c)
target_include_directories(nvr_crypto PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# OpenSSL libcrypto：主机用系统；交叉用工程内 vendored 头(1.1.1g) + TUTK aarch64 libcrypto.so
if(NVR_CROSS)
  target_include_directories(nvr_crypto PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/openssl/include)
  target_link_libraries(nvr_crypto PUBLIC ${NVR_TUTK_AARCH64_LIB}/libcrypto.so)
  message(STATUS "nvr_crypto: 交叉=vendored openssl 头 + TUTK aarch64 libcrypto")
else()
  find_package(OpenSSL)
  if(OpenSSL_FOUND)
    target_link_libraries(nvr_crypto PUBLIC OpenSSL::Crypto)
  else()
    find_library(CRYPTO_LIB crypto)
    if(CRYPTO_LIB)
      target_link_libraries(nvr_crypto PUBLIC ${CRYPTO_LIB})
    else()
      message(FATAL_ERROR "libcrypto not found (需要 libssl-dev)")
    endif()
  endif()
endif()

