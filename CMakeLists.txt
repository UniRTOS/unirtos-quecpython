set(_qpy_expected_target "unirtos-quecpython")

if(NOT DEFINED UNIRTOS_EXTERNAL_LIBRARY_NAME OR "${UNIRTOS_EXTERNAL_LIBRARY_NAME}" STREQUAL "")
    message(FATAL_ERROR
        "unirtos-quecpython must be loaded by the UniRTOS external-library loader; "
        "UNIRTOS_EXTERNAL_LIBRARY_NAME is not set")
endif()

if(NOT "${UNIRTOS_EXTERNAL_LIBRARY_NAME}" STREQUAL "${_qpy_expected_target}")
    message(FATAL_ERROR
        "unirtos-quecpython target mismatch: expected '${_qpy_expected_target}', "
        "got '${UNIRTOS_EXTERNAL_LIBRARY_NAME}'")
endif()

set(target "${UNIRTOS_EXTERNAL_LIBRARY_NAME}")

set(MICROPY_DIR ${CMAKE_CURRENT_LIST_DIR}/third_party/micropython)
set(MICROPY_PORT_DIR ${CMAKE_CURRENT_LIST_DIR}/ports/quectel/src)
set(MICROPY_BOARD quectel)
set(MICROPY_QSTRDEFS_PORT ${MICROPY_PORT_DIR}/qstrdefsport.h)
set(MICROPY_QSTR_PREPROCESS_SCRIPT ${MICROPY_PORT_DIR}/qpy_preprocess_qstr.py)
set(MICROPY_LIB_DIR ${CMAKE_CURRENT_LIST_DIR}/ports/quectel/lib)

set(MICROPY_MPYCROSS ${CMAKE_COMMAND})
set(ENV{MICROPY_MPYCROSS} ${CMAKE_COMMAND})

if(NOT DEFINED build_project OR "${build_project}" STREQUAL "")
    message(FATAL_ERROR "unirtos-quecpython requires build.module/build_project; supported value: EG800ZCN_LA")
endif()
if(NOT "${build_project}" STREQUAL "EG800ZCN_LA")
    message(FATAL_ERROR
        "unirtos-quecpython v1.1.0 supports only EG800ZCN_LA; requested '${build_project}'")
endif()
string(MAKE_C_IDENTIFIER "${build_project}" QPY_BOARD_ID)
set(QPY_BOARD_CONFIG_DIR "${CMAKE_CURRENT_LIST_DIR}/boards/${build_project}")
set(QPY_BOARD_CONFIG_H "${QPY_BOARD_CONFIG_DIR}/mpconfigboard.h")
if(NOT EXISTS "${QPY_BOARD_CONFIG_H}")
    message(FATAL_ERROR "QuecPython board config not found: ${QPY_BOARD_CONFIG_H}")
endif()

# The QuecPython runtime uses a dedicated 0x48000-byte CUST partition.  SDK
# 1.0.5 decides whether to package that partition before this external
# library is loaded, so the component cannot enable it by setting the SDK's
# BINPKG_CUS_APP2_ENABLED variable.  Attach a post-build step to the SDK's
# firmware target instead: it regenerates the same package with the board
# image added as PKGFLX_CUST, then the SDK's normal unpack/copy targets run.
set(QPY_CUSTOMER_APP2_IMAGE "${QPY_BOARD_CONFIG_DIR}/customer_app2.bin")
set(QPY_CUSTOMER_APP2_SIZE 294912)
if(NOT EXISTS "${QPY_CUSTOMER_APP2_IMAGE}")
    message(FATAL_ERROR "QuecPython customer partition image not found: ${QPY_CUSTOMER_APP2_IMAGE}")
endif()
file(SIZE "${QPY_CUSTOMER_APP2_IMAGE}" _qpy_customer_app2_actual_size)
if(NOT _qpy_customer_app2_actual_size EQUAL QPY_CUSTOMER_APP2_SIZE)
    message(FATAL_ERROR
        "QuecPython customer partition image must be ${QPY_CUSTOMER_APP2_SIZE} bytes; "
        "got ${_qpy_customer_app2_actual_size}: ${QPY_CUSTOMER_APP2_IMAGE}")
endif()
set_property(GLOBAL PROPERTY qpy_customer_app2_image "${QPY_CUSTOMER_APP2_IMAGE}")

