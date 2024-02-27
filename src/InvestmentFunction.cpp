/*--------------------------------------------------------------------------*/
/*---------------------- File InvestmentFunction.cpp -----------------------*/
/*--------------------------------------------------------------------------*/
/** @file
 * Implementation of the InvestmentFunction class.
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

#include "BatteryUnitBlock.h"
#include "BendersBFunction.h"
#include "BendersBlock.h"
#include "BlockSolverConfig.h"
#include "DCNetworkBlock.h"
#include "FRealObjective.h"
#include "Observer.h"
#include "OneVarConstraint.h"
#include "RBlockConfig.h"
#include "IntermittentUnitBlock.h"
#include "InvestmentFunction.h"
#include "SDDPBlock.h"
//!!#include "SDDPBlockSolutionOutput.h"
#include "SDDPGreedySolver.h"
#include "SDDPSolver.h"
#include "SMSTypedefs.h"
#include "StochasticBlock.h"
#include "ThermalUnitBlock.h"
#include "UCBlock.h"

#include <cmath>
#include <functional>
#include <queue>

#ifdef _OPENMP
#include <omp.h>
#endif

/*--------------------------------------------------------------------------*/
/*------------------------- NAMESPACE AND USING ----------------------------*/
/*--------------------------------------------------------------------------*/

using namespace SMSpp_di_unipi_it;

/*--------------------------------------------------------------------------*/
/*----------------------------- STATIC MEMBERS -----------------------------*/
/*--------------------------------------------------------------------------*/

// register InvestmentFunction to the Block factory

SMSpp_insert_in_factory_cpp_1( InvestmentFunction );
SMSpp_insert_in_factory_cpp_1( InvestmentFunctionState );

/*--------------------------------------------------------------------------*/
/*---------------------------------TODO-------------------------------------*/
/*--------------------------------------------------------------------------*/

void InvestmentFunction::load( std::istream &input , char frmt ) {
 throw( std::logic_error( "InvestmentFunction::load(): "
                          "not implemented yet." ) );
}

/*--------------------------------------------------------------------------*/
/*------------ CONSTRUCTING AND DESTRUCTING InvestmentFunction -------------*/
/*--------------------------------------------------------------------------*/

InvestmentFunction::InvestmentFunction
( Block * inner_block , VarVector && x , IndexVector && asset_indices ,
  AssetTypeVector && asset_type , RealVector && cost ,
  RealVector && disinvestment_cost , Observer * const observer )
 : C05Function( observer ) , f_blocks_are_updated( false ) ,
   f_solver_status( kUnEval ) , f_diagonal_linearization_required( false ) ,
   f_id( this ) {

 set_inner_block( inner_block );
 set_variables( std::move( x ) );

 v_asset_indices = std::move( asset_indices );
 v_asset_type = std::move( asset_type );
 v_cost = std::move( cost );
 v_disinvestment_cost = std::move( disinvestment_cost );

 f_violated_constraint = { Inf< Index >() , eLHS };

 v_events.resize( max_event_number() );

 // default parameter values

 f_compute_linearization = get_dflt_int_par( intComputeLinearization );
 AAccMlt = get_dflt_dbl_par( dblAAccMlt );
 set_par( intGPMaxSz , C05Function::get_dflt_int_par( intGPMaxSz ) );
}

/*--------------------------------------------------------------------------*/

InvestmentFunction::~InvestmentFunction() {
 for( auto block : v_Block )
  delete block;
}

/*--------------------------------------------------------------------------*/

void InvestmentFunction::deserialize( const netCDF::NcGroup & group ,
                                      ModParam issueMod ) {

 // Deserialize the attributes

 auto replicate_battery = group.getAtt( "ReplicateBatteryUnits" );
 if( ( ! replicate_battery.isNull() ) ) {
  int replicate;
  replicate_battery.getValues( & replicate );
  f_replicate_battery = replicate;
 }

 auto replicate_intermittent = group.getAtt( "ReplicateIntermittentUnits" );
 if( ! replicate_intermittent.isNull() ) {
  int replicate;
  replicate_intermittent.getValues( & replicate );
  f_replicate_intermittent = replicate;
 }

 // Deserialize the dimensions

 // Number of assets

 Index num_assets;

 if( ! ::deserialize_dim( group , "NumAssets" , num_assets ) )
  num_assets = 0;

 if( ! v_x.empty() ) {
  if( num_assets != v_x.size() )
   throw std::logic_error( "InvestmentFunction::deserialize: the number of "
                           "assets to invest (" + std::to_string( num_assets ) +
                           ") is different from the number of active variables "
                           "(" + std::to_string( v_x.size() ) + ")." );
 }

 // Number of linear constraints

 Index num_constraints;

 if( ! ::deserialize_dim( group , "NumConstraints" , num_constraints ) )
  num_constraints = 0;

 // Deserialize the assets

 if( num_assets ) {

  // Deserialize the asset indices.

  ::deserialize( group , "Assets" , num_assets , v_asset_indices , false );

  // Deserialize the types of assets.

  if( ! ::deserialize( group , "AssetType" , num_assets , v_asset_type ,
                       true , true ) )
   v_asset_type.resize( num_assets , eUnitBlock );

  if( ! v_asset_type.empty() ) {
   if( v_asset_type.size() == 1 )
    v_asset_type.resize( num_assets , v_asset_type.front() );
   else if( v_asset_type.size() != num_assets )
    throw( std::logic_error( "InvestmentFunction::deserialize: the 'AssetType'"
                             " netCDF variable, if provided, must have size 0,"
                             " 1, or 'NumAssets'." ) );

   assert( v_asset_indices.size() == v_asset_type.size() );
   for( Index i = 0 ; i < v_asset_indices.size() ; ++i )
    for( Index j = i + 1 ; j < v_asset_indices.size() ; ++j )
     if( ( v_asset_indices[ i ] == v_asset_indices[ j ] ) &&
         ( v_asset_type[ i ] == v_asset_type[ j ] ) )
      throw( std::logic_error
             ( "InvestmentFunction::deserialize: asset with index " +
               std::to_string( v_asset_indices[ i ] ) + " and type " +
               std::to_string( v_asset_type[ i ] ) + " is duplicated." ) );
  }

  // Deserialize the lower bound on the active variables

  ::deserialize( group , "LowerBound" , { num_assets } , v_lower_bound ,
                 true , true );

  if( ! v_lower_bound.empty() ) {
   if( v_lower_bound.size() == 1 )
    v_lower_bound.resize( num_assets , v_lower_bound.front() );
   else if( v_lower_bound.size() != num_assets )
    throw( std::logic_error( "InvestmentFunction::deserialize: the 'LowerBound'"
                             " netCDF variable, if provided, must have size 0,"
                             " 1, or 'NumAssets'." ) );
  }

  // Deserialize the costs of investments

  if( ::deserialize( group , "Cost" , num_assets ,
                     v_cost , true , true ) ) {
   if( v_cost.size() == 1 )
    v_cost.resize( num_assets , v_cost.front() );
   else if( v_cost.size() != num_assets )
    throw( std::logic_error( "InvestmentFunction::deserialize: the 'Cost'"
                             " netCDF variable, if provided, must have size "
                             "0, 1, or 'NumAssets'." ) );
  }
  else {
   // All coefficients are zero.
   v_cost.resize( num_assets , 0 );
  }

  // Deserialize the costs of disinvestments

  if( ::deserialize( group , "DisinvestmentCost" , num_assets ,
                     v_disinvestment_cost , true , true ) ) {
   if( v_disinvestment_cost.size() == 1 )
    v_disinvestment_cost.resize( num_assets , v_disinvestment_cost.front() );
   else if( v_disinvestment_cost.size() != num_assets )
    throw( std::logic_error( "InvestmentFunction::deserialize: the "
                             "'DisinvestmentCost' netCDF variable, if provided,"
                             " must have size 0, 1, or 'NumAssets'." ) );
  }
  else {
   // All coefficients are zero.
   v_disinvestment_cost.resize( num_assets , 0 );
  }

  // Deserialize the amount of assets currently installed in the system

  if( ::deserialize( group , "InstalledQuantity" , num_assets ,
                     v_installed_quantity , true , true ) ) {
   if( v_installed_quantity.size() == 1 )
    v_installed_quantity.resize( num_assets , v_installed_quantity.front() );
   else if( v_installed_quantity.size() != num_assets )
    throw( std::logic_error( "InvestmentFunction::deserialize: the "
                             "'InstalledCapacity' netCDF variable, if provided,"
                             " must have size 0, 1, or 'NumAssets'." ) );
  }

 } // end( if( num_assets ) )

 // Deserialize the linear constraints

 if( num_constraints ) {

  if( ::deserialize( group , "Constraints_LowerBound" , num_constraints ,
                     v_constraints_lower_bound , true , true ) ) {
   if( v_constraints_lower_bound.size() == 1 )
    v_constraints_lower_bound.resize( num_constraints ,
                                      v_constraints_lower_bound.front() );
   else if( v_constraints_lower_bound.size() != num_constraints )
    throw( std::logic_error
           ( "InvestmentFunction::deserialize: the 'Constraints_LowerBound'"
             " netCDF variable, if provided, must have size "
             "0, 1, or 'NumConstraints'." ) );
  }
  else {
   // The lower bound is - infinity
   v_constraints_lower_bound.resize( num_constraints , -Inf< double >() );
  }

  if( ::deserialize( group , "Constraints_UpperBound" , num_constraints ,
                     v_constraints_upper_bound , true , true ) ) {
   if( v_constraints_upper_bound.size() == 1 )
    v_constraints_upper_bound.resize( num_constraints ,
                                      v_constraints_upper_bound.front() );
   else if( v_constraints_upper_bound.size() != num_constraints )
    throw( std::logic_error
           ( "InvestmentFunction::deserialize: the 'Constraints_UpperBound'"
             " netCDF variable, if provided, must have size "
             "0, 1, or 'NumConstraints'." ) );
  }
  else {
   // The upper bound is infinity
   v_constraints_upper_bound.resize( num_constraints , Inf< double >() );
  }

  for( Index i = 0 ; i < num_constraints ; ++i )
   if( v_constraints_lower_bound[ i ] > v_constraints_upper_bound[ i ] )
    throw( std::logic_error
           ( "InvestmentFunction::deserialize: Constraints_LowerBound[" +
             std::to_string( i ) + "] = " +
             std::to_string( v_constraints_lower_bound[ i ] ) + " > " +
             std::to_string( v_constraints_upper_bound[ i ] ) + " = " +
             "Constraints_UpperBound[" + std::to_string( i ) + "]." ) );

  auto A = group.getVar( "Constraints_A" );
  if( A.isNull() )
   throw( std::logic_error( "InvestmentFunction::deserialize: the netCDF "
                            "variable 'Constraints_A' has not been "
                            "provided." ) );

  auto dim_A = A.getDims();
  if( ( A.getDimCount() != 2 ) || ( dim_A[ 0 ].getSize() != num_constraints ) ||
      ( dim_A[ 1 ].getSize() != num_assets ) )
   throw( std::logic_error( "InvestmentFunction::deserialize: the netCDF "
                            "variable 'Constraints_A' must have dimensions "
                            "'NumConstraints' x 'NumAssets'" ) );

  v_A.resize( num_constraints );
  for( Index i = 0 ; i < v_A.size() ; ++i ) {
   v_A[ i ].resize( num_assets );
   A.getVar( { i , 0 } , { 1 , num_assets } , v_A[ i ].data() );
  }

 } // end( deserialize linear constraints )

 // Deserialize the inner Block

 auto inner_block_group = group.getGroup( BLOCK_NAME );
 if( inner_block_group.isNull() )
  throw std::logic_error( "InvestmentFunction::deserialize: the '" +
                          BLOCK_NAME + "' group must be present." );

 auto inner_block = Block::new_Block( inner_block_group , this );

 if( ! inner_block )
  throw std::logic_error( "InvestmentFunction::deserialize: it was not "
                          "possible to create the inner Block from group '" +
                          BLOCK_NAME + "'." );

 if( ! ( dynamic_cast< SDDPBlock * >( inner_block ) ||
         dynamic_cast< UCBlock * >( inner_block ) ) )
  throw std::logic_error( "InvestmentFunction::deserialize: the inner "
                          "Block is neither an SDDPBlock nor a UCBlock." );

 set_inner_block( inner_block );

 Block::deserialize( group );

}  // end( InvestmentFunction::deserialize )

/*--------------------------------------------------------------------------*/
/*-------------------------- OTHER INITIALIZATIONS -------------------------*/
/*--------------------------------------------------------------------------*/

void InvestmentFunction::set_default_inner_Block_BlockConfig() {
 if( v_Block.empty() || ( ! v_Block.front() ) )
  return;
 assert( v_Block.size() == 1 );
 auto config = new OCRBlockConfig( v_Block.front() );
 config->clear();
 config->apply( v_Block.front() );
 delete config;
}

/*--------------------------------------------------------------------------*/

void InvestmentFunction::set_default_inner_Block_BlockSolverConfig() {
 if( v_Block.empty() || ( ! v_Block.front() ) )
  return;
 assert( v_Block.size() == 1 );
 auto solver_config = new RBlockSolverConfig( v_Block.front() );
 solver_config->clear();
 solver_config->apply( v_Block.front() );
 delete solver_config;
}

/*--------------------------------------------------------------------------*/

