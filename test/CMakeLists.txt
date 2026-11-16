# --------------------------------------------------------------------------- #
#    CMake file for InvestmentBlock/test                                      #
#                                                                             #
#    In this file we define the unit tests for this library.                  #
#    Tests that should be supported by ctest executable must be added         #
#    with the add_test() command.                                             #
#                                                                             #
#                                Donato Meoli                                 #
#                         Dipartimento di Informatica                         #
#                             Universita' di Pisa                             #
# --------------------------------------------------------------------------- #

# The classes of the modules register themselves in the factories from
# static objects, which nothing here names: a shared library is kept by
# what its module asks of the linker, while an archive has to be taken
# whole [see SMSppModule.cmake of SMS++].
smspp_keep_modules()

# ----- InvestmentBlock_unit_test ------------------------------------------- #
add_executable(${modName}_unit_test test.cpp)
target_link_libraries(${modName}_unit_test PRIVATE SMS++::InvestmentBlock)

# The inner Blocks of the instances are UCBlock, read through the factory,
# and solved by a :MILPSolver: without either in the build only the rest is
# tested. The umbrella adds them after this module, hence they are asked for
# by their option too; they are linked as targets, so that their link options
# keep their libraries in the program where the linker would drop them
if (TARGET SMS++::UCBlock OR BUILD_UCBlock)
    target_link_libraries(${modName}_unit_test PRIVATE SMS++::UCBlock)
    target_compile_definitions(${modName}_unit_test PRIVATE HAVE_UCBLOCK)
endif ()
if (TARGET SMS++::MILPSolver OR BUILD_MILPSolver)
    target_link_libraries(${modName}_unit_test PRIVATE SMS++::MILPSolver)
    target_compile_definitions(${modName}_unit_test PRIVATE HAVE_MILPSOLVER)
endif ()

smspp_copy_runtime(${modName}_unit_test)

# The tester builds its instances in memory and reads its Configuration
# files, which are next to it, from the working directory.
add_test(NAME ${modName}_unit_test
         COMMAND ${modName}_unit_test
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

# ----- the label of this module's tests ------------------------------------ #

# The label lets the module CI select exactly the tests exercising this module
# with `ctest -L <module>` (see tests/cmake/TestLabels.cmake in the umbrella
# for the system-tests counterpart): without it these tests are built and
# never run.
get_property(_tests DIRECTORY PROPERTY TESTS)
if (_tests)
    set_tests_properties(${_tests} PROPERTIES LABELS "${modName}")
endif ()

# --------------------------------------------------------------------------- #