function(qpy_attach_customer_app2_packaging)
    get_property(_qpy_customer_image GLOBAL PROPERTY qpy_customer_app2_image)
    if(NOT EXISTS "${_qpy_customer_image}")
        message(FATAL_ERROR "QuecPython customer partition image disappeared: ${_qpy_customer_image}")
    endif()
    if(NOT TARGET firmware OR NOT TARGET copy_binpkg)
        message(FATAL_ERROR
            "UniRTOS SDK 1.0.5 firmware targets are unavailable; "
            "cannot package QuecPython customer_app2.bin")
    endif()
    if(SECBOOT_SIGN)
        message(FATAL_ERROR
            "unirtos-quecpython v1.1.0 customer partition packaging does not support SECBOOT_SIGN")
    endif()
    if(BINPKG_EXT_FS_ENABLED OR BINPKG_EXT_UPDATER_ENABLED)
        message(FATAL_ERROR
            "unirtos-quecpython customer partition conflicts with an SDK external FS/updater package")
    endif()

    foreach(_qpy_required_var IN ITEMS
            FCELF_TOOL BOOTLOADER_BIN KERNEL_BIN CP_BIN AP_BIN UNIR_UPDATER
            EC718_PRMGRCFG EC718_FORMAT EC718_PRD_UART EC718_PRD_USB
            MEM_MAP OUTPUT_BINPKG BINARY_BIN_DIR OUTPUT_RELEASE_DIR build_version chip_type)
        if(NOT DEFINED ${_qpy_required_var} OR "${${_qpy_required_var}}" STREQUAL "")
            message(FATAL_ERROR
                "UniRTOS SDK packaging variable is unavailable: ${_qpy_required_var}")
        endif()
    endforeach()

    file(STRINGS "${MEM_MAP}" _qpy_opencpu_partition_lines
        REGEX "UNIR_APP_OPEN_CPU_SIZE[ \\t]+\\(0x90000\\)")
    file(STRINGS "${MEM_MAP}" _qpy_cust_partition_lines
        REGEX "UNIRTOS_CUST_FLASH_SIZE[ \\t]+\\(0x48000\\)")
    file(STRINGS "${MEM_MAP}" _qpy_lfs_partition_lines
        REGEX "UNIR_LFS_SIZE[ \\t]+\\(0x79000\\)")
    if(NOT _qpy_opencpu_partition_lines OR NOT _qpy_cust_partition_lines OR NOT _qpy_lfs_partition_lines)
        message(FATAL_ERROR
            "The selected gccout does not declare the required EG800ZCN_LA CUST layout "
            "(OpenCPU 0x90000 / CUST 0x48000 / LFS 0x79000): ${MEM_MAP}")
    endif()

    add_custom_command(TARGET firmware POST_BUILD
        COMMAND "${FCELF_TOOL}" -M
                -input "${BOOTLOADER_BIN}" -addrname BL_PKGIMG_LNA -flashsize BOOTLOADER_PKGIMG_LIMIT_SIZE
                -input "${KERNEL_BIN}" -addrname AP_PKGIMG_LNA -flashsize AP_PKGIMG_LIMIT_SIZE
                -input "${CP_BIN}" -addrname CP_PKGIMG_LNA -flashsize CP_PKGIMG_LIMIT_SIZE
                -input "${AP_BIN}" -addrname PKGFLX_APP_PKGIMG_LNA -flashsize PKGFLX_APP_PKGIMG_LIMIT_SIZE
                -input "${UNIR_UPDATER}" -addrname PKGFLX_UPDATER_PKGIMG_LNA -flashsize PKGFLX_UPDATER_PKGIMG_LIMIT_SIZE
                -input "${EC718_PRMGRCFG}" -addrname XPKGDPRM_VIRTIMG_MERGE_LNA -flashsize XPKG_VIRTIMG_LOAD_SIZE
                -input "${EC718_FORMAT}" -addrname XPKGDCMN_VIRTIMG_MERGE_LNA -flashsize XPKG_VIRTIMG_LOAD_SIZE
                -input "${EC718_PRD_UART}" -addrname XPKGDCMN_VIRTIMG_MERGE_LNA -flashsize XPKG_VIRTIMG_LOAD_SIZE
                -input "${EC718_PRD_USB}" -addrname XPKGDCMN_VIRTIMG_MERGE_LNA -flashsize XPKG_VIRTIMG_LOAD_SIZE
                -input "${_qpy_customer_image}" -addrname PKGFLX_CUST_PKGIMG_LNA -flashsize PKGFLX_CUST_PKGIMG_LIMIT_SIZE
                -pkgmode 1
                -banoldtool 1
                -productname "${chip_type}"
                -def "${MEM_MAP}"
                -outfile "${OUTPUT_BINPKG}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${_qpy_customer_image}" "${BINARY_BIN_DIR}/customer_app2.bin"
        COMMENT "Repackaging firmware with QuecPython customer_app2.bin"
        VERBATIM
    )

    add_custom_command(TARGET copy_binpkg POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${_qpy_customer_image}"
                "${OUTPUT_RELEASE_DIR}/${build_version}/customer_app2.bin"
        COMMENT "Copying QuecPython customer_app2.bin to release directory"
        VERBATIM
    )

    message(STATUS
        "[unirtos-quecpython] customer partition packaging enabled: ${_qpy_customer_image}")