void InvestmentFunction::set_ComputeConfig( ComputeConfig * scfg ) {

 if( v_Block.empty() ||
     std::any_of( v_Block.cbegin() , v_Block.cend() ,
                  []( Block * b ) { return( b == nullptr ); } ) )
  throw( std::logic_error( "InvestmentFunction::set_ComputeConfig: "
                           "the inner Block is not present." ) );

 if( ! scfg ) {
  // scfg is nullptr
  ThinComputeInterface::set_ComputeConfig();
  set_default_inner_Block_configuration();
  return;
 }

 if( ! scfg->f_extra_Configuration ) {
  // scfg->f_extra_Configuration is nullptr
  ThinComputeInterface::set_ComputeConfig( scfg );
  if( ! scfg->f_diff )
   set_default_inner_Block_configuration();
  return;
 }

 auto config_map = dynamic_cast
  < SimpleConfiguration< std::map< std::string , Configuration * > > * >
  ( scfg->f_extra_Configuration );

 if( ! config_map )
  // An invalid extra Configuration has not been provided.
  throw( std::invalid_argument( "InvestmentFunction::set_ComputeConfig: "
                                "invalid extra_Configuration." ) );

 ThinComputeInterface::set_ComputeConfig( scfg );

 for( const auto & [ key , config ] : config_map->f_value ) {

  if( key == "BlockConfig" ) {
   if( ! config ) {
    if( ! scfg->f_diff )
     // A BlockConfig for the inner Block was not provided. The inner Block is
     // configured to its default configuration.
     set_default_inner_Block_BlockConfig();
   }
   else if( auto block_config = dynamic_cast< BlockConfig * >( config ) ) {
    // A BlockConfig for the inner Block has been provided. Apply it.
    block_config->apply( v_Block.front() );
   }
   else
    // An invalid Configuration has been provided.
    throw( std::invalid_argument
           ( "InvestmentFunction::set_ComputeConfig: the Configuration "
             "associated with key \"BlockConfig\" is not a BlockConfig." ) );
  }
  else if( key == "BlockSolverConfig" ) {
   if( ! config ) {
    if( ! scfg->f_diff )
     // A BlockSolverConfig for the inner Block was not provided. The Solver
     // of the inner Block (and their sub-Block, recursively) are unregistered
     // and deleted.
     set_default_inner_Block_BlockSolverConfig();
   }
   else if( auto bsc = dynamic_cast< BlockSolverConfig * >( config ) ) {
    // A BlockSolverConfig for the inner Block has been provided. Apply it.
    bsc->apply( v_Block.front() );
   }
   else
    // An invalid Configuration has been provided.
    throw( std::invalid_argument
           ( "InvestmentFunction::set_ComputeConfig: the Configuration "
             "associated with key \"BlockSolverConfig\" is not a "
             "BlockSolverConfig." ) );
  }
  else {
   // An invalid key has been provided.
   throw( std::invalid_argument( "InvestmentFunction::set_ComputeConfig: "
                                 "invalid key: " + key ) );
  }
 }
}

/*--------------------------------------------------------------------------*/

void InvestmentFunction::set_variables( VarVector && x ) {
 if( ! v_cost.empty() )
  if( v_cost.size() != x.size() )
   throw( std::logic_error("InvestmentFunction::set_variables: given x has "
                           "size " + std::to_string( x.size() ) + ", but the "
                           "number of linear coefficients is " +
                           std::to_string( v_cost.size() ) ) );

 if( ! v_disinvestment_cost.empty() )
  if( v_disinvestment_cost.size() != x.size() )
   throw( std::logic_error("InvestmentFunction::set_variables: given x has "
                           "size " + std::to_string( x.size() ) + ", but the "
                           "number of linear coefficients is " +
                           std::to_string( v_disinvestment_cost.size() ) ) );

 v_x = std::move( x );
 f_blocks_are_updated = false;
}  // end( InvestmentFunction::set_variables )

/*--------------------------------------------------------------------------*/

void InvestmentFunction::set_par( const idx_type par , const int value ) {
 switch( par ) {

  case( intComputeLinearization ):
   f_compute_linearization = value;
   break;

  case( intOutputSolution ):
   f_output_solution = value;
   break;

  case( intGPMaxSz ): {
   if( value < 0 )
    throw( std::invalid_argument( "InvestmentFunction::set_par: intGPMaxSz "
                                  "must be non-negative" ) );

   auto old_size = global_pool.size();

   global_pool.resize( value );

   if( f_Observer && ( decltype( old_size )( value ) < old_size ) ) {
    // The size of the global pool is being reduced. We store in "which" the
    // indices of the deleted linearizations.
    Subset which( global_pool.size() - value );
    std::iota( which.begin() , which.end() , value );
    f_Observer->add_Modification
     ( std::make_shared< C05FunctionMod >
       ( this , C05FunctionMod::GlobalPoolRemoved , std::move( which ) , 0 ) );
   }

   break;
  }

  default: C05Function::set_par( par , value );
 }
}  // end( InvestmentFunction::set_par )

/*--------------------------------------------------------------------------*/
/*---------------------- METHODS FOR EVENTS HANDLING -----------------------*/
/*--------------------------------------------------------------------------*/

void InvestmentFunction::reset_event_handler( int type , EventID id ) {
 if( ( type != eBeforeTermination ) && ( type != eAtTheBeginning ) )
  throw( std::invalid_argument( "InvestmentFunction: unsupported event "
                                "type " + std::to_string( type ) ) );

 if( id >= v_events[ type ].size() )
  throw( std::invalid_argument( "InvestmentFunction: incorrect event id " +
                                std::to_string( id ) + " for type " +
                                std::to_string( type ) ) );

 static auto do_nothing = []() -> int {
  return( ThinComputeInterface::eContinue ); };

 if( id == v_events[ type ].size() - 1 ) {
  // if the event is the last of its type, shorten the vector; moreover, if
  // any of the previous events is a do_nothing, keep shortening
  do
   v_events[ type ].pop_back();
  while( ( ! v_events[ type ].empty() ) &&
         ( *( v_events[ type ].back().target < int( * )() > ( ) ) ==
           do_nothing ) );
 }
 else
  // the event is not the last of its type: replace it with a do_nothing to
  // avoid messing up with the id-s, which are positions in the vector
  v_events[ type ][ id ] = do_nothing;
}

/*--------------------------------------------------------------------------*/
/*-------- METHODS FOR HANDLING THE State OF THE InvestmentFunction --------*/
/*--------------------------------------------------------------------------*/

State * InvestmentFunction::get_State( void ) const {
 return( new InvestmentFunctionState( this ) );
}  // end( InvestmentFunction::get_State )

/*--------------------------------------------------------------------------*/

void InvestmentFunction::put_State( const State & state ) {

 const auto & s = dynamic_cast< const InvestmentFunctionState & >( state );

 const bool global_pool_was_empty = global_pool.empty();

 global_pool.clone( s.global_pool );

 if( ! f_Observer )
  return;

 // If the global pool was not initially empty, issue a Modification telling
 // that all previous linearizations have been removed.

 if( ! global_pool_was_empty )
  f_Observer->add_Modification( std::make_shared< C05FunctionMod >
                                ( this , C05FunctionMod::GlobalPoolRemoved ,
                                  Subset() , 0 , 0 ) );

 // Collect the indices of all linearizations that were added and issue the
 // Modification.

 Subset added;
 added.reserve( global_pool.size() );
 for( Index i = 0 ; i < global_pool.size() ; ++i )
  if( global_pool.is_linearization_there( i ) )
   added.push_back( i );

 if( ! added.empty() )
  f_Observer->add_Modification( std::make_shared< C05FunctionMod >
                                ( this , C05FunctionMod::GlobalPoolAdded ,
                                  std::move( added ) , 0 , 0 ) );

}  // end( InvestmentFunction::put_State )

/*--------------------------------------------------------------------------*/

void InvestmentFunction::put_State( State && state ) {

 auto && s = dynamic_cast< InvestmentFunctionState && >( state );

 const bool global_pool_was_empty = global_pool.empty();

 global_pool.clone( std::move( s.global_pool ) );

 if( ! f_Observer )
  return;

 // If the global pool was not initially empty, issue a Modification telling
 // that all previous linearizations have been removed.

 if( ! global_pool_was_empty )
  f_Observer->add_Modification( std::make_shared< C05FunctionMod >
                                ( this , C05FunctionMod::GlobalPoolRemoved ,
                                  Subset() , 0 , 0 ) );

 // Collect the indices of all linearizations that were added and issue the
 // Modification.

 Subset added;
 added.reserve( global_pool.size() );
 for( Index i = 0 ; i < global_pool.size() ; ++i )
  if( global_pool.is_linearization_there( i ) )
   added.push_back( i );

 if( ! added.empty() )
  f_Observer->add_Modification( std::make_shared< C05FunctionMod >
                                ( this , C05FunctionMod::GlobalPoolAdded ,
                                  std::move( added ) , 0 , 0 ) );
}  // end( InvestmentFunction::put_State )

/*--------------------------------------------------------------------------*/

void InvestmentFunction::serialize_State
( netCDF::NcGroup & group , const std::string & sub_group_name ) const {

 if( ! sub_group_name.empty() ) {
  auto g = group.addGroup( sub_group_name );
  serialize_State( g );
  return;
 }

 group.putAtt( "type" , "InvestmentFunctionState" );
 global_pool.serialize( group );

}  // end( InvestmentFunction::serialize_State )

/*--------------------------------------------------------------------------*/
/*---- METHODS FOR HANDLING "ACTIVE" Variable IN THE InvestmentFunction ----*/
/*--------------------------------------------------------------------------*/

void InvestmentFunction::map_active( c_Vec_p_Var & vars , Subset & map ,
                                     const bool ordered ) const {
 if( v_x.empty() )
  return;

 if( map.size() < vars.size() )
  map.resize( vars.size() );

 if( ordered ) {
  Index found = 0;
  for( Index i = 0 ; i < v_x.size() ; ++i ) {
   auto itvi = std::lower_bound( vars.begin() , vars.end() , v_x[ i ] );
   if( itvi != vars.end() ) {
    map[ std::distance( vars.begin() , itvi ) ] = i;
    ++found;
   }
  }
  if( found < vars.size() )
   throw( std::invalid_argument( "InvestmentFunction::map_active: some Variable "
                                 "is not active." ) );
 }
 else {
  auto it = map.begin();
  for( auto var : vars ) {
   auto i = this->is_active( var );
   if( i >= v_x.size() )
    throw( std::invalid_argument( "InvestmentFunction::map_active: some Variable "
                                  "is not active" ) );
   *(it++) = i;
  }
 }
}  // end( InvestmentFunction::map_active )

/*--------------------------------------------------------------------------*/
/*-------------- METHODS FOR MODIFYING THE InvestmentFunction --------------*/
/*--------------------------------------------------------------------------*/

void InvestmentFunction::remove_variable( Index i , ModParam issueMod ) {
 if( i >= v_x.size() )
  throw( std::logic_error( "InvestmentFunction::remove_variable: invalid "
                           "Variable index " + std::to_string( i ) + "." ) );

 auto var = v_x[ i ];
 v_x.erase( v_x.begin() + i );    // erase it in v_x

 // Erase the asset index, asset type, and the linear coefficient associated
 // with the Variable being removed
 v_asset_indices.erase( v_asset_indices.begin() + i );
 v_asset_type.erase( v_asset_type.begin() + i );

 f_blocks_are_updated = false;
 generator_node_map.clear(); // the generator map must be rebuilt

 if( ( ! f_Observer ) || ( ! f_Observer->issue_mod( issueMod ) ) )
  return;

 // Now issue the Modification.
 // An InvestmentFunction is strongly quasi-additive.
 f_Observer->add_Modification( std::make_shared< C05FunctionModVarsRngd >
                               ( this , Vec_p_Var( { var } ) ,
                                 Range( i , i + 1 ) , 0 ,
                                 Observer::par2concern( issueMod ) ) ,
                               Observer::par2chnl( issueMod ) );

}  // end( InvestmentFunction::remove_variable( index ) )

/*--------------------------------------------------------------------------*/

void InvestmentFunction::remove_variables( Range range , ModParam issueMod ) {

 range.second = std::min( range.second , Index( v_x.size() ) );
 if( range.second <= range.first )
  return;

 f_blocks_are_updated = false;
 generator_node_map.clear(); // the generator map must be rebuilt

 if( ( range.first == 0 ) && ( range.second == Index( v_x.size() ) ) ) {
  // removing *all* Variables
  Vec_p_Var vars( v_x.size() );

  for( decltype( v_x )::size_type i = 0 ; i < v_x.size() ; ++i )
   vars[ i ] = v_x[ i ];

  v_x.clear();
  v_asset_indices.clear();
  v_asset_type.clear();
  v_cost.clear();
  v_disinvestment_cost.clear();

  // Now issue the Modification.
  // An InvestmentFunction is strongly quasi-additive.
  if( f_Observer && f_Observer->issue_mod( issueMod ) )
   f_Observer->add_Modification( std::make_shared< C05FunctionModVarsRngd >
                                 ( this , std::move( vars ) , range , 0 ,
                                   Observer::par2concern( issueMod ) ) ,
                                 Observer::par2chnl( issueMod ) );
  return;
 }

 // Removing *some* Variables.

 // Iterators to the Variables to be removed.

 const auto v_x_it = std::make_pair( v_x.begin() + range.first ,
                                     v_x.begin() + range.second );

 const auto erase = [ this , &v_x_it , range ]() {

  // Iterators to the elements to be removed

  const auto v_asset_indices_it =
   std::make_pair( v_asset_indices.begin() + range.first ,
                   v_asset_indices.begin() + range.second );

  const auto v_asset_type_it =
   std::make_pair( v_asset_type.begin() + range.first ,
                   v_asset_type.begin() + range.second );

  const auto v_cost_it =
   std::make_pair( v_cost.begin() + range.first ,
                   v_cost.begin() + range.second );

  const auto v_disinvestment_cost_it =
   std::make_pair( v_disinvestment_cost.begin() + range.first ,
                   v_disinvestment_cost.begin() + range.second );

  v_x.erase( v_x_it.first , v_x_it.second );
  v_asset_indices.erase( v_asset_indices_it.first , v_asset_indices_it.second );
  v_asset_type.erase( v_asset_type_it.first , v_asset_type_it.second );
  v_cost.erase( v_cost_it.first , v_cost_it.second );
  v_disinvestment_cost.erase( v_disinvestment_cost_it.first ,
                              v_disinvestment_cost_it.second );
 };

 if( f_Observer && f_Observer->issue_mod( issueMod ) ) {
  // Somebody is there: meanwhile, prepare data for the Modification

  Vec_p_Var vars( range.second - range.first );
  std::copy( v_x_it.first , v_x_it.second , vars.begin() );

  // Erase the elements associated with the Variables being removed
  erase();

  // Now issue the Modification.
  // An InvestmentFunction is strongly quasi-additive
  f_Observer->add_Modification( std::make_shared< C05FunctionModVarsRngd >
                                ( this , std::move( vars ) , range , 0 ,
                                  Observer::par2concern( issueMod ) ) ,
                                Observer::par2chnl( issueMod ) );
 }
 else  // no one is there: just do it
  // Erase the elements associated with the Variables being removed
  erase();

}  // end( InvestmentFunction::remove_variables( range ) )

