cmake_minimum_required(VERSION 3.16)
project(hello_kobo C)

add_executable(hello_kobo.app src/main.c)

target_include_directories(hello_kobo.app PRIVATE
  ${PB_INCLUDE_DIRECTORIES}
)

target_link_directories(hello_kobo.app PRIVATE
  ${PB_LINK_DIRECTORIES}
)

target_link_libraries(hello_kobo.app PRIVATE inkview)