endfunction()

cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}"
    CALL qpy_attach_customer_app2_packaging)

function(qpy_board_feature_enabled out_var macro_name)
    file(STRINGS "${QPY_BOARD_CONFIG_H}" _qpy_feature_lines
        REGEX "^[ \t]*#[ \t]*define[ \t]+${macro_name}([ \t]+.*)?$")
    if(NOT _qpy_feature_lines)
        set(${out_var} FALSE PARENT_SCOPE)
        return()
    endif()
    list(GET _qpy_feature_lines 0 _qpy_feature_line)
    if(_qpy_feature_line MATCHES "^[ \t]*#[ \t]*define[ \t]+${macro_name}([ \t]+)?$")
        set(${out_var} TRUE PARENT_SCOPE)
    elseif(_qpy_feature_line MATCHES "^[ \t]*#[ \t]*define[ \t]+${macro_name}[ \t]+\\(?0\\)?([ \t]*($|//|/\\*))")
        set(${out_var} FALSE PARENT_SCOPE)
    else()
        set(${out_var} TRUE PARENT_SCOPE)
    endif()
endfunction()

function(qpy_add_source_if_feature source_file macro_name)
    qpy_board_feature_enabled(_qpy_enabled "${macro_name}")
    if(_qpy_enabled)
        if(NOT EXISTS "${source_file}")
            message(FATAL_ERROR "${macro_name} is enabled but source is missing: ${source_file}")
        endif()
        list(APPEND QPY_PORT_SOURCES "${source_file}")
        set(QPY_PORT_SOURCES "${QPY_PORT_SOURCES}" PARENT_SCOPE)
    endif()
endfunction()

function(qpy_add_upstream_source_if_feature source_file macro_name)
    qpy_board_feature_enabled(_qpy_enabled "${macro_name}")
    if(_qpy_enabled)
        list(APPEND QPY_UPSTREAM_SOURCES "${source_file}")
        set(QPY_UPSTREAM_SOURCES "${QPY_UPSTREAM_SOURCES}" PARENT_SCOPE)
    endif()
endfunction()

function(qpy_remove_micropy_source_if_disabled source_file macro_name)
    qpy_board_feature_enabled(_qpy_enabled "${macro_name}")
    if(NOT _qpy_enabled)
        list(REMOVE_ITEM QPY_MICROPY_SOURCE_PY "${source_file}")
        set(QPY_MICROPY_SOURCE_PY "${QPY_MICROPY_SOURCE_PY}" PARENT_SCOPE)
    endif()
endfunction()

function(qpy_freeze_module_if_feature module_file macro_name)
    qpy_board_feature_enabled(_qpy_enabled "${macro_name}")
    if(_qpy_enabled)
        configure_file("${MICROPY_PORT_DIR}/modules/${module_file}" "${QPY_FROZEN_MODULE_DIR}/${module_file}" COPYONLY)
    endif()
endfunction()

# Keep feature-to-file mappings in one declarative manifest.  The helper
# macros intentionally remain macros: qpy_add_*_source_if_feature updates the
# caller's source list through PARENT_SCOPE.
include("${CMAKE_CURRENT_LIST_DIR}/cmake/QuecPythonFeatures.cmake")

macro(qpy_freeze_feature_list feature_list)
    foreach(_qpy_feature IN LISTS ${feature_list})
        string(REPLACE "|" ";" _qpy_feature_pair "${_qpy_feature}")
        list(GET _qpy_feature_pair 0 _qpy_module_file)
        list(GET _qpy_feature_pair 1 _qpy_feature_macro)
        qpy_freeze_module_if_feature("${_qpy_module_file}" "${_qpy_feature_macro}")
    endforeach()