/*--------------------------------------------------------------------------*/

template< class T >
static void compact( std::vector< T > & x ,
                     const InvestmentFunction::Subset & indices ) {

 InvestmentFunction::Index i = indices.front();
 auto xit = x.begin() + (i++);
 for( auto nit = ++(indices.begin()) ; nit != indices.end() ; ++i )
  if( *nit == i )
   ++nit;
  else
   *(xit++) = std::move( x[ i ] );

 for( ; i < x.size() ; ++i )
  *(xit++) = std::move( x[ i ] );

 x.resize( x.size() - indices.size() );
}

/*--------------------------------------------------------------------------*/

void InvestmentFunction::remove_variables( Subset && indices , bool ordered ,
                                           ModParam issueMod ) {

 if( indices.empty() ) {      // removing *all* Variables

  if( v_x.empty() )       // there is no Variable to be removed
   return;                // cowardly (and silently) return

  Vec_p_Var vars( v_x.size() );

  for( Index i = 0 ; i < v_x.size() ; ++i )
   vars[ i ] = v_x[ i ];

  // Clear all elements
  v_x.clear();
  v_asset_indices.clear();
  v_asset_type.clear();
  v_cost.clear();
  v_disinvestment_cost.clear();

  f_blocks_are_updated = false;
  generator_node_map.clear(); // the generator map must be rebuilt

  // Now issue the Modification: note that the subset is empty.
  // An InvestmentFunction is strongly quasi-additive, and indices is ordered.
  if( f_Observer && f_Observer->issue_mod( issueMod ) )
   f_Observer->add_Modification( std::make_shared< C05FunctionModVarsSbst >
                                 ( this , std::move( vars ) , Subset() , true ,
                                   0 , Observer::par2concern( issueMod ) ) ,
                                 Observer::par2chnl( issueMod ) );
  return;
 }

 // removing *some* Variables

 if( ! ordered )
  std::sort( indices.begin() , indices.end() );

 if( indices.back() >= v_x.size() )  // the last name is wrong
  throw( std::invalid_argument( "InvestmentFunction::remove_variables: wrong "
                                "Variable index in the Subset indices." ) );

 f_blocks_are_updated = false;
 generator_node_map.clear(); // the generator map must be rebuilt

 const auto erase = [ this , &indices ]() {
  compact( v_asset_indices , indices );
  compact( v_asset_type , indices );
  compact( v_cost , indices );
  compact( v_disinvestment_cost , indices );
  compact( v_x , indices );
 };

 if( f_Observer && f_Observer->issue_mod( issueMod ) ) {
  Vec_p_Var vars( indices.size() );
  auto its = vars.begin();
  for( auto nm : indices )
   *(its++) = v_x[ nm ];

  erase();

  // Remove

  // Now issue the Modification.
  // An InvestmentFunction is strongly quasi-additive, and indices is ordered.
  f_Observer->add_Modification( std::make_shared< C05FunctionModVarsSbst >
                                ( this , std::move( vars ) ,
                                  std::move( indices ) , true , 0 ,
                                  Observer::par2concern( issueMod ) ) ,
                                Observer::par2chnl( issueMod ) );
 }
 else  // no one is there: just do it
  erase();

}  // end( InvestmentFunction::remove_variables( subset ) )

/*--------------------------------------------------------------------------*/
/*------------ METHODS FOR Saving THE DATA OF THE InvestmentFunction -------*/
/*--------------------------------------------------------------------------*/

void InvestmentFunction::serialize( netCDF::NcGroup & group ) const {

 Block::serialize( group );

 if( f_replicate_battery )
  group.putAtt( "ReplicateBatteryUnits" , netCDF::NcInt() ,
                int( f_replicate_battery ) );

 if( f_replicate_intermittent )
  group.putAtt( "ReplicateIntermittentUnits" , netCDF::NcInt() ,
                int( f_replicate_intermittent ) );

 const auto num_assets = v_asset_indices.size();
 auto NumAssets = group.addDim( "NumAssets" , num_assets );

 ::serialize( group , "Assets" , netCDF::NcUint() , NumAssets ,
              v_asset_indices );

 ::serialize( group , "AssetType" , netCDF::NcUint() , NumAssets ,
              v_asset_type );

 ::serialize( group , "LowerBound" , netCDF::NcDouble() , NumAssets ,
              v_lower_bound );

 ::serialize( group , "Cost" , netCDF::NcDouble() , NumAssets , v_cost );

 ::serialize( group , "DisinvestmentCost" , netCDF::NcDouble() , NumAssets ,
              v_disinvestment_cost );

 if( ! v_installed_quantity.empty() )
  ::serialize( group , "InstalledQuantity" , netCDF::NcDouble() , NumAssets ,
               v_installed_quantity );

 if( ! v_A.empty() ) {
 // Deserialize the linear constraints

  const auto num_constraints = v_A.size();
  auto NumConstraints = group.addDim( "NumConstraints" , num_constraints );

 ::serialize( group , "Constraints_LowerBound" , netCDF::NcDouble() ,
              NumConstraints , v_constraints_lower_bound );

 ::serialize( group , "Constraints_UpperBound" , netCDF::NcDouble() ,
              NumConstraints , v_constraints_upper_bound );

  auto Constraints_A = group.addVar( "Constraints_A" , netCDF::NcDouble() ,
                                     { NumConstraints , NumAssets } );

  for( Index i = 0 ; i < num_constraints ; ++i )
   Constraints_A.putVar( { i , 0 } , { 1 , num_assets } , v_A[ i ].data() );
 }

 if( auto inner_block = get_nested_Block( 0 ) ) {
  auto inner_block_group = group.addGroup( BLOCK_NAME );
  inner_block->serialize( inner_block_group );
 }
}

/*--------------------------------------------------------------------------*/
/*-------- METHODS DESCRIBING THE BEHAVIOR OF THE InvestmentFunction -------*/
/*--------------------------------------------------------------------------*/

int InvestmentFunction::compute( bool changedvars ) {

 // First, handle the events that happens at the beginning of compute()

 for( auto & event : v_events[ eAtTheBeginning ] ) {
  auto result = event();
  switch( result ) {
   case( ThinComputeInterface::eStopOK ):
    return( kOK );
   case( ThinComputeInterface::eStopError ):
    return( kError );
  }
 }

 if( ( ! changedvars ) && f_blocks_are_updated )
  // TODO We need another flag telling whether the sub-Block has changed since
  // the last call.
  return( f_solver_status ); //  nothing changed since last call, nothing to do

 output_variable_values();

 f_has_diagonal_linearization = false;
 f_has_value = false;

 f_violated_constraint = { Inf< Index >() , eLHS };
 if( ! constraints_are_satisfied() ) { // the linear constraints are not satisfied
  f_has_value = true;
  f_value = worst_value();
  output_function_value();
  return( kOK );
 }

 if( v_Block.empty() )
  throw( std::logic_error( "InvestmentFunction::compute: there must be at "
                           "least one sub-Block, but there is none." ) );

 // For the InvestmentFunction to be correctly computed, the inner Block
 // cannot be modified by other entities. Therefore, the inner Block must be
 // locked.

 bool owned;

 // Try to lock the inner Block.
 owned = v_Block.front()->is_owned_by( f_id );
 if( ( ! owned ) && ( ! v_Block.front()->lock( f_id ) ) )
  return( kError ); // If this does not work, this is clearly an error.

 int status;
 if( get_sddp_block() )
  status = compute_SDDPBlock( changedvars , owned );
 else if( get_ucblock() )
  status = compute_UCBlock( changedvars , owned );
 else {
  if( ! owned )
   v_Block.front()->unlock( f_id );  // unlock the inner Block
  throw( std::invalid_argument( "InvestmentFunction::compute: "
                                "invalid inner Block." ) );
 }

 if( ! owned )
  v_Block.front()->unlock( f_id );  // unlock the inner Block

 return( status );
}

/*--------------------------------------------------------------------------*/

int InvestmentFunction::compute_UCBlock( bool changedvars , bool owned ) {

 // Since the inner Solver may need to lock the inner Block, the
 // InvestmentFunction lends its identity to the inner Solver.

 auto solver = get_ucblock_solver( 0 , 0 );
 assert( solver );

 void * solver_id = solver->id();
 solver->set_id( f_id );

 const auto saved_f_ignore_modifications = f_ignore_modifications;
 f_ignore_modifications = true;

 if( generator_node_map.empty() )
  build_generator_node_map();

 if( changedvars || ( ! f_blocks_are_updated ) ) {
  // Update the Blocks.

  try {
   update_blocks();
  }
  catch( const std::exception & e ) {
   // An error occurred while updating the Blocks.
   std::cout << "InvestmentFunction::compute_UCBlock(): an error occurred "
             << "while updating the Blocks: '" << e.what() << "'" << std::endl;
   f_value = worst_value();
   output_function_value();
   solver->set_id( solver_id );
   return( kError );
  }
 }

 // Optimization phase

 f_solver_status = solver->compute();

 if( ! solver->has_var_solution() ) {
  f_value = worst_value();
  output_function_value();
  solver->set_id( solver_id );
  return( kError );
 }

 f_value = solver->get_var_value();

 // Compute the linearization if required

 if( f_compute_linearization ) {
  reset_linearization();
  try {
   update_linearization( 0 );
  }
  catch( const std::exception & e ) {
   // An error occurred while updating the linearization.
   std::cout << "InvestmentFunction::compute(): an error occurred while "
             << "updating the linearization: '"
             << e.what() << "'" << std::endl;
   solver->set_id( solver_id );
   output_function_value();
   return( f_solver_status );
  }
 }

 // Possibly output the solution

 if( f_output_solution ) {
  // TODO
 }

 f_ignore_modifications = saved_f_ignore_modifications;

 // Consider the linear term of the objective

 for( Index i = 0 ; i < v_x.size() ; ++i ) {

  const auto installed_quantity = get_installed_quantity( i );
  const auto x = get_var_value( i , false );

  // Update the objective value and the linearization

  if( x > installed_quantity ) {
   // An investment is being made in asset i
   const auto cost = get_cost( i );
   f_value += cost * ( x - installed_quantity );
   v_linearization[ i ] += cost;
  }
  else {
   // A disinvestment is being made in asset i
   const auto disinvestment_cost = get_disinvestment_cost( i );
   f_value += disinvestment_cost * ( installed_quantity - x );
   v_linearization[ i ] -= disinvestment_cost;
  }
 }

 solver->set_id( solver_id );

 // At this point, if a linearization has been computed, then a diagonal
 // linearization is available.
 f_has_diagonal_linearization = f_compute_linearization;

 f_has_value = true;

 // Finally, handle the events that happens at the end of compute()

 for( auto & event : v_events[ eBeforeTermination ] ) {
  auto result = event();
  switch( result ) {
   case( ThinComputeInterface::eStopOK ):
    return( kOK );
   case( ThinComputeInterface::eStopError ):
    return( kError );
  }
 }

 output_function_value();

 return( f_solver_status );
}  // end( InvestmentFunction::compute_UCBlock )

/*--------------------------------------------------------------------------*/

