/*--------------------------------------------------------------------------*/
/*--------------------- File SDDPBlockSolutionOutput.h ---------------------*/
/*--------------------------------------------------------------------------*/
/** @file
 *
 * \author Rafael Durbano Lobato \n
 *         Dipartimento di Informatica \n
 *         Universita' di Pisa \n
 *
 * \copyright &copy; by Rafael Durbano Lobato
 */

/*--------------------------------------------------------------------------*/
/*----------------------------- DEFINITIONS --------------------------------*/
/*--------------------------------------------------------------------------*/

#ifndef __SDDPBlockSolutionOutput
#define __SDDPBlockSolutionOutput

/*--------------------------------------------------------------------------*/
/*------------------------------ INCLUDES ----------------------------------*/
/*--------------------------------------------------------------------------*/

#include "SDDPBlock.h"
#include "StochasticBlock.h"
#include "UCBlockSolutionOutput.h"

#include <iomanip>
#include <iostream>

/*--------------------------------------------------------------------------*/
/*--------------------------- NAMESPACE ------------------------------------*/
/*--------------------------------------------------------------------------*/

using namespace SMSpp_di_unipi_it;

/*--------------------------------------------------------------------------*/
/*--------------------- CLASS SDDPBlockSolutionOutput ----------------------*/
/*--------------------------------------------------------------------------*/

class SDDPBlockSolutionOutput {

/*--------------------------------------------------------------------------*/
/*----------------------- PUBLIC PART OF THE CLASS -------------------------*/
/*--------------------------------------------------------------------------*/

public:

/*--------------------------------------------------------------------------*/
/*---------------------------- PUBLIC TYPES --------------------------------*/
/*--------------------------------------------------------------------------*/

 using Index = Block::Index;

/*--------------------------------------------------------------------------*/
/*--------------------- PUBLIC METHODS OF THE CLASS ------------------------*/
/*--------------------------------------------------------------------------*/

 void print_cuts( SDDPBlock * block , const Index sub_block_index ,
                  const std::string & filename ) const {

  if( block->get_polyhedral_functions().empty() )
   return;

  std::ofstream output( filename , std::ios::out );

  const auto num_var =
   block->get_polyhedral_functions().front()->get_num_active_var();

  output << "Timestep";
  for( Index i = 0 ; i < num_var ; ++i ) {
   output << separator_character << "a_" << std::to_string( i );
  }
  output << separator_character << "b" << std::endl;

  for( Index stage = 0 ; stage < block->get_time_horizon() ; ++stage ) {

   const auto & b =
    block->get_polyhedral_function( stage , 0 , sub_block_index )->get_b();
   const auto & A =
    block->get_polyhedral_function( stage , 0 , sub_block_index )->get_A();

   assert( b.size() == A.size() );

   for( Index i = 0 ; i < b.size() ; ++i ) {
    output << stage;
    for( Index j = 0 ; j < A[ i ].size() ; ++j )
     output << separator_character << std::setprecision( 20 ) << A[ i ][ j ];
    output << separator_character << std::setprecision( 20 ) << b[ i ]
           << std::endl;
   }
  }

  output.close();
 }

/*--------------------------------------------------------------------------*/

 void print( SDDPBlock * block , const Index sub_block_index ,
             const Index fault_stage = Inf<Index>() ) const {

  UCBlockSolutionOutput solution_output;
  solution_output.set_separator_character( separator_character );

  Index initial_time = 0;

  for( Index stage = 0 ;
       stage < std::min( block->get_time_horizon() , fault_stage ) ; ++stage ) {

   auto benders_block = static_cast< BendersBlock * >
    ( static_cast< StochasticBlock * >
      ( block->get_sub_Block( stage , sub_block_index ) )->
      get_nested_Blocks().front() );

   auto objective = static_cast< FRealObjective * >
    ( benders_block->get_objective() );

   auto benders_function = static_cast< BendersBFunction * >
    ( objective->get_function() );

   auto uc_block = static_cast< UCBlock * >
    ( benders_function->get_inner_block() );

   if( auto solver = benders_function->get_solver() ) {
    if( solver->has_var_solution() )
     solver->get_var_solution();
    if( auto cda_solver = dynamic_cast< CDASolver * >( solver ) )
     if( cda_solver->has_dual_solution() )
      cda_solver->get_dual_solution();
   }

   solution_output.print( uc_block );

   solution_output.set_append();
   initial_time += uc_block->get_time_horizon();
   solution_output.set_initial_time( initial_time );
  }
 }

/*--------------------------------------------------------------------------*/

 void print( SDDPBlock * block , const Index sub_block_index ,
             const Index scenario , const bool append ) const {

  UCBlockSolutionOutput solution_output;
  solution_output.set_separator_character( separator_character );

  Index initial_inner_time = 0;

  for( Index stage = 0 ; stage < block->get_time_horizon() ; ++stage ) {

   auto benders_block = static_cast< BendersBlock * >
    ( static_cast< StochasticBlock * >
      ( block->get_sub_Block( stage , sub_block_index ) )->
      get_nested_Blocks().front() );

   auto objective = static_cast< FRealObjective * >
    ( benders_block->get_objective() );

   auto benders_function = static_cast< BendersBFunction * >
    ( objective->get_function() );

   auto uc_block = static_cast< UCBlock * >
    ( benders_function->get_inner_block() );

   if( ! append ) {
    solution_output.set_append( false );
    solution_output.set_filenames_suffix
     ( "_Scen" + std::to_string( scenario ) + "_" + std::to_string( stage )
       + "_OUT.csv" );
   }
   else if( append ) {
    solution_output.set_filenames_suffix( "_Scen" + std::to_string( scenario ) +
                                          "_OUT.csv" );
    if( stage == 0 )
     solution_output.set_append( false );
    else
     solution_output.set_append( true );
   }

   solution_output.print( uc_block );

   initial_inner_time += uc_block->get_time_horizon();
   solution_output.set_initial_time( initial_inner_time );
  }
 }

/*--------------------------------------------------------------------------*/

 void set_separator_character( char separator_character ) {
  this->separator_character = separator_character;
 }

/*--------------------------------------------------------------------------*/
/*--------------------- PRIVATE PART OF THE CLASS --------------------------*/
/*--------------------------------------------------------------------------*/

private:

/*--------------------------------------------------------------------------*/
/*---------------------------- PRIVATE FIELDS  -----------------------------*/
/*--------------------------------------------------------------------------*/

 char separator_character = ',';

/*--------------------------------------------------------------------------*/
/*--------------------------------------------------------------------------*/

};  // end( class SDDPBlockSolutionOutput )

/*--------------------------------------------------------------------------*/
/*--------------------------------------------------------------------------*/

#endif  /* SDDPBlockSolutionOutput.h included */

/*--------------------------------------------------------------------------*/
/*------------------- End File SDDPBlockSolutionOutput.h -------------------*/
/*--------------------------------------------------------------------------*/