endmacro()

macro(qpy_add_port_feature_list feature_list)
    foreach(_qpy_feature IN LISTS ${feature_list})
        string(REPLACE "|" ";" _qpy_feature_pair "${_qpy_feature}")
        list(GET _qpy_feature_pair 0 _qpy_source_file)
        list(GET _qpy_feature_pair 1 _qpy_feature_macro)
        qpy_add_source_if_feature("${MICROPY_PORT_DIR}/${_qpy_source_file}" "${_qpy_feature_macro}")
    endforeach()
endmacro()

macro(qpy_add_upstream_feature_list feature_list)
    foreach(_qpy_feature IN LISTS ${feature_list})
        string(REPLACE "|" ";" _qpy_feature_pair "${_qpy_feature}")
        list(GET _qpy_feature_pair 0 _qpy_source_file)
        list(GET _qpy_feature_pair 1 _qpy_feature_macro)
        qpy_add_upstream_source_if_feature("${QPY_BUILD_UPSTREAM_DIR}/${_qpy_source_file}" "${_qpy_feature_macro}")
    endforeach()
endmacro()

set(QPY_FROZEN_MODULE_DIR "${CMAKE_BINARY_DIR}/qpy_frozen_modules")
file(REMOVE_RECURSE "${QPY_FROZEN_MODULE_DIR}")
file(MAKE_DIRECTORY "${QPY_FROZEN_MODULE_DIR}")
qpy_freeze_feature_list(QPY_FROZEN_MODULE_FEATURES)

include(${MICROPY_DIR}/py/py.cmake)

set(QPY_BUILD_UPSTREAM_DIR ${CMAKE_BINARY_DIR}/qpy_upstream)
file(MAKE_DIRECTORY ${QPY_BUILD_UPSTREAM_DIR})
file(COPY
    ${MICROPY_DIR}/py
    ${MICROPY_DIR}/extmod
    ${MICROPY_DIR}/shared
    DESTINATION ${QPY_BUILD_UPSTREAM_DIR}
)

set(QPY_MICROPY_SOURCE_PY)
foreach(_qpy_src IN LISTS MICROPY_SOURCE_PY)
    string(REPLACE "${MICROPY_DIR}/" "${QPY_BUILD_UPSTREAM_DIR}/" _qpy_src_short "${_qpy_src}")
    list(APPEND QPY_MICROPY_SOURCE_PY "${_qpy_src_short}")
endforeach()
qpy_remove_micropy_source_if_disabled(${QPY_BUILD_UPSTREAM_DIR}/py/modcollections.c MICROPY_QPY_MODULE_UCOLLECTIONS)
qpy_remove_micropy_source_if_disabled(${QPY_BUILD_UPSTREAM_DIR}/py/moderrno.c MICROPY_QPY_MODULE_UERRNO)
qpy_remove_micropy_source_if_disabled(${QPY_BUILD_UPSTREAM_DIR}/py/modmath.c MICROPY_QPY_MODULE_MATH)
qpy_remove_micropy_source_if_disabled(${QPY_BUILD_UPSTREAM_DIR}/py/modstruct.c MICROPY_QPY_MODULE_USTRUCT)

add_library(${target} STATIC)
add_library(quecpython ALIAS ${target})

target_include_directories(${target} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>
)

target_include_directories(${target} PRIVATE
    ${SOURCE_TOP_DIR}
    ${SOURCE_TOP_DIR}/qos_components/components/include
    ${SOURCE_TOP_DIR}/qos_components/system/at
    ${SOURCE_TOP_DIR}/qos_components/system/debug
    ${SOURCE_TOP_DIR}/qos_components/system/dev
    ${SOURCE_TOP_DIR}/qos_components/system/hal
    ${SOURCE_TOP_DIR}/qos_components/system/fota
    ${SOURCE_TOP_DIR}/qos_components/system/include
    ${SOURCE_TOP_DIR}/qos_components/system/modem/include
    ${SOURCE_TOP_DIR}/qos_components/system/os
    ${SOURCE_TOP_DIR}/qos_components/system/socket
    ${SOURCE_TOP_DIR}/qos_components/system/public_comp/link_list
    ${SOURCE_TOP_DIR}/qos_components/system/public_comp/event_notify
    ${SOURCE_TOP_DIR}/qos_components/system/vfs
    ${SOURCE_TOP_DIR}/qos_components/components/qurl/include
    ${SOURCE_TOP_DIR}/qos_kernel/eigen_718/arch_prj/ql_common/unirtos_adapter/dynamic_create
    ${SOURCE_TOP_DIR}/qos_kernel/eigen_718/arch_prj/ql_common/unirtos_adapter/system_source/plat_source/vfs
    ${SOURCE_TOP_DIR}/qos_kernel/eigen_718/arch_prj/ql_common/middleware/quectel/cust/inc
    ${SOURCE_TOP_DIR}/qos_applications/app_init
    ${CMAKE_CURRENT_LIST_DIR}
    ${MICROPY_LIB_DIR}/littlefs
    ${MICROPY_DIR}
    ${MICROPY_PORT_DIR}
    ${QPY_BOARD_CONFIG_DIR}
    ${CMAKE_BINARY_DIR}
    ${CMAKE_BINARY_DIR}/genhdr
)