int InvestmentFunction::compute_SDDPBlock( bool changedvars , bool owned ) {

 // Identify all relevant Solver of the inner Block
 build_solver_map();

 assert( v_greedy_solvers.size() == f_num_sub_blocks_per_stage );

 // Since the inner Solver may need to lock the inner Block, the
 // InvestmentFunction lends its identity to the inner Solver.

 std::vector< void * > solver_ids( v_greedy_solvers.size() + 1 , nullptr );

 for( Index i = 0 ; i < solver_ids.size() - 1 ; ++i ) {
  auto solver = get_solver( v_greedy_solvers[ i ] );
  assert( solver );
  solver_ids[ i ] = solver->id();
  solver->set_id( f_id );
 }
 if( f_sddp_solver ) {
  solver_ids.back() = f_sddp_solver->id();
  f_sddp_solver->set_id( f_id );
 }

 const auto unlend_identity = [ & ]( bool unlock_locally = false ) {
  for( Index i = 0 ; i < v_greedy_solvers.size() ; ++i ) {
   auto solver = get_solver( v_greedy_solvers[ i ] );
   assert( solver );
   solver->set_id( solver_ids[ i ] );
   if( unlock_locally )
    unlock_sub_block( i );
  }
  if( f_sddp_solver )
   f_sddp_solver->set_id( solver_ids.back() );
 };

 const auto saved_f_ignore_modifications = f_ignore_modifications;
 f_ignore_modifications = true;

 if( generator_node_map.empty() )
  build_generator_node_map();

 if( changedvars || ( ! f_blocks_are_updated ) ) {
  // Update the Blocks.

  try {
   update_blocks();
  }
  catch( const std::exception & e ) {
   // An error occurred while updating the Blocks.
   unlend_identity();
   std::cout << "InvestmentFunction::compute(): an error occurred while "
             << "updating the Blocks: '" << e.what() << "'" << std::endl;
   f_value = worst_value();
   output_function_value();
   return( kError );
  }
 }


 // Optimization phase

 if( f_sddp_solver ) {

  if( changedvars )
   get_sddp_block()->remove_cuts();

  const auto status = f_sddp_solver->compute();

  if( status != SDDPSolver::kOK ) {

   switch( status ) {
    case( SDDPSolver::kStopIter ):
     f_solver_status = SDDPSolver::kStopIter;
    case( SDDPSolver::kCurveCross ):
    case( SDDPSolver::kError ):
    default:
     f_solver_status = kError;
   }

   unlend_identity();
   return( f_solver_status );
  }
 }

#ifdef USE_MPI
 {
  boost::mpi::communicator communicator;
  if( communicator.rank() )
   return( kOK ); // Abort the computation of positive rank processes
 }
#endif

 // Simulation phase

 const auto num_scenarios = get_number_scenarios();
 f_value = 0.0;
 reset_linearization();

 f_solver_status = kUnEval;

 // This variable indicates whether the loop over the scenarios must be
 // interrupted. The loop is interrupted when either a solution for a
 // subproblem is not found or when an error occurs while updating the
 // linearization.
 bool interrupt_loop = false;

 int error_status = kError;

 auto simulation_value = decltype( f_value )( 0 );

 #pragma omp parallel for reduction( + : simulation_value )
 for( int scenario = 0 ; scenario < num_scenarios ; ++scenario ) {

  if( interrupt_loop )
   continue;

  const auto sub_block_index = lock_sub_block();
  auto solver = get_solver< SDDPGreedySolver >
   ( v_greedy_solvers[ sub_block_index ] );
  assert( solver );
  solver->set_par( SDDPGreedySolver::intScenarioId , scenario );
  // TODO
  //solver->set_par
  //( SDDPGreedySolver::intSubBlockIndex , int( sub_block_index ) );
  const auto status = solver->compute( true );

  if( ! solver->has_var_solution() ) {
   unlock_sub_block( sub_block_index );
   #pragma omp critical( InvestmentFunction )
   {
    interrupt_loop = true;
    error_status = status;
   }
   continue;
  }

  try {
   #pragma omp critical( InvestmentFunction )
   {
    f_solver_status = status;
    if( f_compute_linearization )
     update_linearization( sub_block_index );
   }
  }
  catch( const std::exception & e ) {
   // An error occurred while updating the linearization.
   std::cout << "InvestmentFunction::compute(): an error occurred while "
    "updating the linearization: '" << e.what() << "'" << std::endl;
   unlock_sub_block( sub_block_index );
   #pragma omp critical( InvestmentFunction )
   {
    error_status = kError;
    interrupt_loop = true;
   }
   continue;
  }

  // Update the function value

  simulation_value += solver->get_var_value();

  // TODO
  // Possibly output the solution

  /*
  if( f_output_solution ) {
   auto solver = get_solver( v_greedy_solvers[ sub_block_index ] );
   SDDPBlockSolutionOutput().print( get_sddp_block() ,
                                    solver->get_int_par
                                    ( SDDPGreedySolver::intSubBlockIndex ) ,
                                    scenario , true );
  }
  */

  // Unlock the sub-Block

  unlock_sub_block( sub_block_index );

 } // end( for each scenario )

 if( interrupt_loop ) {
  // The loop was interrupted due to an error. Unlock the sub-Blocks and
  // return.

  unlend_identity( true );

  f_solver_status = error_status;
  f_value = worst_value();
  output_function_value();
  return( f_solver_status );
 }

 f_value = simulation_value;

 f_ignore_modifications = saved_f_ignore_modifications;

 // Compute the expectation of the operational costs

 f_value /= num_scenarios;

 // Compute the expectation of the linearization

 for( Index i = 0 ; i < v_linearization.size() ; ++i ) {
  v_linearization[ i ] /= num_scenarios;
 }

 // Consider the linear term of the objective

 for( Index i = 0 ; i < v_x.size() ; ++i ) {

  const auto installed_quantity = get_installed_quantity( i );
  const auto x = get_var_value( i , false );

  // Update the objective value and the linearization

  if( x > installed_quantity ) {
   // An investment is being made in asset i
   const auto cost = get_cost( i );
   f_value += cost * ( x - installed_quantity );
   v_linearization[ i ] += cost;
  }
  else {
   // A disinvestment is being made in asset i
   const auto disinvestment_cost = get_disinvestment_cost( i );
   f_value += disinvestment_cost * ( installed_quantity - x );
   v_linearization[ i ] -= disinvestment_cost;
  }
 }

 // Unlock the inner Block if it is necessary
 unlend_identity();

 // At this point, if a linearization has been computed, then a diagonal
 // linearization is available.
 f_has_diagonal_linearization = f_compute_linearization;

 f_has_value = true;

 // Finally, handle the events that happens at the end of compute()

 for( auto & event : v_events[ eBeforeTermination ] ) {
  auto result = event();
  switch( result ) {
   case( ThinComputeInterface::eStopOK ):
    return( kOK );
   case( ThinComputeInterface::eStopError ):
    return( kError );
  }
 }

 output_function_value();

 return( f_solver_status );
}  // end( InvestmentFunction::compute_SDDPBlock )

/*--------------------------------------------------------------------------*/

static RealObjective::OFValue get_recours_obj( const Block * blck ) {
 RealObjective::OFValue rv = 0;
 if( auto obj = dynamic_cast< RealObjective * >( blck->get_objective() ) )
  rv = obj->get_constant_term();
 for( const auto bk : blck->get_nested_Blocks() )
  rv += get_recours_obj( bk );

 return( rv );
};

/*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

Function::FunctionValue InvestmentFunction::get_constant_term( void ) const {
 if( auto bk = get_nested_Block( 0 ) )
  return( get_recours_obj( bk ) );
 else
  return( 0 );
}

/*--------------------------------------------------------------------------*/

bool InvestmentFunction::is_convex( void ) const {
 return( true );
}

/*--------------------------------------------------------------------------*/

bool InvestmentFunction::is_concave( void ) const {
 return( false );
}

/*--------------------------------------------------------------------------*/

bool InvestmentFunction::has_linearization( const bool diagonal ) {
 if( diagonal ) {
  f_diagonal_linearization_required = true;
  return( f_has_diagonal_linearization );
 }
 else {
  f_diagonal_linearization_required = false;
  return( f_violated_constraint.first < Inf< Index >() );
 }
}  // end( InvestmentFunction::has_linearization )


/*--------------------------------------------------------------------------*/

bool InvestmentFunction::compute_new_linearization( bool diagonal ) {
 if( diagonal )
  return( false );
 return( ! constraints_are_satisfied() );
}

/*--------------------------------------------------------------------------*/

void InvestmentFunction::store_linearization( Index name , ModParam issueMod ) {
 if( name >= global_pool.size() )
  throw( std::invalid_argument( "InvestmentFunction::store_linearization: "
                                "invalid global pool name: " +
                                std::to_string( name ) ) );

 global_pool.store( get_linearization_constant() , v_linearization , name ,
                    f_diagonal_linearization_required );

 if( ( ! f_Observer ) || ( ! f_Observer->issue_mod( issueMod ) ) )
  return;

 f_Observer->add_Modification( std::make_shared< C05FunctionMod >
                               ( this , C05FunctionMod::GlobalPoolAdded ,
                                 Subset( { name } ) , 0 ,
                                 Observer::par2concern( issueMod ) ) ,
                               Observer::par2chnl( issueMod ) );

} // end InvestmentFunction::store_linearization( Index )

/*--------------------------------------------------------------------------*/

void InvestmentFunction::store_combination_of_linearizations
( c_LinearCombination & coefficients , Index name , ModParam issueMod ) {

 global_pool.store_combination_of_linearizations( coefficients , name ,
                                                  AAccMlt );

 if( ( ! f_Observer ) || ( ! f_Observer->issue_mod( issueMod ) ) )
  return;

 f_Observer->add_Modification( std::make_shared< C05FunctionMod >
                               ( this , C05FunctionMod::GlobalPoolAdded ,
                                 Subset( { name } ) , 0 ,
                                 Observer::par2concern( issueMod ) ) ,
                               Observer::par2chnl( issueMod ) );

}  // end( InvestmentFunction::store_combination_of_linearizations )

/*--------------------------------------------------------------------------*/

void InvestmentFunction::delete_linearization( const Index name ,
                                               ModParam issueMod ) {
 global_pool.delete_linearization( name );

 if( ( ! f_Observer ) || ( ! f_Observer->issue_mod( issueMod ) ) )
  return;

 f_Observer->add_Modification( std::make_shared< C05FunctionMod >
                               ( this , C05FunctionMod::GlobalPoolRemoved ,
                                 Subset( { name } ) , 0 ,
                                 Observer::par2concern( issueMod ) ) ,
                               Observer::par2chnl( issueMod ) );
}  // end( InvestmentFunction::delete_linearization )

/*--------------------------------------------------------------------------*/

void InvestmentFunction::delete_linearizations( Subset && which , bool ordered ,
                                                ModParam issueMod ) {
 global_pool.delete_linearizations( which , ordered );

 if( ( ! f_Observer ) || ( ! f_Observer->issue_mod( issueMod ) ) )
  return;

 f_Observer->add_Modification( std::make_shared< C05FunctionMod >
                               ( this , C05FunctionMod::GlobalPoolRemoved ,
                                 std::move( which ) , 0 ,
                                 Observer::par2concern( issueMod ) ) ,
                               Observer::par2chnl( issueMod ) );
}

/*--------------------------------------------------------------------------*/

void InvestmentFunction::get_linearization_coefficients
( FunctionValue * g , Range range , Index name ) {

 range.second = std::min( range.second , Index( v_x.size() ) );
 if( range.second <= range.first )
  return;

 if( name != Inf< Index >() ) {
  // Linearization from the global pool
  global_pool.get_linearization_coefficients( g , range , name );
  return;
 }

 if( f_diagonal_linearization_required ) {
  // Diagonal linearization
  for( Index i = range.first ; i < range.second ; ++i )
   g[ i - range.first ] = v_linearization[ i ];
 }
 else {
  // Vertical linearization
  assert( f_violated_constraint.first < v_A.size() );
  const double sign = ( f_violated_constraint.second == eLHS ) ? -1 : 1;
  const auto i = f_violated_constraint.first;
  for( Index j = range.first ; j < range.second ; ++j )
   g[ j - range.first ] = sign * v_A[ i ][ j ];
 }
}  // end( InvestmentFunction::get_linearization_coefficients( * , range ) )

/*--------------------------------------------------------------------------*/

void InvestmentFunction::get_linearization_coefficients
( SparseVector & g , Range range , Index name ) {

 range.second = std::min( range.second , Index( v_x.size() ) );
 if( range.second <= range.first )
  return;

 if( name != Inf< Index >() ) {
  // Linearization from the global pool
  global_pool.get_linearization_coefficients( g , range , name );
  return;
 }

 if( f_diagonal_linearization_required ) {
  // Diagonal linearization
  for( Index i = range.first ; i < range.second ; ++i )
   g.coeffRef( i ) = v_linearization[ i ];
 }
 else {
  // Vertical linearization
  assert( f_violated_constraint.first < v_A.size() );
  const double sign = ( f_violated_constraint.second == eLHS ) ? -1 : 1;
  const auto i = f_violated_constraint.first;
  for( Index j = range.first ; j < range.second ; ++j )
   g.coeffRef( j ) = sign * v_A[ i ][ j ];
 }
}  // end( InvestmentFunction::get_linearization_coefficients( sv , range ) )

/*--------------------------------------------------------------------------*/

void InvestmentFunction::get_linearization_coefficients
( FunctionValue * g , c_Subset & subset , const bool ordered , Index name ) {

 if( name != Inf< Index >() ) {
  // Linearization from the global pool
  global_pool.get_linearization_coefficients( g , subset , ordered , name );
  return;
 }

 if( f_diagonal_linearization_required ) {
  // Diagonal linearization
  Index k = 0;
  for( auto i : subset )
   g[ k++ ] = v_linearization[ i ];
 }
 else {
  // Vertical linearization
  assert( f_violated_constraint.first < v_A.size() );
  const double sign = ( f_violated_constraint.second == eLHS ) ? -1 : 1;
  const auto i = f_violated_constraint.first;
  Index k = 0;
  for( auto j : subset )
   g[ k++ ] = sign * v_A[ i ][ j ];
 }
}  // end( InvestmentFunction::get_linearization_coefficients( * , subset ) )

/*--------------------------------------------------------------------------*/

void InvestmentFunction::get_linearization_coefficients
( SparseVector & g , c_Subset & subset , const bool ordered , Index name ) {

 if( name != Inf< Index >() ) {
  // Linearization from the global pool
  global_pool.get_linearization_coefficients( g , subset , ordered , name );
  return;
 }

 if( f_diagonal_linearization_required ) {
  // Diagonal linearization
  for( auto i : subset )
   g.coeffRef( i ) = v_linearization[ i ];
 }
 else {
  // Vertical linearization
  assert( f_violated_constraint.first < v_A.size() );
  const double sign = ( f_violated_constraint.second == eLHS ) ? -1 : 1;
  const auto i = f_violated_constraint.first;
  for( auto j : subset )
   g.coeffRef( j ) = sign * v_A[ i ][ j ];
 }
}  // end( InvestmentFunction::get_linearization_coefficients( sv, subset ) )

/*--------------------------------------------------------------------------*/

Function::FunctionValue
InvestmentFunction::get_linearization_constant( Index name ) {

 if( name == Inf<Index>() ) {
  // Linearization just computed and not in the global pool yet.

  if( f_diagonal_linearization_required ) {
   // Diagonal linearization
   auto alpha = f_value;
   for( Index i = 0 ; i < v_linearization.size() ; ++i ) {
    alpha -= v_linearization[ i ] * get_var_value( i );
   }

   return( alpha );
  }
  else {
   // Vertical linearization
   assert( f_violated_constraint.first < v_A.size() );
   const auto i = f_violated_constraint.first;
   double alpha = 0;
   if( f_reformulated_bounds ) {
    for( Index j = 0 ; j < v_A[ i ].size() ; ++j )
     if( ( j < v_lower_bound.size() )
         && ( v_lower_bound[ j ] > -Inf< double >() ) )
      alpha += v_A[ i ][ j ] * v_lower_bound[ j ];
   }

   if( f_violated_constraint.second == eLHS )
    alpha = v_constraints_lower_bound[ i ] - alpha;
   else
    alpha = alpha - v_constraints_upper_bound[ i ];

   return( alpha );
  }
 }
 else {
  // Linearization from the global pool
  return( global_pool.get_linearization_constant( name ) );
 }

 return( 0 );
}  // end( InvestmentFunction::get_linearization_constant )

