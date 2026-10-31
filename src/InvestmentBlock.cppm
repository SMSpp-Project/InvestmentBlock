/*--------------------------------------------------------------------------*/
/*----------------------- File InvestmentBlock.cppm ------------------------*/
/*--------------------------------------------------------------------------*/
/** @file
 * Interface of the C++20 module smspp.investmentblock, which exports every
 * name that the headers of InvestmentBlock declare at namespace scope,
 * together with the modules it depends on; a translation unit can then write
 *
 *     import smspp.investmentblock;
 *
 * in place of the #include of the headers. The macros (e.g., those of the
 * factory in SMSFactory.h) are not exported by a module, so a file that uses
 * them still includes the header that defines them.
 *
 * This file is written by SMS++/cmake/smspp_module_exports.py, which reads
 * the names from the AST of the headers: it is not to be edited by hand.
 *
 * \author Rafael Durbano Lobato \n
 *         Dipartimento di Informatica \n
 *         Universita' di Pisa \n
 *
 * \author Antonio Frangioni \n
 *         Dipartimento di Informatica \n
 *         Universita' di Pisa \n
 *
 * \copyright &copy; by Rafael Durbano Lobato, Antonio Frangioni
 */
/*--------------------------------------------------------------------------*/
/*------------------------- GLOBAL MODULE FRAGMENT -------------------------*/
/*--------------------------------------------------------------------------*/

module;

#include "InvestmentBlock.h"

/*--------------------------------------------------------------------------*/
/*----------------------------- MODULE PURVIEW -----------------------------*/
/*--------------------------------------------------------------------------*/

export module smspp.investmentblock;

export import smspp;

export namespace SMSpp_di_unipi_it {

 // InvestmentBlock.h
 using SMSpp_di_unipi_it::InvestmentBlock;
 using SMSpp_di_unipi_it::InvestmentBlockSolution;

 // InvestmentFunction.h
 using SMSpp_di_unipi_it::InvestmentFunction;
 using SMSpp_di_unipi_it::InvestmentFunctionState;
}

/*--------------------------------------------------------------------------*/
/*--------------------- End File InvestmentBlock.cppm ----------------------*/
/*--------------------------------------------------------------------------*/
