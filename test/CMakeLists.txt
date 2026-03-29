# --------------------------------------------------------------------------- #
#    CMake file for unit tests                                                #
#                                                                             #
#    In this file we define the unit tests for this library.                  #
#    Tests that should be supported by ctest executable must be added         #
#    with the add_test() command.                                             #
#                                                                             #
#                                Donato Meoli                                 #
#                         Dipartimento di Informatica                         #
#                             Universita' di Pisa                             #
# --------------------------------------------------------------------------- #

# TODO this test needs to me moved in system /tests submodule

# Since we are using the block factory, objects from linked libraries
# may wrongly appear unused, and by default the linker does not include them,
# so we have to force the linking.
if (BUILD_SHARED_LIBS)
    if (UNIX AND (NOT APPLE))
        add_link_options("-Wl,--no-as-needed")
    endif ()
else ()
    if (MSVC)
        add_link_options("/WHOLEARCHIVE" "/FORCE:MULTIPLE")
    else () # Unix
        if (APPLE)
            add_link_options("-Wl,-all_load")
        else ()
            add_link_options("-Wl,--whole-archive,--allow-multiple-definition")
        endif ()
    endif ()
endif ()

# ----- Requirements -------------------------------------------------------- #
# If it's not being called by the umbrella, we need to
# look for the system-installed libraries.
if (NOT hasParent)
    # Blocks
    find_package(InvestmentBlock)
    # Solvers
    find_package(BundleSolver)
    find_package(MILPSolver)
endif ()

# ----- Tests --------------------------------------------------------------- #
if (TARGET SMS++::InvestmentBlock)

    add_executable(InvestmentBlock_test ../../tools/block_solver/common_utils.cpp
                                        test.cpp)
    target_include_directories(InvestmentBlock_test PRIVATE ../../tools/block_solver)
    target_link_libraries(InvestmentBlock_test PRIVATE SMS++::InvestmentBlock
                                                       SMS++::BundleSolver) # TODO this line needs to be removed and the following should be uncomment

    #[[if (TARGET SMS++::BundleSolver)
        target_link_libraries(InvestmentBlock_test PRIVATE SMS++::BundleSolver)
    endif ()

    if (TARGET SMS++::MILPSolver)
        target_link_libraries(InvestmentBlock_test PRIVATE SMS++::MILPSolver)
    endif ()]]

    if (WIN32 AND BUILD_SHARED_LIBS AND NOT DEFINED ENV{CONDA_BUILD})
        # copy runtime DLLs next to the exe after build
        add_custom_command(TARGET InvestmentBlock_test POST_BUILD
                           COMMAND ${CMAKE_COMMAND} -E copy_if_different
                           $<TARGET_RUNTIME_DLLS:InvestmentBlock_test>
                           $<TARGET_FILE_DIR:InvestmentBlock_test>
                           COMMAND_EXPAND_LISTS
                           COMMENT "Copying runtime DLLs next to InvestmentBlock_test.exe")
    endif()

    add_test(NAME InvestmentBlock_test
             COMMAND InvestmentBlock_test)

    # TODO this part needs to be removed when investment_solver in /tools will be aligned to InvestmentBlock
    # Install
    include(GNUInstallDirs)
    install(TARGETS InvestmentBlock_test
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif ()

# --------------------------------------------------------------------------- #
