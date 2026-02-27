cmake_minimum_required(VERSION 3.22.1)
project("pagecurl")
set(CMAKE_CXX_STANDARD 17)

add_library(pagecurl SHARED
    page_curl_renderer.cpp
    page_curl_jni.cpp
)

target_link_libraries(pagecurl
    PRIVATE android log EGL GLESv3 jnigraphics
)
