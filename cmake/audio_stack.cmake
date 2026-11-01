# Codec calls and nested USB IRQs exceed the SDK's 2 KB core-0 stack.
# Reserve 16 KB at the top of main RAM; the 4 KB scratch bank cannot hold it.
set(audio_stack_bytes 16384)
set(audio_linker_source "${PICO_SDK_PATH}/src/rp2_common/pico_crt0/rp2350/memmap_default.ld")
file(READ "${audio_linker_source}" audio_linker_content)
set(audio_stack_section "    .stack_dummy (NOLOAD):\n    {\n        KEEP(*(.stack*))\n    } > SCRATCH_Y")
string(FIND "${audio_linker_content}" "${audio_stack_section}" audio_stack_pos)
if(audio_stack_pos EQUAL -1)
    message(FATAL_ERROR "Pico SDK linker layout changed; cannot reserve the audio stack")
endif()
string(REPLACE "${audio_stack_section}"
    "    .stack_dummy (ORIGIN(RAM) + LENGTH(RAM) - ${audio_stack_bytes}) (NOLOAD):\n    {\n        KEEP(*(.stack*))\n    } > RAM"
    audio_linker_content "${audio_linker_content}")
foreach(audio_limit __HeapLimit __StackLimit)
    string(REPLACE "${audio_limit} = ORIGIN(RAM) + LENGTH(RAM);"
        "${audio_limit} = ORIGIN(RAM) + LENGTH(RAM) - ${audio_stack_bytes};"
        audio_linker_content "${audio_linker_content}")
endforeach()
string(REPLACE "__StackTop = ORIGIN(SCRATCH_Y) + LENGTH(SCRATCH_Y);"
    "__StackTop = ORIGIN(RAM) + LENGTH(RAM);" audio_linker_content "${audio_linker_content}")
string(REPLACE "ASSERT(__StackLimit >= __HeapLimit, \"region RAM overflowed\")"
    "ASSERT(__end__ <= __HeapLimit, \"audio data overlaps reserved stack\")"
    audio_linker_content "${audio_linker_content}")
set(audio_linker_output "${CMAKE_BINARY_DIR}/audio_memmap.ld")
file(WRITE "${audio_linker_output}" "${audio_linker_content}")
pico_set_linker_script(${PROJECT_NAME} "${audio_linker_output}")
target_compile_definitions(${PROJECT_NAME} PRIVATE
    PICO_STACK_SIZE=${audio_stack_bytes} PICO_CORE1_STACK_SIZE=2048)
target_compile_options(${PROJECT_NAME} PRIVATE -fstack-usage)
target_compile_options(fdk-aac PRIVATE -fstack-usage)
