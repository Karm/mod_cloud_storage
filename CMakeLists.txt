# @author Michal Karm Babacek <karm@fedoraproject.org>
project(mod_cloud_storage)
cmake_minimum_required(VERSION 3.2)
if(WIN32)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /O2 /Wall /Z7")
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} /Od /Wall /Zi")
else()
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -O2 -std=c99 -fPIC -Wall -Wextra -pedantic")
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -g -p -O1 -std=gnu99 -fPIC -Wall -Wextra -pedantic")
endif()
set(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake/modules ${CMAKE_MODULE_PATH})

set(VERSION 0.4)

find_package(CURL REQUIRED)
find_package(APR REQUIRED)
find_package(OpenSSL REQUIRED)

include_directories(${CURL_INCLUDE_DIR})
include_directories(${APR_INCLUDE_DIR})
include_directories(${APRUTIL_INCLUDE_DIR})
include_directories(${OPENSSL_INCLUDE_DIR})

aux_source_directory(src SRC_LIST)
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR}/bin)
add_executable(${PROJECT_NAME} ${SRC_LIST})
target_link_libraries(${PROJECT_NAME} ${CURL_LIBRARIES} ${APR_LIBRARIES} ${APRUTIL_LIBRARIES} ${OPENSSL_CRYPTO_LIBRARY})

# Invoke target make release_zip
file(COPY ${CMAKE_SOURCE_DIR}/README.md DESTINATION ${CMAKE_BINARY_DIR})
file(COPY ${CMAKE_SOURCE_DIR}/LICENSE DESTINATION ${CMAKE_BINARY_DIR})
set(RELEASE_ZIP_NAME "archive-${VERSION}.zip")
add_custom_target(release_zip COMMAND
    ${CMAKE_COMMAND} -E tar "cfv" "${RELEASE_ZIP_NAME}" --format=zip
    "${CMAKE_BINARY_DIR}/bin"
    "${CMAKE_BINARY_DIR}/README.md"
    "${CMAKE_BINARY_DIR}/LICENSE")
#file(SHA1 "${CMAKE_BINARY_DIR}/${RELEASE_ZIP_NAME}" RELEASE_FILE_HASH)
#file(WRITE "archive-${VERSION}.sha1" ${RELEASE_FILE_HASH})
