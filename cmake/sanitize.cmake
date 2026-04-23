if (SANITIZER_TYPE STREQUAL "thread")
    # -Wno-tsan: GCC 13+ warns on atomic_thread_fence which TSAN cannot fully intercept;
    # suppress to avoid -Werror failures on a known GCC/TSAN limitation.
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=thread -g -O1 -fno-omit-frame-pointer -Wno-tsan")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=thread -g -O1 -fno-omit-frame-pointer -Wno-tsan")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=thread")
    message(STATUS "********* WARNING: Running with Thread Sanitizer ON *********")
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -fsanitize=undefined -fsanitize-address-use-after-scope -fno-sanitize=alignment -DCDS_ADDRESS_SANITIZER_ENABLED -fno-omit-frame-pointer -fno-optimize-sibling-calls")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address,undefined -fsanitize-address-use-after-scope -DCDS_ADDRESS_SANITIZER_ENABLED -fno-omit-frame-pointer -fno-optimize-sibling-calls")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address -fsanitize=undefined")
    message(STATUS "********* WARNING: Running with Address + UB Sanitizer ON *********")
endif()
