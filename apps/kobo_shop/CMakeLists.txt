cmake_minimum_required(VERSION 3.16)
project(kobo_shop C)

set(CMAKE_C_STANDARD 99)

add_executable(kobo_shop.app
  src/main.c
  src/http.c
  src/config.c
  src/kobo.c
  src/adept.c
  third_party/cJSON.c
)

target_include_directories(kobo_shop.app PRIVATE
  ${PB_INCLUDE_DIRECTORIES}
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

target_link_directories(kobo_shop.app PRIVATE
  ${PB_LINK_DIRECTORIES}
)

target_link_libraries(kobo_shop.app PRIVATE
  inkview
  curl
  ssl
  crypto
  z
)

# PocketBook apps often need pthread + dl with curl
find_library(PTHREAD_LIB pthread)
if(PTHREAD_LIB)
  target_link_libraries(kobo_shop.app PRIVATE ${PTHREAD_LIB})
endif()
