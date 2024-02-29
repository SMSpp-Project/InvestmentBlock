# --------------------------------------------------------------------------- #
#    CMake file for unit tests                                                #
#                                                                             #
#    In this file we define the unit tests for this library.                  #
#    Tests that should be supported by ctest executable must be added         #
#    with the add_test() command.                                             #
#                                                                             #
#    Note: this file tries to respect the principles of Modern CMake,         #
#          keep it in mind when editing it.                                   #
#                                                                             #
#                            Rafael Durbano Lobato                            #
#                         Dipartimento di Informatica                         #
#                             Universita' di Pisa                             #
# --------------------------------------------------------------------------- #

# ----- Standard unit tests ------------------------------------------------- #
add_executable(InvestmentBlock_test test.cpp)
target_link_libraries(InvestmentBlock_test PRIVATE SMS++::InvestmentBlock)
add_test(NAME InvestmentBlock_test
         COMMAND InvestmentBlock_test)
