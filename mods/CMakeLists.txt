# compile all .c files in this folder to shared libraries

file(GLOB SOURCES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}  "*.c")
foreach(source ${SOURCES})
    get_filename_component(name ${source} NAME_WE)
    add_library(${name} SHARED ${source})
endforeach()

# compile all .cpp files in this folder to shared libraries

file(GLOB SOURCES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "*.cpp")
foreach(source ${SOURCES})
    get_filename_component(name ${source} NAME_WE)
    add_library(${name} SHARED ${source})
    set_target_properties(${name} PROPERTIES LINKER_LANGUAGE CXX)
endforeach()


# compile every folder into a single shared library, with the folder name

file(GLOB CHILDREN RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} *)
foreach(child ${CHILDREN})
    if(IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${child})
        # ignore this if it contains a CMakeLists.txt file
        if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${child}/CMakeLists.txt)
            add_subdirectory(${child})
            continue()
        endif()    

        file(GLOB SOURCES "${child}/*.c")
        if(SOURCES)
            include_directories(${child})
            add_library(${child} SHARED ${SOURCES})
        endif()
    endif()
endforeach()