/*--------------------------------------------------------------------------*/

Function::FunctionValue InvestmentFunction::get_value( void ) const {
 if( f_has_value )
  return( f_value );
 return( worst_value() );
} // end ( InvestmentFunction::get_value )

/*--------------------------------------------------------------------------*/

double InvestmentFunction::compute_linear_constraint_value( Index i ) const {
 double value = 0;
 for( Index j = 0 ; j < v_A[ i ].size() ; ++j )
  value += v_A[ i ][ j ] * get_var_value( j , false );
 return( value );
}

/*--------------------------------------------------------------------------*/

bool InvestmentFunction::constraints_are_satisfied( void ) {

 Index start = 0;
 if( f_violated_constraint.first < Inf< Index >() )
  start = f_violated_constraint.first + 1;

 for( Index i = start ; i < v_A.size() ; ++i ) {
  auto constraint_value = compute_linear_constraint_value( i );
  if( constraint_value < v_constraints_lower_bound[ i ] ) {
   f_violated_constraint = { i , eLHS };
   return( false );
  }

  if( constraint_value > v_constraints_upper_bound[ i ] ) {
   f_violated_constraint = { i , eRHS };
   return( false );
  }
 }

 return( true );
} // end ( InvestmentFunction::constraints_are_satisfied )

/*--------------------------------------------------------------------------*/
/*-------------------- Methods for handling Modification -------------------*/
/*--------------------------------------------------------------------------*/

void InvestmentFunction::add_Modification( sp_Mod mod ,
                                           Observer::ChnlName chnl ) {
 if( f_ignore_modifications )
  return;
 send_nuclear_modification( chnl );

}  // end( InvestmentFunction::add_Modification )

/*--------------------------------------------------------------------------*/
/*---------------- PRIVATE METHODS OF THE InvestmentFunction ---------------*/
/*--------------------------------------------------------------------------*/

int InvestmentFunction::get_inner_block_objective_sense() const {
 auto inner_block = get_ucblock( 0 , 0 );
 assert( inner_block );
 return( inner_block->get_objective_sense() );
}

/*--------------------------------------------------------------------------*/

UCBlock * InvestmentFunction::get_ucblock( Index stage , Index i ) const {
 if( auto ucblock = get_ucblock() )
  return( ucblock );
 assert( i < f_num_sub_blocks_per_stage );
 auto benders_function = get_benders_function( stage , i );
 assert( benders_function );
 return( dynamic_cast< UCBlock * >( benders_function->get_inner_block() ) );
}

/*--------------------------------------------------------------------------*/

UCBlock * InvestmentFunction::get_ucblock() const {
 assert( ! v_Block.empty() );
 return( dynamic_cast< UCBlock * >( v_Block.front() ) );
}

/*--------------------------------------------------------------------------*/

SDDPBlock * InvestmentFunction::get_sddp_block() const {
 assert( ! v_Block.empty() );
 return( dynamic_cast< SDDPBlock * >( v_Block.front() ) );
}

/*--------------------------------------------------------------------------*/

CDASolver * InvestmentFunction::get_ucblock_solver( Index stage ,
                                                    Index i ) const {
 if( auto ucblock = get_ucblock( stage , i ) )
  if( ! ucblock->get_registered_solvers().empty() )
   return( dynamic_cast< CDASolver * >
           ( ucblock->get_registered_solvers().front() ) );
 return( nullptr );
}

/*--------------------------------------------------------------------------*/

BendersBFunction *
InvestmentFunction::get_benders_function( Index stage ,
                                          Index sub_block_index ) const {

 auto sddp_block = get_sddp_block();
 assert( sddp_block );
 if( stage >= sddp_block->get_time_horizon() )
  throw( std::invalid_argument( "InvestmentFunction::get_benders_function: "
                                "invalid stage index: " +
                                std::to_string( stage ) ) );

 auto benders_block = static_cast< BendersBlock * >
  ( sddp_block->get_sub_Block( stage , sub_block_index )->get_inner_block() );

 auto objective = static_cast< FRealObjective * >
  ( benders_block->get_objective() );

 return( static_cast< BendersBFunction * >( objective->get_function() ) );
}

/*--------------------------------------------------------------------------*/

void InvestmentFunction::reset_linearization() {
 v_linearization.assign( v_x.size() , 0 );
}

/*--------------------------------------------------------------------------*/

Index InvestmentFunction::get_node( Index stage , Index block_index ,
                                    Index generator ) const {
 // i is between 0 and the number of UnitBlock assets - 1.
 const auto i = v_block_indices_map[ block_index ];
 return( generator_node_map[ stage ][ i ][ generator ] );
}

/*--------------------------------------------------------------------------*/

void InvestmentFunction::build_generator_node_map() {

 // The indices of the UnitBlocks
 std::vector< Index > block_indices;
 block_indices.reserve( v_asset_indices.size() );

 for( Index i = 0 ; i < v_asset_indices.size() ; ++i ) {
  if( v_asset_type[ i ] == eUnitBlock )
   block_indices.push_back( v_asset_indices[ i ] );
 }

 if( block_indices.empty() )
  return;

 v_block_indices_map.resize
  ( 1 + * std::max_element( block_indices.cbegin() , block_indices.cend() ) ,
    Inf< Index >() );
 for( Index i = 0 ; i < block_indices.size() ; ++i ) {
  v_block_indices_map[ block_indices[ i ] ] = i;
 }

 const auto num_stages = get_number_stages();

 generator_node_map.resize( num_stages );

 for( Index stage = 0 ; stage < num_stages ; ++stage ) {

  generator_node_map[ stage ].resize( block_indices.size() );

  const auto ucblock = get_ucblock( stage , 0 );
  const auto network_data = ucblock->get_NetworkData();
  const auto number_nodes = network_data ? network_data->get_number_nodes() : 1;

  if( number_nodes <= 1 ) {
   // Since there is only one node, all generators belong to the same node
   // (node 0).
   for( Index i = 0 ; i < block_indices.size() ; ++i ) {
    const auto unit_block = ucblock->get_unit_block( block_indices[ i ] );
    const auto num_generators = unit_block->get_number_generators();
    generator_node_map[ stage ][ i ].resize( num_generators , 0 );
   }
   continue;
  }

  // There are multiple nodes.

  const auto number_units = ucblock->get_number_units();
  const auto & generator_node = ucblock->get_generator_node();

  for( Index node_id = 0 ; node_id < number_nodes ; ++node_id ) {

   Index elc_generator = 0;
   for( Index unit_id = 0 ; unit_id < number_units ; ++unit_id ) {

    const auto unit_block = ucblock->get_unit_block( unit_id );
    const auto num_generators = unit_block->get_number_generators();

    auto it = std::find( block_indices.cbegin() ,
                         block_indices.cend() , unit_id );

    const auto index = std::distance( block_indices.cbegin() , it );

    if( index == block_indices.size() ) {
     // This UnitBlock is not subject to investment.
     elc_generator += num_generators;
     continue;
    }

    generator_node_map[ stage ][ index ].resize( num_generators );

    for( Index generator = 0 ; generator < num_generators ;
         ++generator , ++elc_generator ) {
     generator_node_map[ stage ][ index ][ generator ] =
      generator_node[ elc_generator ];
    }
   } // end( for each UnitBlock )
  } // end( for each node )
 } // end( for each stage )
} // end( InvestmentFunction::build_generator_node_map )

/*--------------------------------------------------------------------------*/

double InvestmentFunction::compute_scale_linearization
( Index block_index , Index stage , Index sub_block_index ) {

 /* TODO The following code does not take into account the pollutant budget
  * constraints and the heat constraints. When these constraints are correctly
  * implemented, this function must be updated. */

 const auto ucblock = get_ucblock( stage , sub_block_index );
 const auto network_data = ucblock->get_NetworkData();
 const auto number_nodes = network_data ? network_data->get_number_nodes() : 1;
 const auto time_horizon = ucblock->get_time_horizon();

 const auto block = ucblock->get_unit_block( block_index );

 // This is the contribution to the linearization associated with this
 // UnitBlock.
 double linearization = 0;

 // Add the contribution associated with the node injection constraints

 const auto & node_injection_constraints =
  ucblock->get_node_injection_constraints();

 for( Index t = 0 ; t < time_horizon ; ++t ) {

  for( Index g = 0 ; g < block->get_number_generators() ; ++g ) {

   const auto node = get_node( stage , block_index , g );

   const auto & constraint = node_injection_constraints[ t ][ node ];
   const auto function = constraint.get_function();

   const auto dual = constraint.get_dual();
   const auto & active_power = block->get_active_power( g )[ t ];
   linearization += dual * active_power.get_value();

   assert( active_power.is_active( &constraint ) < Inf< Index >() );
   assert( function->is_active( &active_power ) < Inf< Index >() );

   if( auto fc = block->get_fixed_consumption( g ) ) {
    if( auto u = block->get_commitment( g ) ) {
     const auto commitment = u[ t ].get_value();
     const auto fixed_consumption = fc[ t ];
     linearization += dual * fixed_consumption * ( 1.0 - commitment );

     assert( u[ t ].is_active( &constraint ) );
     assert( function->is_active( &u[ t ] ) );
    }
   }

  } // end( for each generator )
 } // end( for each time instant )

 // Add the contribution associated with the primary demand constraints.

 const auto & primary_demand_constraints =
  ucblock->get_primary_demand_constraints();

 if( ! primary_demand_constraints.empty() ) {

  const auto number_primary_zones = ucblock->get_number_primary_zones();

  for( Index t = 0 ; t < time_horizon ; ++t ) {
   for( Index zone_id = 0 ; zone_id < number_primary_zones ; ++zone_id ) {
    for( Index node_id = 0 ; node_id < number_nodes ; ++node_id ) {
     if( ! ucblock->node_belongs_to_primary_zone( node_id , zone_id ) )
      continue;

     // Compute the index of the first electrical generator of the current
     // UnitBlock.
     Index elc_generator = 0;
     for( Index unit_id = 0 ; unit_id < block_index ; ++unit_id ) {
      const auto unit_block = ucblock->get_unit_block( unit_id );
      elc_generator += unit_block->get_number_generators();
     }

     const auto num_generators = block->get_number_generators();

     for( Index generator = 0 ; generator < num_generators ;
          ++generator , ++elc_generator ) {

      if( ! ucblock->generator_belongs_to_node( elc_generator , node_id ) )
       continue;

      if( const auto primary_s_r =
          block->get_primary_spinning_reserve( generator ) ) {

       const auto primary_spinning_reserve = & primary_s_r[ t ];
       const auto dual = primary_demand_constraints[ t ][ zone_id ].get_dual();
       linearization += dual * primary_spinning_reserve->get_value();
      }

     } // end( for each generator )
    } // end( for each node )
   } // end( for each zone )
  } // end( for each time instant )

 } // end( non-empty primary demand constraints )

 // Add the contribution associated with the secondary demand constraints.

 const auto & secondary_demand_constraints =
  ucblock->get_secondary_demand_constraints();

 if( ! secondary_demand_constraints.empty() ) {

  const auto number_secondary_zones = ucblock->get_number_secondary_zones();

  for( Index t = 0 ; t < time_horizon ; ++t ) {
   for( Index zone_id = 0 ; zone_id < number_secondary_zones ; ++zone_id ) {
    for( Index node_id = 0 ; node_id < number_nodes ; ++node_id ) {
     if( ! ucblock->node_belongs_to_secondary_zone( node_id , zone_id ) )
      continue;

     // Compute the index of the first electrical generator of the current
     // UnitBlock.
     Index elc_generator = 0;
     for( Index unit_id = 0 ; unit_id < block_index ; ++unit_id ) {
      const auto unit_block = ucblock->get_unit_block( unit_id );
      elc_generator += unit_block->get_number_generators();
     }

     const auto num_generators = block->get_number_generators();

     for( Index generator = 0 ; generator < num_generators ;
          ++generator , ++elc_generator ) {

      if( ! ucblock->generator_belongs_to_node( elc_generator , node_id ) )
       continue;

      if( const auto secondary_s_r =
          block->get_secondary_spinning_reserve( generator ) ) {

       const auto secondary_spinning_reserve = & secondary_s_r[ t ];
       const auto dual = secondary_demand_constraints[ t ][ zone_id ].get_dual();
       linearization += dual * secondary_spinning_reserve->get_value();
      }

     } // end( for each generator )
    } // end( for each node )
   } // end( for each zone )
  } // end( for each time instant )

 } // end( non-empty secondary demand constraints )

 // Add the contribution associated with the inertia demand constraints.

 const auto & inertia_demand_constraints =
  ucblock->get_inertia_demand_constraints();

 if( ! inertia_demand_constraints.empty() ) {

  const auto number_inertia_zones = ucblock->get_number_inertia_zones();

  for( Index t = 0 ; t < time_horizon ; ++t ) {
   for( Index zone_id = 0 ; zone_id < number_inertia_zones ; ++zone_id ) {
    for( Index node_id = 0 ; node_id < number_nodes ; ++node_id ) {
     if( ! ucblock->node_belongs_to_inertia_zone( node_id , zone_id ) )
      continue;

     // Compute the index of the first electrical generator of the current
     // UnitBlock.
     Index elc_generator = 0;
     for( Index unit_id = 0 ; unit_id < block_index ; ++unit_id ) {
      const auto unit_block = ucblock->get_unit_block( unit_id );
      elc_generator += unit_block->get_number_generators();
     }

     const auto num_generators = block->get_number_generators();

     for( Index generator = 0 ; generator < num_generators ;
          ++generator , ++elc_generator ) {

      if( ! ucblock->generator_belongs_to_node( elc_generator , node_id ) )
       continue;

      const auto dual = inertia_demand_constraints[ t ][ zone_id ].get_dual();

      // Commitment variable

      auto commitment = block->get_commitment( generator );
      auto inertia_commitment = block->get_inertia_commitment( generator );

      if( commitment && inertia_commitment ) {
       const auto commitment_t = & commitment[ t ];
       linearization +=
        dual * inertia_commitment[ t ] * commitment_t->get_value();
      }

      // Active power variable

      auto active_power = block->get_active_power( generator );
      auto inertia_power = block->get_inertia_power( generator );

      if( active_power && inertia_power ) {
       auto active_power_t = & active_power[ t ];
       linearization += dual * inertia_power[ t ] * active_power_t->get_value();
      }

     } // end( for each generator )
    } // end( for each node )
   } // end( for each zone )
  } // end( for each time instant )

 } // end( non-empty inertia demand constraints )

 /* Finally, add the contribution associated with the objective function (if
  * any) of the UnitBlock.
  *
  * The objective function of a UnitBlock may have the form k*f(x), where k is
  * the scale factor. The contribution associated with the objective to the
  * linearization is therefore f(x). If k is non-zero, f(x) can be retrieved
  * by simply computing the objective and then dividing its value by k. If k
  * is zero, then we can temporarily scale the UnitBlock to 1, evaluate the
  * objective (whose value must then be f(x)), and finally scale the UnitBlock
  * back to its original scale factor. */

 if( auto objective =
     dynamic_cast< FRealObjective * >( block->get_objective() ) ) {

  const auto scale = block->get_scale();

  if( scale != 0 ) {
   objective->compute();
   linearization += objective->value() / scale;
  }
  else {
   /* Scale the UnitBlock to 1 so that we can retrieve the value of the
    * objective associated with a single representative unit. No Modification
    * should be issued since the UnitBlock will be scaled back to the original
    * scale factor after the objective is computed. */
   block->scale( 1.0 , eNoMod , eNoMod );

   // Compute the Objective and retrieve its value.
   objective->compute();
   linearization += objective->value();

   // Scale the UnitBlock to its original scale factor.
   block->scale( scale , eNoMod , eNoMod );

   // Recompute the objective to take into account its original scale factor.
   objective->compute();
  }
 }

 return( linearization );
} // end( InvestmentFunction::compute_scale_linearization )

