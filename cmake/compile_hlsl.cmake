function (add_hlsl_shader_compilation_target TARGET)
  set(OPTION_ARGS
    VSMAIN # whether to compile VSMain
    PSMAIN # whether to compile PSMain
  )
  set(ONE_VALUE_ARGS
    SHADER_PATH  # absolute path of the shader file
    OUTPUT_DIR   # where to output the compiled shaders
    DXC_PATH     # absolute path to dxc.exe exectuable
  )
  #
  # Just so you don't forget again whenever you have to write another bullshit
  # CMake function using this garbage syntax:
  # cmake_parse_arguments(<prefix> <options> <one-value-args> <multiple-value-args> <argn>)
  #   => this sets the following vars in current scope (use a macro for global
  #       scope):
  #       - <prefix>_<option-name> (ON or OFF)
  #       - <prefix>_<one-value-arg-name> (STRING)
  #       - <prefix>_<multiple-value-arg-name> (STRING/LIST)
  # Also, make sure to put everything in string quotes or you will lose your
  # remaining 2 brain cells dealing with this abonimation of a syntax ...
  #
  cmake_parse_arguments(HLSL "${OPTION_ARGS}" "${ONE_VALUE_ARGS}" "" ${ARGN})
  cmake_path(GET HLSL_SHADER_PATH STEM HLSL_SHADER_FILENAME)
  cmake_path(GET HLSL_SHADER_PATH ROOT_PATH HLSL_SHADER_DIRECTORY)

  # Some input sanitation so that you don't lose your shit when you forget a single keyword arg (or its value)
  if (NOT HLSL_SHADER_PATH OR
      NOT HLSL_OUTPUT_DIR OR
      NOT HLSL_DXC_PATH)
    message(FATAL_ERROR "Missing one or more keyword argument to add_hlsl_shader_compilation_target")
  endif()

  set (COMPILED_SHADERS "")
  if (HLSL_VSMAIN)
    set(HLSL_SHADER_VSMAIN_OUTPUT_PATH "${HLSL_OUTPUT_DIR}/${HLSL_SHADER_FILENAME}_VSMain.cso")
    message(STATUS "HLSL_SHADER_VSMAIN_OUTPUT_PATH=${HLSL_SHADER_VSMAIN_OUTPUT_PATH}")
    add_custom_command(
      OUTPUT  ${HLSL_SHADER_VSMAIN_OUTPUT_PATH}
      COMMAND ${HLSL_DXC_PATH}
          -nologo
          -T vs_6_0
          -E VSMain
          -Zi
          -Qembed_debug
          -Fo "${HLSL_SHADER_VSMAIN_OUTPUT_PATH}"
          "${HLSL_SHADER_PATH}"
      WORKING_DIRECTORY ${HLSL_SHADER_DIRECTORY}
      DEPENDS ${HLSL_SHADER_PATH}
      COMMENT "Compiling HLSL shader ${HLSL_SHADER_PATH} (VSMain)"
      VERBATIM
    )
    list(APPEND COMPILED_SHADERS ${HLSL_SHADER_VSMAIN_OUTPUT_PATH})
  endif()

  if (HLSL_PSMAIN)
    set(HLSL_SHADER_PSMAIN_OUTPUT_PATH "${HLSL_OUTPUT_DIR}/${HLSL_SHADER_FILENAME}_PSMain.cso")
    message(STATUS "HLSL_SHADER_PSMAIN_OUTPUT_PATH=${HLSL_SHADER_PSMAIN_OUTPUT_PATH}")
    add_custom_command(
      OUTPUT  ${HLSL_SHADER_PSMAIN_OUTPUT_PATH}
      COMMAND ${HLSL_DXC_PATH}
          -nologo
          -T ps_6_0
          -E PSMain
          -Zi -Qembed_debug
          -Fo "${HLSL_SHADER_PSMAIN_OUTPUT_PATH}"
          "${HLSL_SHADER_PATH}"
      WORKING_DIRECTORY ${HLSL_SHADER_DIRECTORY}
      DEPENDS ${HLSL_SHADER_PATH}
      COMMENT "Compiling HLSL shader ${HLSL_SHADER_PATH} (PSMain)"
      VERBATIM
    )
    list(APPEND COMPILED_SHADERS ${HLSL_SHADER_PSMAIN_OUTPUT_PATH})
  endif()

  if (COMPILED_SHADERS)
    add_custom_target(${TARGET} DEPENDS ${COMPILED_SHADERS})
  else()
    message(FATAL_ERROR "add_hlsl_shader_compilation_target() has to be called with at least VSMAIN or PSMAIN set.")
  endif()
endfunction()