target_compile_definitions(${target} PRIVATE
    QPY_BOARD_${QPY_BOARD_ID}=1
    LFS_NO_DEBUG
    LFS_NO_WARN
    LFS_NO_ERROR
    LFS_NO_ASSERT
)

target_compile_options(${target} PRIVATE
    -mcpu=cortex-m3
    -mthumb
)

set(QPY_PORT_SOURCES
    ${MICROPY_PORT_DIR}/quecpython.c
    ${MICROPY_PORT_DIR}/mphalport.c
    ${MICROPY_PORT_DIR}/mpthreadport.c
    ${MICROPY_PORT_DIR}/qpy_compat_common.c
    ${MICROPY_PORT_DIR}/qpy_path.c
    ${MICROPY_PORT_DIR}/qpy_usrfs.c
    ${MICROPY_LIB_DIR}/littlefs/lfs.c
    ${MICROPY_LIB_DIR}/littlefs/lfs_util.c
    ${MICROPY_PORT_DIR}/qpy_file.c
)
qpy_add_port_feature_list(QPY_PORT_FEATURE_SOURCES)
qpy_board_feature_enabled(QPY_HAS_MACHINE MICROPY_QPY_MODULE_MACHINE)
if(QPY_HAS_MACHINE)
    list(APPEND QPY_PORT_SOURCES ${MICROPY_PORT_DIR}/qpy_gpio_map.c)
    qpy_add_port_feature_list(QPY_MACHINE_FEATURE_SOURCES)
endif()
qpy_board_feature_enabled(QPY_HAS_MISC MICROPY_QPY_MODULE_MISC)
if(QPY_HAS_MISC)
    qpy_add_port_feature_list(QPY_MISC_FEATURE_SOURCES)
endif()

file(TO_CMAKE_PATH "${QPY_FROZEN_MODULE_DIR}" QPY_FROZEN_MODULE_DIR_CMAKE)
set(QPY_GENERATED_MANIFEST "${CMAKE_BINARY_DIR}/qpy_manifest.py")
file(WRITE "${QPY_GENERATED_MANIFEST}" "freeze_as_str(\"${QPY_FROZEN_MODULE_DIR_CMAKE}\")\n")
set(MICROPY_FROZEN_MANIFEST "${QPY_GENERATED_MANIFEST}")

set(QPY_UPSTREAM_SOURCES
    ${QPY_MICROPY_SOURCE_PY}
    ${QPY_BUILD_UPSTREAM_DIR}/shared/readline/readline.c
    ${QPY_BUILD_UPSTREAM_DIR}/shared/runtime/interrupt_char.c
    ${QPY_BUILD_UPSTREAM_DIR}/shared/runtime/pyexec.c
    ${QPY_BUILD_UPSTREAM_DIR}/shared/runtime/sys_stdio_mphal.c
    ${QPY_BUILD_UPSTREAM_DIR}/shared/timeutils/timeutils.c
)
qpy_add_upstream_feature_list(QPY_UPSTREAM_FEATURE_SOURCES)

set(QPY_OBJ_DIRS)
foreach(_qpy_src IN LISTS QPY_UPSTREAM_SOURCES QPY_PORT_SOURCES)
    if(IS_ABSOLUTE "${_qpy_src}")
        get_filename_component(_qpy_src_real "${_qpy_src}" REALPATH)
        string(REGEX REPLACE "^([A-Za-z]):/" "\\1_/" _qpy_obj_rel "${_qpy_src_real}")
        get_filename_component(_qpy_obj_dir "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/${target}.dir/${_qpy_obj_rel}.obj" DIRECTORY)
        list(APPEND QPY_OBJ_DIRS "${_qpy_obj_dir}")
    endif()