/*--------------------------------------------------------------------------*/

double InvestmentFunction::compute_kappa_linearization
( IntermittentUnitBlock * intermittent_unit , Index var_index ) {

 /* The kappa constant associated with an IntermittentUnitBlock appears in the
  * following constraints for each time instant t:
  *
  * - The lower and upper bound constraints on the active power:
  *
  *   kappa * P^{mn}_{t} <= p^{ac}_{t} <= kappa * P^{mx}_{t}
  *
  * - The minimum total amount of power produced by the unit:
  *
  *   kappa * P^{mn}_{t} <= p^{ac}_{t} - p^{pr}_{t} - p^{sc}_{t}
  *
  * - The maximum total amount of power produced by the unit:
  *
  *   gamma * p^{ac}_{t} + p^{pr}_{t} + p^{sc}_{t} <= gamma * kappa * P^{mx}_{t}
  *
  * By letting lambda_min and lambda_max be the dual variables associated with
  * the lower and upper bound constraints on the active power, respectively,
  * and alpha_min and alpha_max be the dual variables associated with the
  * minimum and maximum total amount of power produced by the unit,
  * respectively, the linearization coefficient for the variable associated
  * with the IntermittentUnitBlock is
  *
  *   P^{mn} ' (lambda_min + alpha_min) -
  *   P^{mx} ' (lambda_max + gamma * alpha_max).
  */

 double linearization = 0;

 const auto gamma = intermittent_unit->get_gamma();

 // Minimum and maximum total power constraints

 const auto & min_power_constraints =
  intermittent_unit->get_min_power_constraints();

 const auto & max_power_constraints =
  intermittent_unit->get_max_power_constraints();

 // Lower and upper bound constraints on the active power

 const auto & active_power_bound_constraints =
  intermittent_unit->get_active_power_bound_constraints();

 // Lower bound on the kappa variable

 const auto var_lower_bound = get_var_lower_bound( var_index );

 /* The dual value of the bound constraint on the active power is associated
  * with either the lower bound or the upper bound constraint. This will help
  * determine to which bound the dual is associated with. */
 const auto obj_sign =
  ( intermittent_unit->get_objective_sense() == Objective::eMin ) ? - 1 : 1;

 const auto time_horizon = intermittent_unit->get_time_horizon();

 for( Index t = 0 ; t < time_horizon ; ++t ) {

  const auto min_power = intermittent_unit->get_min_power( t );
  const auto max_power = intermittent_unit->get_max_power( t );

  // Bound constraints on the active power

  if( ! active_power_bound_constraints.empty() ) {
   const auto dual = active_power_bound_constraints[ t ].get_dual();

   // Now determine which bound is associated with the dual value

   double bound = 0;
   if( obj_sign * dual > 0 )
    // The dual is associated with the lower bound constraint
    bound = min_power;
   else
    // The dual is associated with the upper bound constraint
    bound = max_power;

   linearization += - dual * bound;
  } // end( ! active_power_bound_constraints.empty() )

  // Minimum and maximum total power constraints

  double alpha_min = 0;
  if( ! min_power_constraints.empty() )
   alpha_min = std::abs( min_power_constraints[ t ].get_dual() );

  double alpha_max = 0;
  if( ! max_power_constraints.empty() )
   alpha_max = std::abs( max_power_constraints[ t ].get_dual() );

  // Finally, update the linearization

  // Update the linearization coefficient

  linearization +=
   min_power * ( alpha_min ) - max_power * ( gamma * alpha_max );
 }

 return( linearization );
}

/*--------------------------------------------------------------------------*/

double InvestmentFunction::compute_kappa_linearization
( const BatteryUnitBlock * unit , Index var_index ) {

 /* The kappa constant associated with a BatteryUnitBlock appears in the
  * following constraints for each time instant t:
  *
  * - Minimum and maximum power output constraint (lambda):
  *
  *   kappa * P^{min}_{t} <= p^{ac}_{t} - p^{pr}_{t} - p^{sc}_{t}  [lambda_min]
  *
  *   p^{ac}_{t} + p^{pr}_{t} + p^{sc}_{t} <= kappa * P^{max}_{t}  [lambda_max]
  *
  * - Intake and outtake level bounds (alpha):
  *
  *   p^{+}_{t} <= kappa * P^{max}_{t}                             [alpha_max]
  *
  *   p^{+}_{t} <= kappa * u^{+}_t * P^{max}_{t}                   [alpha_max_u]
  *
  *   p^{-}_{t} <= - kappa * (1 - u^{+}_t) * P^{min}_{t}           [alpha_min_u]
  *
  * - Storage level bounds (beta):
  *
  *   kappa * V^{min}_t <= v_t                                     [beta_min]
  *
  *   v_t <= kappa * V^{max}_t                                     [beta_max]
  *
  * - Primary and secondary reserves bounds (gamma):
  *
  *   p^{pr}_t <= kappa P^{pr max}_t                               [gamma_pr]
  *
  *   p^{sc}_t <= kappa P^{sc max}_t                               [gamma_sc]
  *
  * The name between [] represents the dual variable associated with each
  * constraint. The linearization coefficient for the investment variable
  * associated with the BatteryUnitBlock is
  *
  *   P^{min} ' (lambda_min + (1 - u^+) * alpha_min_u) -
  *   P^{max} ' (lambda_max + alpha_max + u^+ * alpha_max_u) +
  *   V^{min} ' beta_min - V^{max} ' beta_max -
  *   P^{pr max} ' gamma_pr - P^{sc max} ' gamma_sc
  */

 double linearization = 0;

 // Minimum and maximum power output constraint

 const auto & min_power_constraints = unit->get_min_power_constraints();

 const auto & max_power_constraints = unit->get_max_power_constraints();

 // Intake and outtake level bounds

 const auto & intake_bound_constraints = unit->get_max_intake_bounds();

 const auto & max_intake_binary_constraints =
  unit->get_max_intake_binary_constraints();

 const auto & max_outtake_binary_constraints =
  unit->get_max_outtake_binary_constraints();

 const auto & u = unit->get_intake_outtake_binary_variables();

 // Storage level bounds

 const auto & storage_level_bound_constraints =
  unit->get_storage_level_bounds();

 // Primary and secondary reserves bounds

 const auto & primary_reserve_bounds = unit->get_primary_reserve_bounds();

 const auto & secondary_reserve_bounds = unit->get_secondary_reserve_bounds();

 /* The dual value of a constraint that has both finite lower and upper bounds
  * is associated with either the lower bound or the upper bound
  * constraint. This will help determine to which bound the dual is associated
  * with. */
 const auto obj_sign =
  ( unit->get_objective_sense() == Objective::eMin ) ? - 1 : 1;

 const auto time_horizon = unit->get_time_horizon();

 for( Index t = 0 ; t < time_horizon ; ++t ) {

  const auto min_power = unit->get_min_power( t );
  const auto max_power = unit->get_max_power( t );
  const auto min_storage = unit->get_min_storage()[ t ];
  const auto max_storage = unit->get_max_storage()[ t ];

  // Minimum and maximum power output constraint

  const auto lambda_min = std::abs( min_power_constraints[ t ].get_dual() );
  const auto lambda_max = std::abs( max_power_constraints[ t ].get_dual() );

  linearization += min_power * lambda_min - max_power * lambda_max;

  // Intake and outtake level bounds

  if( intake_bound_constraints ) {
   const auto dual = intake_bound_constraints[ t ].get_dual();

   // Now determine which bound is associated with the dual value

   double bound = 0;
   if( obj_sign * dual < 0 )
    // The dual is associated with the upper bound constraint
    bound = max_power;

   linearization += - dual * bound;
  }

  if( max_intake_binary_constraints ) {
   const auto alpha_max_u =
    std::abs( max_intake_binary_constraints[ t ].get_dual() );
   linearization += - alpha_max_u * u[ t ].get_value() * max_power;
  }

  if( max_outtake_binary_constraints ) {
   const auto alpha_min_u =
    std::abs( max_outtake_binary_constraints[ t ].get_dual() );
   linearization += ( 1.0 - u[ t ].get_value() ) * alpha_min_u * min_power;
  }

  // Storage level bounds

  const auto dual = storage_level_bound_constraints[ t ].get_dual();
  auto bound = max_storage;
  if( obj_sign * dual > 0 ) {
   // The bound is associated with the lower bound constraint
   bound = min_storage;
  }

  // The bound is associated with the upper bound constraint.
  linearization += - dual * bound;

  // Primary and secondary reserves bounds

  if( ! primary_reserve_bounds.empty() ) {
   const auto gamma_pr = std::abs( primary_reserve_bounds[ t ].get_dual() );
   linearization += - unit->get_max_primary_power()[ t ] * gamma_pr;
  }

  if( ! secondary_reserve_bounds.empty() ) {
   const auto gamma_sc = std::abs( secondary_reserve_bounds[ t ].get_dual() );
   linearization += - unit->get_max_secondary_power()[ t ] * gamma_sc;
  }

 }

 return( linearization );
}

/*--------------------------------------------------------------------------*/

void InvestmentFunction::update_linearization_unit_blocks
( Index stage , Index sub_block_index ,
  const std::vector< std::pair< Index , Index > > & block_indices ) {

 /* The UnitBlocks that are subject to investment can be divided into two
  * groups, depending on how the investment is represented.
  *
  * The first group is formed by the UnitBlocks whose scale factors represent
  * the investment. These are the ThermalUnitBlock and the
  * BatteryUnitBlock. For these UnitBlocks, the linearization is impacted by
  * their objective function (as they are scaled) and the linking constraints
  * in the UCBlock.
  *
  * The second group is formed by the UnitBlocks whose kappa constants
  * represent the investment. These are the IntermittentUnitBlocks. For these
  * UnitBlocks, the linearization is impacted only by the constraints in which
  * the kappa constants appear, which are the constraints defined by
  * themselves.
  */

 const auto ucblock = get_ucblock( stage , sub_block_index );

 for( const auto & [ block_index , var_index ] : block_indices ) {

  auto block = ucblock->get_unit_block( block_index );

  if( dynamic_cast< const ThermalUnitBlock * >( block ) ) {
   v_linearization[ var_index ] +=
    compute_scale_linearization( block_index , stage , sub_block_index );
  }
  else if( auto unit = dynamic_cast< BatteryUnitBlock * >( block ) ) {
   if( f_replicate_battery )
    v_linearization[ var_index ] +=
     compute_scale_linearization( block_index , stage , sub_block_index );
   else
    v_linearization[ var_index ] +=
     compute_kappa_linearization( unit , var_index );
  }
  else if( auto unit = dynamic_cast< IntermittentUnitBlock * >( block ) ) {
   if( f_replicate_intermittent )
    v_linearization[ var_index ] +=
     compute_scale_linearization( block_index , stage , sub_block_index );
   else
    v_linearization[ var_index ] +=
     compute_kappa_linearization( unit , var_index );
  }
  else {
   // Unrecognized Block
   auto error_message = "InvestmentFunction::update_linearization: "
    "unrecognized UnitBlock: " + block->classname();
   if( ! block->name().empty() )
    error_message += " with name '" + block->name() + "'";
   error_message += ".";
   throw( std::logic_error( error_message ) );
  }
 } // end( for each UnitBlock )
} // end( InvestmentFunction::update_linearization_unit_blocks )

/*--------------------------------------------------------------------------*/

