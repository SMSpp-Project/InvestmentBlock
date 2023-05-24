/*--------------------------------------------------------------------------*/
/*------------------------- File InvestmentBlock.cpp -----------------------*/
/*--------------------------------------------------------------------------*/
/** @file
 * Implementation of the InvestmentBlock class.
 *
 * \author Rafael Durbano Lobato \n
 *         Dipartimento di Informatica \n
 *         Universita' di Pisa \n
 *
 * \copyright &copy; by Rafael Durbano Lobato
 */
/*--------------------------------------------------------------------------*/
/*---------------------------- IMPLEMENTATION ------------------------------*/
/*--------------------------------------------------------------------------*/
/*------------------------------ INCLUDES ----------------------------------*/
/*--------------------------------------------------------------------------*/

#include "InvestmentBlock.h"
#include "OneVarConstraint.h"

/*--------------------------------------------------------------------------*/
/*------------------------- NAMESPACE AND USING ----------------------------*/
/*--------------------------------------------------------------------------*/

using namespace SMSpp_di_unipi_it;

/*--------------------------------------------------------------------------*/
/*----------------------------- STATIC MEMBERS -----------------------------*/
/*--------------------------------------------------------------------------*/

SMSpp_insert_in_factory_cpp_1( InvestmentBlock );

/*--------------------------------------------------------------------------*/
/*------------------------- METHODS of InvestmentBlock ---------------------*/
/*--------------------------------------------------------------------------*/

/*--------------------------------------------------------------------------*/
/*-------------- CONSTRUCTING AND DESTRUCTING InvestmentBlock --------------*/
/*--------------------------------------------------------------------------*/

InvestmentBlock::~InvestmentBlock() {
 for( auto block : v_Block )
  delete block;
 v_Block.clear();

 for( auto & constraint : v_constraints )
  constraint.clear();

 objective.clear();
}

/*--------------------------------------------------------------------------*/

void InvestmentBlock::deserialize( const netCDF::NcGroup & group ) {

 Index num_assets = 0;

 if( ! ::deserialize_dim( group , "NumAssets" , num_assets ) )
  num_assets = 0;

 v_variables.resize( num_assets );
 for( auto & variable : v_variables )
  variable.set_Block( this );

 ::deserialize( group , "LowerBound" , { num_assets } , v_lower_bound ,
                true , true );

 ::deserialize( group , "UpperBound" , { num_assets } , v_upper_bound ,
                true , true );

 f_objective_sense = Objective::eMin;
 if( ::deserialize_dim( group , "ObjectiveSense" , f_objective_sense ) &&
     ( ! f_objective_sense ) ) {
  f_objective_sense = Objective::eMax;
 }

 if( ! v_lower_bound.empty() ) {
  if( v_lower_bound.size() == 1 )
   v_lower_bound.resize( num_assets , v_lower_bound.front() );
  else if( v_lower_bound.size() != num_assets )
   throw( std::logic_error( "InvestmentBlock::deserialize: the 'LowerBound' "
                            "netCDF variable, if provided, must have size 0,"
                            " 1, or 'NumAssets'." ) );
 }

 if( ! v_upper_bound.empty() ) {
  if( v_upper_bound.size() == 1 )
   v_upper_bound.resize( num_assets , v_upper_bound.front() );
  else if( v_upper_bound.size() != num_assets )
   throw( std::logic_error( "InvestmentBlock::deserialize: the 'UpperBound' "
                            "netCDF variable, if provided, must have size 0,"
                            " 1, or 'NumAssets'." ) );
 }

 auto investment_function = new InvestmentFunction();

 {
  std::vector< ColVariable * > p_variables;
  p_variables.reserve( v_variables.size() );
  for( auto & variable : v_variables )
   p_variables.push_back( & variable );
  investment_function->set_variables( std::move( p_variables ) );
 }

 investment_function->set_num_sub_blocks_per_stage
  ( f_num_sub_blocks_per_stage );
 investment_function->deserialize( group );
 set_function( investment_function );
 investment_function->set_f_Block( this );

 Block::deserialize( group );
}

/*--------------------------------------------------------------------------*/
/*-------------------------- OTHER INITIALIZATIONS -------------------------*/
/*--------------------------------------------------------------------------*/

void InvestmentBlock::generate_abstract_variables( Configuration * stvv ) {

 if( variables_generated() )
  return; // variables have already been generated

 if( ! v_variables.empty() )
  add_static_variable( v_variables , "investment" );

 set_variables_generated();
}

/*--------------------------------------------------------------------------*/

void InvestmentBlock::generate_objective( Configuration * objc ) {
 if( objective_generated() )
  return;  // Objective has already been generated

 objective.set_sense( f_objective_sense );

 // set Block objective
 set_objective( & objective );

 set_objective_generated();
}

/*--------------------------------------------------------------------------*/

