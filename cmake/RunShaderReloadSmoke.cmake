# Hot reload smoke는 제품 shader를 건드리지 않고 build 하위 복사본에서 실행한다.
foreach(required VCLOUD_EXE VCLOUD_SHADER_SOURCE VCLOUD_WORK_ROOT VCLOUD_MODE)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "Missing -D${required}")
    endif()
endforeach()

file(REMOVE_RECURSE "${VCLOUD_WORK_ROOT}")
file(MAKE_DIRECTORY "${VCLOUD_WORK_ROOT}")
file(COPY "${VCLOUD_SHADER_SOURCE}/" DESTINATION "${VCLOUD_WORK_ROOT}")
file(WRITE "${VCLOUD_WORK_ROOT}/.vcloud-shader-smoke-root"
    "Temporary copy owned by RunShaderReloadSmoke.cmake\n")

if(VCLOUD_MODE STREQUAL "tone")
    set(smoke_argument "--hot-reload-smoke-test")
    set(smoke_timeout 30)
elseif(VCLOUD_MODE STREQUAL "noise")
    set(smoke_argument "--hot-reload-dependency-smoke-test")
    set(smoke_timeout 60)
elseif(VCLOUD_MODE STREQUAL "weather")
    set(smoke_argument "--weather-hot-reload-smoke-test")
    set(smoke_timeout 30)
else()
    message(FATAL_ERROR "Unknown VCLOUD_MODE=${VCLOUD_MODE}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        "VCLOUD_SHADER_OVERRIDE_DIR=${VCLOUD_WORK_ROOT}"
        "${VCLOUD_EXE}" "${smoke_argument}"
    RESULT_VARIABLE smoke_result
    OUTPUT_VARIABLE smoke_output
    ERROR_VARIABLE smoke_error
    TIMEOUT ${smoke_timeout})

if(NOT smoke_output STREQUAL "")
    string(STRIP "${smoke_output}" smoke_output)
    message(STATUS "${smoke_output}")
endif()
if(NOT smoke_error STREQUAL "")
    string(STRIP "${smoke_error}" smoke_error)
    message(STATUS "${smoke_error}")
endif()
if(NOT smoke_result EQUAL 0)
    message(FATAL_ERROR
        "Shader reload ${VCLOUD_MODE} smoke failed (${smoke_result})")
endif()