void InvestmentFunction::update_linearization_network_blocks
( Index stage , Index sub_block_index ,
  const std::vector< std::pair< Index , Index > > & line_indices ) {

 // Update the linearization with respect to the lines

 if( line_indices.empty() )
  // There is no investment in lines, so there is nothing to be done.
  return;

 const auto ucblock = get_ucblock( stage , sub_block_index );
 const auto time_horizon = ucblock->get_time_horizon();

 for( Index t = 0 ; t < time_horizon ; ++t ) {

  const auto network_block = ucblock->get_network_block( t );

  if( const auto dc_network =
      dynamic_cast< const DCNetworkBlock * >( network_block ) ) {

   // HVDC lines

   const auto network_data = dynamic_cast< DCNetworkBlock::DCNetworkData * >
    ( ucblock->get_NetworkData() );
   assert( ( ! network_data ) ||
           network_data->get_lines_type() == DCNetworkBlock::kHVDC );

   const auto & constraints = dc_network->get_power_flow_limit_HVDC_bounds();

   if( constraints.empty() )
    continue;

   /* For each line l, the flow limit constraints on that line are:
    *
    *     kappa_l * Pmin_l <= power_flow_l <= kappa_l * Pmax_l
    *
    * where power_flow_l is the power flow on the line l and Pmin_l and Pmax_l
    * are the minimum and maximum power flow on the line l, respectively. */

   /* The dual value of the flow limit constraint on the power flow is
    * associated with either the lower bound or the upper bound
    * constraint. This will help determine to which bound the dual is
    * associated with. */
   const auto obj_sign =
    ( dc_network->get_objective_sense() == Objective::eMin ) ? - 1 : 1;

   for( const auto & [ line , var_index ] : line_indices ) {

    const auto dual = constraints[ line ].get_dual();
    const auto min_flow = dc_network->get_min_power_flow( line );
    const auto max_flow = dc_network->get_max_power_flow( line );

    auto bound = max_flow;
    if( obj_sign * dual > 0 )
     // The dual value is associated with the lower bound constraint.
     bound = min_flow;

    // Finally, update the linearization.

    v_linearization[ var_index ] += - dual * bound;
   } // end( for each line )
  } // end( dynamic_cast< const DCNetworkBlock * > )
  else {
   // Unrecognized NetworkBlock
   auto error_message = "InvestmentFunction::update_linearization_network_"
    "blocks: unrecognized NetworkBlock: " + network_block->classname() + ".";
   throw( std::logic_error( error_message ) );
  }
 } // end( for each time instant )

} // end( InvestmentFunction::update_linearization_network_blocks )

/*--------------------------------------------------------------------------*/

void InvestmentFunction::update_linearization( Index sub_block_index ) {

 const auto num_stages = get_number_stages();

 // The indices of the UnitBlocks and the indices of their variables
 std::vector< std::pair< Index , Index > > block_indices;
 block_indices.reserve( v_asset_indices.size() );

 // The indices of the transmission lines and the indices of their variables
 std::vector< std::pair< Index , Index > > line_indices;
 line_indices.reserve( v_asset_indices.size() );

 for( Index i = 0 ; i < v_asset_indices.size() ; ++i ) {

  const auto asset_type = v_asset_type[ i ];
  const auto asset_index = v_asset_indices[ i ];

  if( asset_type == eUnitBlock ) {
   block_indices.push_back( { asset_index , i } );
  }
  else if( asset_type == eLine ) {
   line_indices.push_back( { asset_index , i } );
  }
  else {
   throw( std::logic_error( "InvestmentFunction::update_linearization: invalid"
                            " asset type: " + std::to_string( asset_type ) ) );
  }
 } // end( for each asset )

 CDASolver * solver = nullptr;

 if( get_sddp_block() )
  solver = get_solver< CDASolver >( v_greedy_solvers[ sub_block_index ] );
 else
  solver = dynamic_cast< CDASolver * >
   ( get_ucblock()->get_registered_solvers().front() );

 // Retrieve the dual solution

 if( solver && solver->has_dual_solution() )
  solver->get_dual_solution(); // TODO pass Configuration
 else
  throw( std::logic_error( "InvestmentFunction::update_linearization: "
                           "dual solution not available." ) );

 // Retrieve the primal solution.

 if( ! block_indices.empty() ) {
  // The primal solution may only be necessary if there are UnitBlocks
  // subject to investment.
  if( solver && solver->has_var_solution() )
   solver->get_var_solution(); // TODO pass Configuration
  else
   throw( std::logic_error( "InvestmentFunction::update_linearization: "
                            "primal solution not available." ) );
 }

 for( Index stage = 0 ; stage < num_stages ; ++stage ) {
  update_linearization_unit_blocks( stage , sub_block_index , block_indices );
  update_linearization_network_blocks( stage , sub_block_index , line_indices );
 } // end( for each stage )

}  // end( InvestmentFunction::update_linearization() )

/*--------------------------------------------------------------------------*/

void InvestmentFunction::update_unit_block( UnitBlock * block ,
                                            double investment ) {
 if( dynamic_cast< const ThermalUnitBlock * >( block ) ) {
  block->scale( investment );
 }
 else if( auto unit = dynamic_cast< BatteryUnitBlock * >( block ) ) {
  if( f_replicate_battery )
   unit->scale( investment );
  else
   unit->set_kappa( investment );
 }
 else if( auto unit = dynamic_cast< IntermittentUnitBlock * >( block ) ) {
  if( f_replicate_intermittent )
   unit->scale( investment );
  else
   unit->set_kappa( investment );
 }
 else {
  // Unrecognized UnitBlock
  auto error_message = "InvestmentFunction::update_unit_block: "
   "unrecognized UnitBlock: " + block->classname();
  if( ! block->name().empty() )
   error_message += " with name '" + block->name() + "'";
  error_message += ".";
  throw( std::logic_error( error_message ) );
 }
}

/*--------------------------------------------------------------------------*/

void InvestmentFunction::update_unit_blocks
( Index sub_block_index ,
  const std::vector< Index > & block_indices ,
  const std::vector< double > & investment ) {

 assert( block_indices.size() == investment.size() );

 if( block_indices.empty() )
  return;

 const auto num_stages = get_number_stages();

 for( Index stage = 0 ; stage < num_stages ; ++stage ) {
  auto ucblock = get_ucblock( stage , sub_block_index );
  for( Index i = 0 ; i < block_indices.size() ; ++i ) {
   auto block = ucblock->get_unit_block( block_indices[ i ] );
   update_unit_block( block , investment[ i ] );
  } // end( for each UnitBlock )
 } // end( for each stage )
} // end( InvestmentFunction::update_unit_blocks )

/*--------------------------------------------------------------------------*/

void InvestmentFunction::update_network_blocks
( Index sub_block_index , const std::vector< Index > & line_indices ,
  const std::vector< double > & investment ) {

 assert( line_indices.size() == investment.size() );

 if( line_indices.empty() )
  return;

 const auto num_stages = get_number_stages();

 for( Index stage = 0 ; stage < num_stages ; ++stage ) {

  auto ucblock = get_ucblock( stage , sub_block_index );
  const auto time_horizon = ucblock->get_time_horizon();

  for( Index t = 0 ; t < time_horizon ; ++t ) {

   auto network_block = ucblock->get_network_block( t );

   if( auto dc_network = dynamic_cast< DCNetworkBlock * >( network_block ) ) {
    auto subset = line_indices;
    dc_network->set_kappa( investment.cbegin() , std::move( subset ) );
   }
   else {
    // Unrecognized NetworkBlock
    auto error_message = "InvestmentFunction::update_network_blocks: "
     "unrecognized NetworkBlock: " + network_block->classname() + ".";
    throw( std::logic_error( error_message ) );
   }
  } // end( for each time instant )
 } // end( for each stage )
} // end( InvestmentFunction::update_network_blocks )

/*--------------------------------------------------------------------------*/

void InvestmentFunction::update_blocks() {

 const auto saved_f_ignore_modifications = f_ignore_modifications;

 f_ignore_modifications = true;

 // The indices of the UnitBlocks
 std::vector< Index > block_indices;
 block_indices.reserve( v_asset_indices.size() );

 // The investment to be made in the UnitBlocks
 std::vector< double > block_investment;
 block_investment.reserve( v_asset_indices.size() );

 // The indices of the transmission lines
 std::vector< Index > line_indices;
 line_indices.reserve( v_asset_indices.size() );

 // The investment to be made in the transmission lines
 std::vector< double > line_investment;
 line_investment.reserve( v_asset_indices.size() );

 for( Index i = 0 ; i < v_asset_indices.size() ; ++i ) {

  const auto asset_type =  v_asset_type[ i ];
  const auto asset_index =  v_asset_indices[ i ];
  const auto var_value = get_var_value( i , false );

  if( asset_type == eUnitBlock ) {
   block_indices.push_back( asset_index );
   block_investment.push_back( var_value );
  }
  else if( asset_type == eLine ) {
   line_indices.push_back( asset_index );
   line_investment.push_back( var_value );
  }
  else {
   throw( std::logic_error( "InvestmentFunction::update_blocks: invalid asset"
                            " type: " + std::to_string( asset_type ) ) );
  }
 } // end( for each asset )

 auto num_sub_blocks_per_stage = f_num_sub_blocks_per_stage;
 if( get_ucblock() )
  num_sub_blocks_per_stage = 1;

 for( Index i = 0 ; i < num_sub_blocks_per_stage ; ++i ) {
  update_unit_blocks( i , block_indices , block_investment );
  update_network_blocks( i , line_indices , line_investment );
 }

 f_ignore_modifications = saved_f_ignore_modifications;
 f_blocks_are_updated = true;
}  // end( InvestmentFunction::update_blocks )

/*--------------------------------------------------------------------------*/

void InvestmentFunction::send_nuclear_modification
( const Observer::ChnlName chnl ) {
 // "nuclear modification" for Function: everything changed
 global_pool.invalidate();
 f_blocks_are_updated = false;
 generator_node_map.clear(); // the generator map must be rebuilt
 if( f_Observer )
  f_Observer->add_Modification
   ( std::make_shared< FunctionMod >( this , FunctionMod::NaNshift ) , chnl );
}  // end( InvestmentFunction::send_nuclear_modification )


/*--------------------------------------------------------------------------*/

Index InvestmentFunction::get_number_scenarios() const {
 if( v_Block.empty() )
  return( 0 );
 const auto sddp_block = static_cast< SDDPBlock * >( v_Block.front() );
 return( sddp_block->get_scenario_set().size() );
}

/*--------------------------------------------------------------------------*/

Index InvestmentFunction::get_number_stages() const {
 if( v_Block.empty() )
  return( 0 );
 if( const auto sddp_block = get_sddp_block() )
  return( sddp_block->get_time_horizon() );
 return( 1 );
}

/*--------------------------------------------------------------------------*/

Index InvestmentFunction::lock_sub_block() {
 auto sub_block_index = Inf< Index >();

 while( true ) {

#pragma omp critical (InvestmentFunction)
  {

   if( is_locked.empty() )
    is_locked.resize( f_num_sub_blocks_per_stage , false );

   for( Index i = 0 ; i < is_locked.size() ; ++i ) {
    if( ! is_locked[ i ] ) {
     sub_block_index = i;
     is_locked[ i ] = true;
     break;
    }
   }
  } // end omp critical (InvestmentFunction)

  if( sub_block_index < Inf< Index >() )
   // An unlocked sub-Block has been found. Return its index.
   return( sub_block_index );
  else
   // No sub-Block is available. Wait.
   std::this_thread::sleep_for
    ( std::chrono::duration< double >( waiting_time ) );
 }
}

/*--------------------------------------------------------------------------*/

void InvestmentFunction::unlock_sub_block( Index i ) {
#pragma omp critical (InvestmentFunction)
 {
  is_locked[ i ] = false;
 }
}

/*--------------------------------------------------------------------------*/

void InvestmentFunction::build_solver_map() {
 v_greedy_solvers.clear();
 v_greedy_solvers.reserve( v_Block.front()->get_registered_solvers().size() );
 f_sddp_solver = nullptr;

 Index solver_index = 0;
 for( auto solver : v_Block.front()->get_registered_solvers() ) {
  if( dynamic_cast< SDDPGreedySolver * >( solver ) )
   v_greedy_solvers.push_back( solver_index );
  else if( ! f_sddp_solver )
   f_sddp_solver = dynamic_cast< SDDPSolver * >( solver );
  ++solver_index;
 }
} // end( InvestmentFunction::build_solver_map )

void InvestmentFunction::output_variable_values() const {
 if( f_output_filename.empty() )
  return;

 std::ofstream file( f_output_filename , std::ios_base::app );

 if( ! file.is_open() ) {
  std::cerr << "InvestmentFunction::output_variable_values: "
            << "it was not possible to open the file \""
            << f_output_filename + "\"." << std::endl;
  return;
 }

 file << "Variables: " << v_x.size() << std::endl;
 file << std::setprecision( 20 );
 for( Index i = 0 ; i < v_x.size() ; ++i )
  file << get_var_value( i , false ) << std::endl;
}

/*--------------------------------------------------------------------------*/

void InvestmentFunction::output_function_value() const {
 if( f_output_filename.empty() )
  return;

 std::ofstream file( f_output_filename , std::ios_base::app );

 if( ! file.is_open() ) {
  std::cerr << "InvestmentFunction::output_function_value: "
            << "it was not possible to open the file \""
            << f_output_filename + "\"." << std::endl;
  return;
 }

 file << "Function value: " << f_value << std::endl;
}

/*--------------------------------------------------------------------------*/
/*----------------------------- GlobalPool ---------------------------------*/
/*--------------------------------------------------------------------------*/

void InvestmentFunction::GlobalPool::resize( Index size ) {
 linearization_coefficients.resize( size , {} );
 linearization_constants.resize( size , NaN );
 is_diagonal.resize( size );
}  // end( InvestmentFunction::GlobalPool::resize )

/*--------------------------------------------------------------------------*/

void InvestmentFunction::GlobalPool::store
( FunctionValue constant , std::vector< FunctionValue > coefficients ,
  Index name , bool diagonal_linearization ) {
 if( name >= size() )
  throw( std::invalid_argument( "InvestmentFunction::GlobalPool::store: "
                                "invalid linearization name." ) );
 linearization_coefficients[ name ] = coefficients;
 linearization_constants[ name ] = constant;
 is_diagonal[ name ] = diagonal_linearization;
}  // end( InvestmentFunction::GlobalPool::store )

/*--------------------------------------------------------------------------*/

bool InvestmentFunction::GlobalPool::is_linearization_there( Index name )
 const {

 if( name >= size() || std::isnan( linearization_constants[ name ] ) )
  return( false );
 return( true );
}  // end( InvestmentFunction::GlobalPool::is_linearization_there )

/*--------------------------------------------------------------------------*/

