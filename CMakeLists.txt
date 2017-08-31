# @author Michal Karm Babacek <karm@fedoraproject.org>
project(mod_cloud_storage)
if(WIN32)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /O2 /Wall /Z7")
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} /Od /Wall /Zi")
else()
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -O2 -std=c99 -fPIC -Wall -Wextra")
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -g -p -O1 -std=gnu99 -fPIC -Wall -Wextra")
endif()
cmake_minimum_required(VERSION 2.8)
set(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake/modules ${CMAKE_MODULE_PATH})

find_package(CURL REQUIRED)
find_package(APR REQUIRED)
find_package(OpenSSL REQUIRED)

include_directories(${CURL_INCLUDE_DIR})
include_directories(${APR_INCLUDE_DIR})
include_directories(${APRUTIL_INCLUDE_DIR})
include_directories(${OPENSSL_INCLUDE_DIR})

aux_source_directory(src SRC_LIST)
add_executable(${PROJECT_NAME} ${SRC_LIST})
target_link_libraries(${PROJECT_NAME} ${CURL_LIBRARIES} ${APR_LIBRARIES} ${APRUTIL_LIBRARIES} ${OPENSSL_CRYPTO_LIBRARY})
