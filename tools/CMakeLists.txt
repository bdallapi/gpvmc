find_program(PYTHON "python")

if (PYTHON)
    set(SETUP_PY_IN "${CMAKE_CURRENT_SOURCE_DIR}/setup.py.in")
    set(SETUP_PY "${CMAKE_CURRENT_BINARY_DIR}/setup.py")
    set(DEPS "${CMAKE_CURRENT_SOURCE_DIR}/vmc_legacy_utils/vmc.py" "${CMAKE_CURRENT_SOURCE_DIR}/vmc_legacy_utils/stagflux.py" "${CMAKE_CURRENT_SOURCE_DIR}/vmc_legacy_utils/vmc_utils.py" "${CMAKE_CURRENT_SOURCE_DIR}/vmc_postproc/execute.py" "${CMAKE_CURRENT_SOURCE_DIR}/vmc_postproc/hl.py" "${CMAKE_CURRENT_SOURCE_DIR}/vmc_postproc/__init__.py" "${CMAKE_CURRENT_SOURCE_DIR}/vmc_postproc/load.py" "${CMAKE_CURRENT_SOURCE_DIR}/vmc_postproc/proc.py" "${CMAKE_CURRENT_SOURCE_DIR}/vmc_postproc/recspace.py" "${CMAKE_CURRENT_SOURCE_DIR}/vmc_postproc/sfpnxphz.py")
    set(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/timestamp")

    configure_file(${SETUP_PY_IN} ${SETUP_PY})

    add_custom_command(OUTPUT ${OUTPUT}
                       COMMAND ${CMAKE_COMMAND} -E touch ${OUTPUT}
                       DEPENDS ${DEPS})

    add_custom_target(collect ALL DEPENDS ${OUTPUT})

    install(CODE "execute_process(COMMAND ${PYTHON} ${SETUP_PY} install --user)")
endif(PYTHON)
add_executable(concatenate concatenate.cpp)

if (HDF5_FOUND)
    include_directories(${HDF5_INCLUDE_DIRS})
    target_link_libraries(concatenate ${HDF5_LIBRARIES})
    target_link_libraries(concatenate ${HDF5_HL_LIBRARIES})
endif (HDF5_FOUND)

install(TARGETS concatenate DESTINATION bin)