bool InvestmentFunction::GlobalPool::is_linearization_vertical( Index name )
 const {

 if( name >= size() || std::isnan( linearization_constants[ name ] ) )
  return( false );
 return( ! is_diagonal[ name ] );
}  // end( InvestmentFunction::GlobalPool::is_linearization_vertical )

/*--------------------------------------------------------------------------*/

void InvestmentFunction::GlobalPool::store_combination_of_linearizations
( c_LinearCombination & linear_combination , Index name ,
  FunctionValue AAccMlt ) {

 if( name >= size() )
  throw( std::invalid_argument
         ( "InvestmentFunction::GlobalPool::store_combination_of_"
           "linearizations: invalid global pool name." ) );

 if( linear_combination.empty() )
  throw( std::invalid_argument
         ( "InvestmentFunction::GlobalPool::store_combination_of_"
           "linearizations: linear combination is empty." ) );

 const auto combine = []( std::vector< FunctionValue > & x ,
                          const std::vector< FunctionValue > & y ,
                          const FunctionValue multiplier ) {
  assert( x.size() == y.size() );
  for( Index i = 0 ; i < x.size() ; ++i )
   x[ i ] += multiplier * y[ i ];
 };

 bool diagonal_linearization = false;
 std::vector< FunctionValue > coefficients;
 FunctionValue constant = 0;
 FunctionValue coeff_sum_diagonal = 0;

 for( const auto name_coeff : linear_combination ) {
  const auto linearization_name = name_coeff.first;
  const auto coeff = name_coeff.second;

  if( coeff < - AAccMlt ) {
   throw( std::invalid_argument
          ( "InvestmentFunction::GlobalPool::store_combination_of_"
            "linearizations: invalid coefficient for linearization with name " +
            std::to_string( linearization_name ) + ": " +
            std::to_string( coeff ) ) );
  }

  if( coefficients.empty() )
   coefficients.resize
    ( linearization_coefficients[ linearization_name ].size() , 0 );
  else
   combine( coefficients , linearization_coefficients[ linearization_name ] ,
            coeff );

  constant += coeff * linearization_constants[ linearization_name ];

  if( is_diagonal[ linearization_name ] ) {
   coeff_sum_diagonal += coeff;
   diagonal_linearization = true;
  }
 }

 if( diagonal_linearization &&
     std::abs( FunctionValue( 1 ) - coeff_sum_diagonal ) >
     AAccMlt * linear_combination.size() ) {

  throw( std::invalid_argument
         ( "InvestmentFunction::GlobalPool::store_combination_of_"
           "linearizations: a non-convex combination of diagonal "
           "linearizations has been provided." ) );
 }

 this->store( constant , coefficients , name , diagonal_linearization );

} // end( InvestmentFunction::GlobalPool::store_combination_of_linearizations )

/*--------------------------------------------------------------------------*/

void InvestmentFunction::GlobalPool::delete_linearization( const Index name ) {
 if( name >= size() )
  throw( std::invalid_argument( "GlobalPool::delete_linearization: invalid "
                                "linearization name: " +
                                std::to_string( name ) ) );

 linearization_constants[ name ] = NaN;
 linearization_coefficients[ name ] = {};
}  // end( InvestmentFunction::GlobalPool::delete_linearization )

/*--------------------------------------------------------------------------*/

void InvestmentFunction::GlobalPool::delete_linearizations( Subset & which ,
                                                            bool ordered ) {
 if( which.empty() ) {  // delete them all
  for( Index i = 0 ; i < size() ; ++i )
   if( is_linearization_there( i ) )
    delete_linearization( i );
 }
 else {                 // delete the given subset
  if( ! ordered )
   std::sort( which.begin() , which.end() );

  if( which.back() >= size() )
   throw( std::invalid_argument( "InvestmentFunction::GlobalPool::delete_linea"
                                 "rizations: invalid linearization name." ) );

  for( auto i : which )
   if( is_linearization_there( i ) )
    delete_linearization( i );
 }
}

/*--------------------------------------------------------------------------*/

void InvestmentFunction::GlobalPool::deserialize
( const netCDF::NcGroup & group ) {

 auto gs = group.getDim( "InvestmentFunction_MaxGlob" );
 const auto global_pool_size = gs.isNull() ? 0 : gs.getSize();

 linearization_constants.assign( global_pool_size , NaN );
 linearization_coefficients.resize( global_pool_size , {} );
 is_diagonal.assign( global_pool_size , true );

 if( global_pool_size ) {

  ::deserialize( group , "InvestmentFunction_Constants" , { global_pool_size } ,
                 linearization_constants , false , false );

  auto nct = group.getVar( "InvestmentFunction_Type" );
  if( nct.isNull() )
   throw( std::logic_error( "InvestmentFunction::GlobalPool::deserialize: "
                            "InvestmentFunction_Type was not found." ) );

  auto nc_coeff = group.getVar( "InvestmentFunction_Coefficients" );

  // Number of non-NaN constants
  auto num_constants = std::count_if( std::cbegin( linearization_constants ) ,
                                      std::cend( linearization_constants ) ,
                                      []( FunctionValue v ) {
                                       return( ! std::isnan( v ) ); } );

  Index num_var = 0;

  if( num_constants ) {
   // At least one linearization constant is not NaN. In this case, the
   // coefficients must be provided.
   if( nc_coeff.isNull() )
    throw( std::logic_error( "InvestmentFunction::GlobalPool::deserialize: "
                             "InvestmentFunction_Coefficients was not found."
                             ) );

   // Retrieve the number of variables.

   assert( nc_coeff.getDimCount() == 1 );
   const auto dim_size = nc_coeff.getDim( 0 ).getSize();
   if( dim_size % num_constants != 0 )
    throw( std::logic_error( "InvestmentFunction::GlobalPool::deserialize: "
                             "InvestmentFunction_Coefficients has an "
                             "incompatible dimension." ) );

   num_var = dim_size / num_constants;
  }

  Index coeff_start = 0;

  for( Index i = 0 ; i < global_pool_size ; ++i ) {
   int type;
   nct.getVar( { i } , &type );
   is_diagonal[ i ] = ( type != 0 );

   if( ! std::isnan( linearization_constants[ i ] ) ) {
    // There is a linearization that is not
    linearization_coefficients[ i ].resize( num_var );
    nc_coeff.getVar( { coeff_start } , { num_var } ,
                     linearization_coefficients[ i ].data() );
    coeff_start += num_var;
   }
  }
 }

 auto nic = group.getDim( "InvestmentFunction_ImpCoeffNum" );
 if( ( ! nic.isNull() ) && ( nic.getSize() ) ) {
  important_linearization_lin_comb.resize( nic.getSize() );

  auto ncCI = group.getVar( "InvestmentFunction_ImpCoeffInd" );
  if( ncCI.isNull() )
   throw( std::logic_error( "InvestmentFunction::GlobalPool::deserialize: "
                            "InvestmentFunction_ImpCoeffInd was not found." ) );

  auto ncCV = group.getVar( "InvestmentFunction_ImpCoeffVal" );
  if( ncCV.isNull() )
   throw( std::logic_error( "InvestmentFunction::GlobalPool::deserialize: "
                            "InvestmentFunction_ImpCoeffVal was not found." ) );

  for( Index i = 0 ; i < important_linearization_lin_comb.size() ; ++i ) {
   ncCI.getVar( { i } , &( important_linearization_lin_comb[ i ].first ) );
   ncCV.getVar( { i } , &( important_linearization_lin_comb[ i ].second ) );
  }
 }
 else
  important_linearization_lin_comb.clear();

}  // end( InvestmentFunction::GlobalPool::deserialize )

/*--------------------------------------------------------------------------*/

void InvestmentFunction::GlobalPool::serialize( netCDF::NcGroup & group ) const {

 const auto global_pool_size = size();

 if( global_pool_size ) {

  auto size_dim = group.addDim( "InvestmentFunction_MaxGlob" , global_pool_size );

  group.addVar( "InvestmentFunction_Constants" , netCDF::NcDouble() , size_dim ).
   putVar( linearization_constants.data() );

  Index num_var = 0;
  Index coeff_dim_size = 0;
  for( Index i = 0 ; i < global_pool_size ; ++i )
   if( ! std::isnan( linearization_constants[ i ] ) ) {
    if( num_var == 0 )
     num_var = linearization_coefficients[ i ].size();
    assert( num_var == linearization_coefficients[ i ].size() );
    coeff_dim_size += num_var;
   }

  if( coeff_dim_size ) {
   auto nc_coeff_dim = group.addDim( "InvestmentFunction_Coefficients_Dim" ,
                                     coeff_dim_size );
   auto nc_coeff = group.addVar( "InvestmentFunction_Coefficients" ,
                                 netCDF::NcDouble() , nc_coeff_dim );
   Index coeff_start = 0;
   for( Index i = 0 ; i < global_pool_size ; ++i )
    if( ! std::isnan( linearization_constants[ i ] ) ) {
     nc_coeff.putVar( { coeff_start } , { num_var } ,
                      linearization_coefficients[ i ].data() );
     coeff_start += num_var;
    }
  }

  std::vector< int > type( global_pool_size );
  for( Index i = 0 ; i < global_pool_size ; ++i )
   type[ i ] = is_diagonal[ i ] ? 1 : 0;

  group.addVar( "InvestmentFunction_Type" , netCDF::NcByte() , size_dim )
   .putVar( { 0 } , { global_pool_size } , type.data() );
 }

 if( ! important_linearization_lin_comb.empty() ) {
  auto linearization_dim = group.addDim
   ( "InvestmentFunction_ImpCoeffNum" , important_linearization_lin_comb.size() );

  auto linearization_coeff_index = group.addVar
   ( "InvestmentFunction_ImpCoeffInd" , netCDF::NcInt() , linearization_dim );

  auto linearization_coeff_value = group.addVar
   ( "InvestmentFunction_ImpCoeffVal" , netCDF::NcDouble() , linearization_dim );

  for( Index i = 0 ; i < important_linearization_lin_comb.size() ; ++i ) {
   linearization_coeff_index.putVar
    ( { i } , important_linearization_lin_comb[ i ].first );
   linearization_coeff_value.putVar
    ( { i } , important_linearization_lin_comb[ i ].second );
  }
 }
}  // end( InvestmentFunction::GlobalPool::serialize )

/*--------------------------------------------------------------------------*/

void InvestmentFunction::GlobalPool::clone( const GlobalPool & global_pool ) {

 if( this->size() < global_pool.size() ) {
  // resize this GlobalPool to accomodate the given elements
  this->resize( global_pool.size() );
 }

 std::copy( global_pool.is_diagonal.cbegin() ,
            global_pool.is_diagonal.cend() ,
            is_diagonal.begin() );

 std::copy( global_pool.linearization_constants.cbegin() ,
            global_pool.linearization_constants.cend() ,
            linearization_constants.begin() );

 important_linearization_lin_comb =
  global_pool.important_linearization_lin_comb;

 for( Index i = 0 ; i < size() ; ++i )
  linearization_coefficients[ i ] = global_pool.linearization_coefficients[ i ];
}  // end( InvestmentFunction::GlobalPool::clone )

/*--------------------------------------------------------------------------*/

void InvestmentFunction::GlobalPool::clone( GlobalPool && global_pool ) {

 // The size of the GlobalPool will be at least the size it currently has.
 const auto size = std::max( this->size() , global_pool.size() );

 is_diagonal = std::move( global_pool.is_diagonal );

 linearization_constants = std::move( global_pool.linearization_constants );

 important_linearization_lin_comb =
  std::move( global_pool.important_linearization_lin_comb );

 linearization_coefficients =
  std::move( global_pool.linearization_coefficients );

 // Possibly resize this GlobalPool so that it has at least the same size it
 // had before.

 this->resize( size );

}  // end( InvestmentFunction::GlobalPool::clone )

/*--------------------------------------------------------------------------*/

void InvestmentFunction::GlobalPool::get_linearization_coefficients
( FunctionValue * g , Range range , Index name ) const {
 for( Index i = range.first ; i < range.second ; ++i )
  g[ i - range.first ] = linearization_coefficients[ name ][ i ];
}  // end( InvestmentFunction::GlobalPool::get_linearization_coefficients )

/*--------------------------------------------------------------------------*/

void InvestmentFunction::GlobalPool::get_linearization_coefficients
( SparseVector & g , Range range , Index name ) const {
 for( Index i = range.first ; i < range.second ; ++i )
  g.coeffRef( i ) = linearization_coefficients[ name ][ i ];
}  // end( InvestmentFunction::GlobalPool::get_linearization_coefficients )

/*--------------------------------------------------------------------------*/

void InvestmentFunction::GlobalPool::get_linearization_coefficients
( FunctionValue * g , c_Subset & subset , const bool ordered , Index name )
 const {
 Index k = 0;
 for( auto i : subset )
  g[ k++ ] = linearization_coefficients[ name ][ i ];
}  // end( InvestmentFunction::GlobalPool::get_linearization_coefficients )

/*--------------------------------------------------------------------------*/

void InvestmentFunction::GlobalPool::get_linearization_coefficients
( SparseVector & g , c_Subset & subset , const bool ordered , Index name )
 const {
 for( auto i : subset )
  g.coeffRef( i ) = linearization_coefficients[ name ][ i ];
}  // end( InvestmentFunction::GlobalPool::get_linearization_coefficients )

/*--------------------------------------------------------------------------*/
/*------------------------ InvestmentFunctionState -------------------------*/
/*--------------------------------------------------------------------------*/

void InvestmentFunctionState::deserialize( const netCDF::NcGroup & group ) {
 global_pool.deserialize( group );
}

/*--------------------------------------------------------------------------*/

void InvestmentFunctionState::serialize( netCDF::NcGroup & group ) const {
 State::serialize( group );
 global_pool.serialize( group );
}

/*--------------------------------------------------------------------------*/

InvestmentFunctionState::InvestmentFunctionState
( const InvestmentFunction * f ) {
 if( ! f )
  return;
 global_pool.clone( f->global_pool );
}

/*--------------------------------------------------------------------------*/
/*-------------------- End File InvestmentFunction.cpp ---------------------*/
/*--------------------------------------------------------------------------*/