void InvestmentBlock::generate_abstract_constraints( Configuration * stcc ) {

 if( constraints_generated() )
  return; // constraints have already been generated

 if( v_lower_bound.empty() && v_upper_bound.empty() )
  return; // there is no bound constraint

 f_reformulate_bounds = 0;
 auto config = dynamic_cast<SimpleConfiguration< int > *>( stcc );
 if( ( ! config ) && f_BlockConfig )
  config = dynamic_cast< SimpleConfiguration< int > * >
   ( f_BlockConfig->f_static_constraints_Configuration );
 if( config )
  f_reformulate_bounds = config->f_value;

 if( auto function =
     dynamic_cast< InvestmentFunction * >( objective.get_function() ) )
  function->reformulated_bounds( f_reformulate_bounds );

 // Initialize the constraints
 v_constraints.resize( v_variables.size() );
 for( Index i = 0 ; i < v_constraints.size() ; ++i ) {
  v_constraints[ i ].set_lhs( - Inf< double > () );
  v_constraints[ i ].set_rhs( Inf< double > () );
  v_constraints[ i ].set_variable( & v_variables[ i ] );
 }

 // Lower bound constraints
 if( ! v_lower_bound.empty() ) {
  assert( v_lower_bound.size() == v_constraints.size() );
  for( Index i = 0 ; i < v_constraints.size() ; ++i ) {

   if( f_reformulate_bounds && ( v_lower_bound[ i ] > -Inf< double >() ) ) {
    assert( v_lower_bound[ i ] != Inf< double >() );
    v_constraints[ i ].set_lhs( 0.0 );
   }
   else
    v_constraints[ i ].set_lhs( v_lower_bound[ i ] );
  }
 }

 // Upper bound constraints
 if( ! v_upper_bound.empty() ) {
  assert( v_upper_bound.size() == v_constraints.size() );
  for( Index i = 0 ; i < v_constraints.size() ; ++i ) {

   if( f_reformulate_bounds && ( i < v_lower_bound.size() ) &&
       ( v_lower_bound[ i ] > -Inf< double >() ) )
    v_constraints[ i ].set_rhs( v_upper_bound[ i ] - v_lower_bound[ i ] );
   else
    v_constraints[ i ].set_rhs( v_upper_bound[ i ] );
  }
 }

 add_static_constraint( v_constraints , "var_bounds" );

 set_constraints_generated();
}

/*--------------------------------------------------------------------------*/
/*------------- METHODS FOR Saving THE DATA OF THE InvestmentBlock ---------*/
/*--------------------------------------------------------------------------*/

void InvestmentBlock::serialize( netCDF::NcGroup & group ) const {

 Block::serialize( group );

 group.putAtt( "type" , "InvestmentBlock" );

 auto NumAssets = group.addDim( "NumAssets" , v_variables.size() );

 if( f_objective_sense == Objective::eMax )
  group.addDim( "ObjectiveSense" , 0 );

 ::serialize( group , "LowerBound" , netCDF::NcDouble() , NumAssets ,
              v_lower_bound );

 ::serialize( group , "UpperBound" , netCDF::NcDouble() , NumAssets ,
              v_upper_bound );

 auto function = objective.get_function();

 if( function ) {
  static_cast< InvestmentFunction * >( function )->serialize( group );
 }
}

/*--------------------------------------------------------------------------*/
/*---------------- Methods for checking the InvestmentBlock ----------------*/
/*--------------------------------------------------------------------------*/

bool InvestmentBlock::is_feasible( bool useabstract , Configuration * fsbc ) {
 if( v_variables.empty() )
  return true;

 // Retrieve the tolerance.

 auto config = dynamic_cast< SimpleConfiguration< double > * >( fsbc );

 if( ( ! config ) && f_BlockConfig )
  config = dynamic_cast< SimpleConfiguration< double > * >
   ( f_BlockConfig->f_is_feasible_Configuration );

 // If a tolerance has not been provided, use the default tolerance.
 const auto tolerance = config ? config->f_value : 1.0e-8;

 if( useabstract && ( ! v_constraints.empty() ) ) {
  // Use the set of Constraint to decide whether the current solution is
  // feasible.
  for( auto & constraint : v_constraints ) {
   if( constraint.is_relaxed() )
    continue;
   constraint.compute();
   if( constraint.abs_viol() > tolerance )
    return false;
  }

  return true;
 }

 // Check the "physical representation"

 for( Index i = 0 ; i < v_lower_bound.size() ; ++i )
  if( v_lower_bound[ i ] > -Inf< double >() )
   if( v_variables[ i ].get_value() < v_lower_bound[ i ] - tolerance )
    return false;

 for( Index i = 0 ; i < v_upper_bound.size() ; ++i )
  if( v_upper_bound[ i ] < Inf< double >() )
   if( v_variables[ i ].get_value() > v_upper_bound[ i ] + tolerance )
    return false;

 return true;
}

/*--------------------------------------------------------------------------*/
/*--------------------- End File InvestmentBlock.cpp -----------------------*/
/*--------------------------------------------------------------------------*/
