# Collect source files from src/ and lib/
FILE(GLOB_RECURSE app_sources
    ${CMAKE_SOURCE_DIR}/src/*.*
    ${CMAKE_SOURCE_DIR}/lib/*/*.*
)

# Collect lib include directories
FILE(GLOB lib_subdirs LIST_DIRECTORIES true ${CMAKE_SOURCE_DIR}/lib/*)
set(lib_include_dirs "")
foreach(dir ${lib_subdirs})
    if(IS_DIRECTORY ${dir})
        list(APPEND lib_include_dirs ${dir})
    endif()
endforeach()

idf_component_register(SRCS ${app_sources}
                       INCLUDE_DIRS
                           ${CMAKE_SOURCE_DIR}/src
                           ${lib_include_dirs}
                       REQUIRES spiffs esp_timer log)