endforeach()
if(QPY_OBJ_DIRS)
    list(REMOVE_DUPLICATES QPY_OBJ_DIRS)
    add_custom_target(${target}_obj_dirs
        COMMAND ${CMAKE_COMMAND} -E make_directory ${QPY_OBJ_DIRS}
        VERBATIM
    )
    add_dependencies(${target} ${target}_obj_dirs)
endif()

target_sources(${target} PRIVATE
    ${QPY_UPSTREAM_SOURCES}
    ${QPY_PORT_SOURCES}
)

set(MICROPY_TARGET ${target})
set(MICROPY_SOURCE_QSTR
    ${QPY_UPSTREAM_SOURCES}
    ${QPY_PORT_SOURCES}
)
list(REMOVE_ITEM MICROPY_SOURCE_QSTR ${MICROPY_PORT_DIR}/qpy_usrfs.c)
list(REMOVE_ITEM MICROPY_SOURCE_QSTR ${MICROPY_LIB_DIR}/littlefs/lfs.c)
list(REMOVE_ITEM MICROPY_SOURCE_QSTR ${MICROPY_LIB_DIR}/littlefs/lfs_util.c)


set(MICROPY_CPP_FLAGS
    -I${MICROPY_DIR}
    -I${MICROPY_PORT_DIR}
    -I${QPY_BOARD_CONFIG_DIR}
    -I${CMAKE_CURRENT_LIST_DIR}
    -I${CMAKE_CURRENT_LIST_DIR}/include
    -I${MICROPY_LIB_DIR}/littlefs
    -I${SOURCE_TOP_DIR}
    -I${SOURCE_TOP_DIR}/qos_components/components/include
    -I${SOURCE_TOP_DIR}/qos_components/system/at
    -I${SOURCE_TOP_DIR}/qos_components/system/debug
    -I${SOURCE_TOP_DIR}/qos_components/system/dev
    -I${SOURCE_TOP_DIR}/qos_components/system/hal
    -I${SOURCE_TOP_DIR}/qos_components/system/fota
    -I${SOURCE_TOP_DIR}/qos_components/system/include
    -I${SOURCE_TOP_DIR}/qos_components/system/modem/include
    -I${SOURCE_TOP_DIR}/qos_components/system/os
    -I${SOURCE_TOP_DIR}/qos_components/system/socket
    -I${SOURCE_TOP_DIR}/qos_components/system/public_comp/link_list
    -I${SOURCE_TOP_DIR}/qos_components/system/public_comp/event_notify
    -I${SOURCE_TOP_DIR}/qos_components/system/vfs
    -I${SOURCE_TOP_DIR}/qos_components/components/qurl/include
    -I${SOURCE_TOP_DIR}/qos_kernel/eigen_718/arch_prj/ql_common/unirtos_adapter/dynamic_create
    -I${SOURCE_TOP_DIR}/qos_kernel/eigen_718/arch_prj/ql_common/unirtos_adapter/system_source/plat_source/vfs
    -I${SOURCE_TOP_DIR}/qos_kernel/eigen_718/arch_prj/ql_common/middleware/quectel/cust/inc
    -I${SOURCE_TOP_DIR}/qos_applications/app_init
    -I${CMAKE_BINARY_DIR}/include
    -I${CMAKE_BINARY_DIR}
    -I${CMAKE_BINARY_DIR}/genhdr
    -DQPY_BOARD_${QPY_BOARD_ID}=1
    -DMICROPY_BOARD_BUILD_NAME="${build_project}"
)
set(MICROPY_MODULE_FROZEN_STR 1)
set(MICROPY_MODULE_FROZEN_MPY 0)
list(APPEND MICROPY_CPP_FLAGS
    -DMICROPY_MODULE_FROZEN_STR=\(1\)
    -DMICROPY_MODULE_FROZEN_MPY=\(0\)
)

include(${MICROPY_DIR}/py/mkrules.cmake)

target_link_libraries_if(CONFIG_QOSA_LINK_SYSTEM_LIBRARIES_FUNC THEN ${target} system_os)
target_link_libraries_if(CONFIG_QCM_FILE_API_FUNC THEN ${target} qcm_file)
target_link_libraries_if(CONFIG_QCM_QURL_FUNC THEN ${target} qurl)
