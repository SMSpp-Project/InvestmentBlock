##############################################################################
################################ makefile ####################################
##############################################################################
#                                                                            #
#   makefile of InvestmentBlock                                              #
#                                                                            #
#   Note that $(SMS++INC) is assumed to include any -I directive             #
#   corresponding to external libraries needed by SMS++, at least to the     #
#   extent in which they are needed by the parts of SMS++ used by            #
#   InvestmentBlock.                                                         #
#                                                                            #
#   Input:  $(CC)          = compiler command                                #
#           $(SW)          = compiler options                                #
#           $(SMS++INC)    = the -I$( core SMS++ directory )                 #
#           $(SMS++OBJ)    = the libSMS++ library itself                     #
#           $(SDDPBkH)     = the .h files to include for SDDPBlock           #
#           $(SDDPBkINC)   = the -I$( source directory ) for SDDPBlock       #
#           $(UCBckH)      = the .h files to include for UCBlock             #
#           $(UCBckINC)    = the -I$( source directory ) for UCBlock         #
#           $(InvsBkSDR)   = the directory where the source is               #
#                                                                            #
#   Output: $(InvsBkOBJ)   = the final object(s) / library                   #
#           $(InvsBkH)     = the .h files to include                         #
#           $(InvsBkINC)   = the -I$( source directory )                     #
#                                                                            #
#                              Antonio Frangioni                             #
#                         Dipartimento di Informatica                        #
#                             Universita' di Pisa                            #
#                                                                            #
##############################################################################

# macros to be exported - - - - - - - - - - - - - - - - - - - - - - - - - - -

InvsBkOBJ = $(InvsBkSDR)/obj/InvestmentBlock.o \
	$(InvsBkSDR)/obj/InvestmentFunction.o

InvsBkINC = -I$(InvsBkSDR)/include

InvsBkH   = $(InvsBkSDR)/include/InvestmentBlock.h \
	$(InvsBkSDR)/include/InvestmentFunction.h

# clean - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

clean::
	rm -f $(InvsBkOBJ) $(InvsBkSDR)/*~

# dependencies: every .o from its .C + every recursively included .h- - - - -

$(InvsBkSDR)/obj/InvestmentBlock.o: $(InvsBkSDR)/src/InvestmentBlock.cpp \
	$(InvsBkSDR)/include/InvestmentFunction.h $(SDDPBkH) $(UCBckH) \
	$(SMS++OBJ)
	$(CC) -c $(InvsBkSDR)/src/InvestmentBlock.cpp -o $@ $(InvsBkINC) \
	$(SDDPBkINC) $(UCBckINC) $(SMS++INC) $(SW)

$(InvsBkSDR)/obj/InvestmentFunction.o: $(InvsBkSDR)/src/InvestmentFunction.cpp \
	$(InvsBkSDR)/include/InvestmentFunction.h \
	$(InvsBkSDR)/include/InvestmentBlock.h $(SDDPBkH) $(UCBckH) $(SMS++OBJ)
	$(CC) -c $(InvsBkSDR)/src/InvestmentFunction.cpp -o $@ $(InvsBkINC) \
	$(SDDPBkINC) $(UCBckINC) $(SMS++INC) $(SW)

########################## End of makefile ###################################
