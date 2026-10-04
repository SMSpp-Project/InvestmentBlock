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

#include "BendersBFunction.h"
#include "BendersBlock.h"
#include "BlockSolverConfig.h"
#include "FRealObjective.h"
#include "Observer.h"
#include "OneVarConstraint.h"
#include "RBlockConfig.h"
#include "InvestmentFunction.h"
#include "SDDPBlock.h"
#include "TwoStageStochasticBlock.h"
// Solution-output utility lives under tools/sddp_solver/. It is purely
// optional: when its header is on the include path (e.g. when the tool
// makefile passes -I.../tools/sddp_solver) the f_output_solution path
// emits per-scenario solutions; otherwise that path silently no-ops.
#if __has_include("SDDPBlockSolutionOutput.h")
 #include "SDDPBlockSolutionOutput.h"
 #define InvF_HAVE_SDDP_SOLUTION_OUTPUT 1
#endif
#include "SDDPGreedySolver.h"
#include "SDDPSolver.h"
#include "SMSTypedefs.h"
#include "UCBlock.h"

#include <cmath>
#include <chrono>
#include <functional>
#include <fstream>
#include <queue>

#ifdef _OPENMP
#include <omp.h>
#endif

#ifdef USE_MPI
#include <boost/mpi/communicator.hpp>
#include <boost/mpi/collectives.hpp>
#include <boost/serialization/vector.hpp>
#endif

/*--------------------------------------------------------------------------*/
/*------------------------- NAMESPACE AND USING ----------------------------*/
/*--------------------------------------------------------------------------*/

using namespace SMSpp_di_unipi_it;

/*--------------------------------------------------------------------------*/
/*------------------------------ LOCAL HELPERS -----------------------------*/
/*--------------------------------------------------------------------------*/

namespace {

/* Sets a flag for as long as it lives, and puts back the value it found on
 * every way out of the scope, a return or an exception alike. restore() puts
 * it back earlier, where the flag must be down before the scope ends; the
 * destructor then writes the same value again. */

class FlagGuard {
 public:
 FlagGuard( bool & flag , bool value ) : f_flag( flag ) , f_saved( flag ) {
  f_flag = value;
  }
 FlagGuard( const FlagGuard & ) = delete;
 FlagGuard & operator=( const FlagGuard & ) = delete;
 ~FlagGuard() { restore(); }
 void restore( void ) { f_flag = f_saved; }
 private:
 bool & f_flag;
 const bool f_saved;
};

}  // namespace

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

 // convexity precondition, same as in deserialize(): with a negative cost
 // the (dis)investment term is not convex and the subgradient at the kink
 // is silently invalid
 for( Index i = 0 ; i < v_asset_indices.size() ; ++i )
  if( ( get_cost( i ) < 0 ) || ( get_disinvestment_cost( i ) < 0 ) )
   throw( std::invalid_argument( "InvestmentFunction: Cost and "
                                 "DisinvestmentCost must be >= 0" ) );

 f_violated_constraint = { Inf< Index >() , eLHS };

 v_events.resize( max_event_number() );

 // default parameter values

 f_compute_linearization = get_dflt_int_par( intComputeLinearization );
 AAccMlt = get_dflt_dbl_par( dblAAccMlt );
 set_par( intGPMaxSz , C05Function::get_dflt_int_par( intGPMaxSz ) );
}

/*--------------------------------------------------------------------------*/

InvestmentFunction::~InvestmentFunction() {
 // remove the Solver that this InvestmentFunction registered in the inner
 // Blocks, then delete them
 unconfigure_inner_Block_Solver();

 for( auto block : v_Block )
  delete block;
}

/*--------------------------------------------------------------------------*/

void InvestmentFunction::deserialize( const netCDF::NcGroup & group ,
                                      ModParam issueMod ) {

 // The two attributes that sized by replication every battery, or every
 // intermittent unit, have left the format: each asset says how it is sized
 // in "AssetMethod". A file that still carries one is refused, rather than
 // read as a different model than the one it was written for

 for( const char * name : { "ReplicateBatteryUnits" ,
                            "ReplicateIntermittentUnits" } )
  if( ! group.getAtt( name ).isNull() )
   throw( std::logic_error( std::string( "InvestmentFunction::deserialize: "
                            "the attribute '" ) + name + "' is no longer "
                            "supported: say how each asset is sized with "
                            "'AssetSetter', or with 'AssetMethod'." ) );

 // Deserialize the dimensions

 // Number of assets

 Index num_assets;

 if( ! deserialize_dim( group , "NumAssets" , num_assets ) )
  num_assets = 0;

 // Deserialize the per-component weight (optional; default 1.0). Stored as a
 // netCDF attribute so legacy single-component files (weight == 1.0, attribute
 // absent) round-trip byte-identically.
 { auto w_att = group.getAtt( "Weight" );
   if( ! w_att.isNull() ) {
    w_att.getValues( & f_weight );
    if( f_weight <= 0 )
     throw( std::logic_error( "InvestmentFunction::deserialize: 'Weight' must "
                              "be > 0." ) );
    } }

 // Deserialize the asset -> active-variable mapping (optional; default identity,
 // i.e. asset i uses the i-th active variable). When set, asset i uses active
 // variable v_asset_var_index[ i ]; this lets several InvestmentFunction share
 // the same active variables while each invests only in some of them (the
 // multi-period (1:K) case). Absent ⇒ identity (legacy).
 ::deserialize( group , "AssetVarIndex" , num_assets , v_asset_var_index ,
                true , true );

 if( ! v_x.empty() ) {
  if( ! v_asset_var_index.empty() ) {
   // mapping mode: one entry per asset, each pointing to an active variable
   if( v_asset_var_index.size() != num_assets )
    throw( std::logic_error( "InvestmentFunction::deserialize: 'AssetVarIndex', "
                             "if provided, must have size 'NumAssets'." ) );
   for( auto vi : v_asset_var_index )
    if( vi >= v_x.size() )
     throw( std::logic_error( "InvestmentFunction::deserialize: 'AssetVarIndex' "
                              "entry " + std::to_string( vi ) + " is out of "
                              "range (>= number of active variables " +
                              std::to_string( v_x.size() ) + ")." ) );
   }
  else if( num_assets != v_x.size() )
   // identity mode (legacy): one asset per active variable, same order
   throw( std::logic_error( "InvestmentFunction::deserialize: the number of "
                            "assets to invest (" + std::to_string( num_assets ) +
                            ") is different from the number of active variables "
                            "(" + std::to_string( v_x.size() ) + ")." ) );
 }

 // Deserialize the per-asset variable baseline (optional; absent => the
 // baseline is the InstalledQuantity datum, as in the single-period case).
 // When present, entry i is the index of the active variable whose value is
 // asset i's baseline -- the multi-period transition: the component of period
 // t reads the design variable of period t-1 (see add_linear_term()).
 ::deserialize( group , "AssetBaselineVarIndex" , num_assets ,
                v_asset_baseline_var_index , true , true );

 if( ! v_asset_baseline_var_index.empty() ) {
  if( v_asset_baseline_var_index.size() != num_assets )
   throw( std::logic_error( "InvestmentFunction::deserialize: "
                            "'AssetBaselineVarIndex', if provided, must have "
                            "size 'NumAssets'." ) );
  if( ! v_x.empty() )
   for( auto vi : v_asset_baseline_var_index )
    if( vi >= v_x.size() )
     throw( std::logic_error( "InvestmentFunction::deserialize: "
                              "'AssetBaselineVarIndex' entry " +
                              std::to_string( vi ) + " is out of range (>= "
                              "number of active variables " +
                              std::to_string( v_x.size() ) + ")." ) );
  }

 // Number of linear constraints

 Index num_constraints;

 if( ! deserialize_dim( group , "NumConstraints" , num_constraints ) )
  num_constraints = 0;

 // Deserialize the assets

 if( num_assets ) {

  // Deserialize the asset indices.

  ::deserialize( group , "Assets" , num_assets , v_asset_indices , false );

  // Deserialize the types of assets.

  if( ! ::deserialize( group , "AssetType" , num_assets , v_asset_type ,
                       true , true ) )
   v_asset_type.resize( num_assets , eUnitBlock );

  // Deserialize how each asset is sized. This is optional: when it is not
  // there the way is deduced from the names that the Block of each asset
  // registers, which is what keeps the instances written before this field
  // existed working unchanged.

  v_asset_method.clear();
  ::deserialize( group , "AssetMethod" , num_assets , v_asset_method ,
                 true , true );

  if( ! v_asset_method.empty() ) {
   // same shape as AssetType: one value for all the assets, or one each
   if( v_asset_method.size() == 1 )
    v_asset_method.resize( num_assets , v_asset_method.front() );
   else if( v_asset_method.size() != num_assets )
    throw( std::logic_error( "InvestmentFunction::deserialize: the "
                             "'AssetMethod' netCDF variable, if provided, must"
                             " have size 0, 1, or 'NumAssets'." ) );

   for( const auto method : v_asset_method )
    if( ( method != eReplicate ) && ( method != eResize ) )
     throw( std::logic_error( "InvestmentFunction::deserialize: invalid "
                              "AssetMethod: " + std::to_string( method ) +
                              "." ) );
   }

  // The names of the methods that write each asset and read it back. They
  // go together, and they leave no room for AssetMethod, which says the same
  // thing for an instance without them [see resolve_asset_methods()].

  ::deserialize( group , "AssetSetter" , num_assets , v_asset_setter_name ,
                 true );
  ::deserialize( group , "AssetLinearization" , num_assets ,
                 v_asset_linearization_name , true );

  if( v_asset_setter_name.empty() != v_asset_linearization_name.empty() )
   throw( std::logic_error( "InvestmentFunction::deserialize: 'AssetSetter' "
                            "and 'AssetLinearization' go together, and only "
                            "one of them is there." ) );

  if( ( ! v_asset_setter_name.empty() ) && ( ! v_asset_method.empty() ) )
   throw( std::logic_error( "InvestmentFunction::deserialize: 'AssetMethod' "
                            "is for an instance without 'AssetSetter', and "
                            "this one has both." ) );

  // one value for all the assets, as a scalar, or one each. The length is
  // checked on the variable, before reading: ::deserialize() reads as many
  // values as asked, and would drop the ones past them without a word
  const auto one_each = [ & group , num_assets ]( const char * name ,
                                                  std::vector< int > & v ) {
   v.clear();
   const auto var = group.getVar( name );
   if( ( ! var.isNull() ) && ( var.getDimCount() == 1 ) &&
       ( var.getDim( 0 ).getSize() != num_assets ) )
    throw( std::logic_error( std::string( "InvestmentFunction::deserialize: "
                             "the '" ) + name + "' netCDF variable, if "
                             "provided, must be a scalar or have size "
                             "'NumAssets'." ) );
   ::deserialize( group , name , num_assets , v , true , true );
   };

  one_each( "AssetSignature" , v_asset_signature );
  for( const auto signature : v_asset_signature )
   if( signature )
    throw( std::logic_error( "InvestmentFunction::deserialize: invalid "
                             "AssetSignature: " + std::to_string( signature )
                             + "; 0 is the only one there is." ) );

  one_each( "AssetFeasibilityCut" , v_asset_feasibility_cut );
  for( const auto cut : v_asset_feasibility_cut )
   if( ( cut != 0 ) && ( cut != 1 ) )
    throw( std::logic_error( "InvestmentFunction::deserialize: invalid "
                             "AssetFeasibilityCut: " + std::to_string( cut ) +
                             "." ) );

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

  ::deserialize( group , "LowerBound" , num_assets , v_lower_bound ,
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

  // Convexity precondition of the format: the transition cost
  // max( c+ d , -c- d ) is convex iff c+ + c- >= 0; the format requires the
  // stronger (and natural) c+ >= 0 and c- >= 0, failing loud.
  for( Index i = 0 ; i < num_assets ; ++i )
   if( ( get_cost( i ) < 0 ) || ( get_disinvestment_cost( i ) < 0 ) )
    throw( std::logic_error( "InvestmentFunction::deserialize: 'Cost' and "
                             "'DisinvestmentCost' must be >= 0 (convexity "
                             "precondition of the format), but asset " +
                             std::to_string( i ) + " has ( " +
                             std::to_string( get_cost( i ) ) + " , " +
                             std::to_string( get_disinvestment_cost( i ) ) +
                             " )." ) );

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

  // A variable baseline excludes the InstalledQuantity datum: both answer
  // "how much was there before", having the two together is ambiguous.
  if( ( ! v_asset_baseline_var_index.empty() ) &&
      ( ! v_installed_quantity.empty() ) )
   throw( std::logic_error( "InvestmentFunction::deserialize: "
                            "'AssetBaselineVarIndex' cannot be combined with "
                            "'InstalledQuantity'." ) );

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

 // Deserialize the inner Block(s)

 auto inner_block_group = group.getGroup( BLOCK_NAME );
 if( inner_block_group.isNull() )
  throw( std::logic_error( "InvestmentFunction::deserialize: the '" +
                           BLOCK_NAME + "' group must be present." ) );

 if( f_external_inner_block ) {

  // The inner Block is provided from outside (StochasticBlock expansion, see
  // deserialize( group , inner , issueMod )): the 'Block' sub-group, which
  // there describes the scenario template, is NOT read.

  if( ! ( dynamic_cast< SDDPBlock * >( f_external_inner_block ) ||
          dynamic_cast< UCBlock * >( f_external_inner_block ) ) )
   throw( std::invalid_argument( "InvestmentFunction::deserialize: the given "
                                 "inner Block is neither an SDDPBlock nor a "
                                 "UCBlock." ) );

  set_inner_block( f_external_inner_block );
  }
 else if( f_num_sub_blocks <= 1 ) {

  // Single-Block path (legacy): create one inner Block, which may be either
  // an SDDPBlock or a UCBlock.

  auto inner_block = Block::new_Block( inner_block_group , this );

  if( ! inner_block )
   throw( std::logic_error( "InvestmentFunction::deserialize: it was not "
                            "possible to create the inner Block from group '" +
                            BLOCK_NAME + "'." ) );

  if( ! ( dynamic_cast< SDDPBlock * >( inner_block ) ||
          dynamic_cast< TwoStageStochasticBlock * >( inner_block ) ||
          dynamic_cast< UCBlock * >( inner_block ) ) ) {
   delete inner_block;
   throw( std::logic_error( "InvestmentFunction::deserialize: the inner "
                            "Block is neither an SDDPBlock, nor a "
                            "TwoStageStochasticBlock, nor a UCBlock." ) );
   }

  set_inner_block( inner_block );
  }
 else {

  // Multi-replica path: create f_num_sub_blocks identical inner Blocks. In
  // this path only SDDPBlock inner Blocks are supported.

  std::vector< Block * > blocks;
  blocks.reserve( f_num_sub_blocks );
  for( Index i = 0 ; i < f_num_sub_blocks ; ++i ) {
   auto inner_block = Block::new_Block( inner_block_group , this );
   if( ! inner_block )
    throw( std::logic_error( "InvestmentFunction::deserialize: it was not "
                             "possible to create the inner Block from group '"
                             + BLOCK_NAME + "'." ) );
   if( ! dynamic_cast< SDDPBlock * >( inner_block ) ) {
    // the replicas are read out of the same group: either the first one is
    // rejected, and none was made before it, or none is
    delete inner_block;
    throw( std::logic_error( "InvestmentFunction::deserialize: the inner "
                             "Block is not an SDDPBlock (only SDDPBlock is "
                             "supported in the multi-replica path)." ) );
    }
   blocks.push_back( inner_block );
   }
  set_inner_blocks( blocks );
  }

 Block::deserialize( group );

}  // end( InvestmentFunction::deserialize )

/*--------------------------------------------------------------------------*/
/*-------------------------- OTHER INITIALIZATIONS -------------------------*/
/*--------------------------------------------------------------------------*/

void InvestmentFunction::set_default_inner_Block_BlockConfig() {
 for( auto inner_block : v_Block ) {
  if( ! inner_block )
   continue;
  auto config = new OCRBlockConfig( inner_block );
  config->clear();
  config->apply( inner_block );
  delete config;
  }
}

/*--------------------------------------------------------------------------*/

void InvestmentFunction::set_default_inner_Block_BlockSolverConfig() {
 unconfigure_inner_Block_Solver();
}

/*--------------------------------------------------------------------------*/

void InvestmentFunction::unconfigure_inner_Block_Solver() {
 for( Index i = 0 ; i < v_BSC.size() ; ++i ) {
  if( v_BSC[ i ] && ( i < v_Block.size() ) && v_Block[ i ] )
   v_BSC[ i ]->apply( v_Block[ i ] );
  delete v_BSC[ i ];
  }

 v_BSC.clear();
}

/*--------------------------------------------------------------------------*/

void InvestmentFunction::set_ComputeConfig( const ComputeConfig * scfg )
{
 if( v_Block.empty() ||
     std::any_of( v_Block.cbegin() , v_Block.cend() ,
                  []( Block * b ) { return( b == nullptr ); } ) )
  throw( std::logic_error( "InvestmentFunction::set_ComputeConfig: "
                           "the inner Block is not present" ) );

 if( ! scfg ) {
  // scfg is nullptr
  ThinComputeInterface::set_ComputeConfig();
  set_default_inner_Block_configuration();
  return;
  }

 if( ! scfg->f_extra_Configuration ) {
  // scfg->f_extra_Configuration is nullptr
  ThinComputeInterface::set_ComputeConfig( scfg );
  if( ! scfg->diff() )
   set_default_inner_Block_configuration();
  return;
  }

 auto config_map = dynamic_cast
  < SimpleConfiguration< std::map< std::string , Configuration * > > * >
  ( scfg->f_extra_Configuration );

 if( ! config_map )
  // An invalid extra Configuration has been provided
  throw( std::invalid_argument( "InvestmentFunction::set_ComputeConfig: "
                                "invalid extra_Configuration" ) );

 ThinComputeInterface::set_ComputeConfig( scfg );

 for( const auto & [ key , config ] : config_map->f_value ) {
  if( key == "BlockConfig" ) {
   if( ! config ) {
    if( ! scfg->diff() )
     // A BlockConfig for the inner Block was not provided. The inner Block is
     // configured to its default configuration.
     set_default_inner_Block_BlockConfig();
    }
   else if( auto block_config = dynamic_cast< BlockConfig * >( config ) ) {
    // A BlockConfig for the inner Block has been provided. Apply it to
    // every (replica) inner Block.
    for( auto inner_block : v_Block )
     block_config->apply( inner_block );
   }
   else
    // An invalid Configuration has been provided.
    throw( std::invalid_argument(
	     "InvestmentFunction::set_ComputeConfig: the Configuration "
             "associated with key \"BlockConfig\" is not a BlockConfig" ) );
   }
  else
   if( key == "BlockSolverConfig" ) {
    if( ! config ) {
     if( ! scfg->diff() )
      // A BlockSolverConfig for the inner Block was not provided. The Solver
      // of the inner Block (and their sub-Block, recursively) are
      // unregistered and deleted
      set_default_inner_Block_BlockSolverConfig();
     }
    else
     if( auto bsc = dynamic_cast< BlockSolverConfig * >( config ) ) {
      // A BlockSolverConfig for the inner Block has been provided. Apply it
      // to every (replica) inner Block through a private clone per Block,
      // which then remains, clear()-ed, as the cleanup object of that Block:
      // having done the apply() itself, it records the Solver registered
      // there and its cleared apply() removes exactly them [see
      // BlockSolverConfig::apply()]
      unconfigure_inner_Block_Solver();   // clean up for the new arrival
      v_BSC.reserve( v_Block.size() );
      for( auto inner_block : v_Block ) {
       auto cBSC = bsc->clone();
       cBSC->apply( inner_block );
       cBSC->clear();
       v_BSC.push_back( cBSC );
       }
      }
     else
      // An invalid Configuration has been provided.
      throw( std::invalid_argument(
	     "InvestmentFunction::set_ComputeConfig: the Configuration "
             "associated with key \"BlockSolverConfig\" is not a "
             "BlockSolverConfig" ) );
    }
   else
    // An invalid key has been provided.
    throw( std::invalid_argument( "InvestmentFunction::set_ComputeConfig: "
				  "invalid key: " + key ) );
  }
 }

/*--------------------------------------------------------------------------*/

void InvestmentFunction::set_variables( VarVector && x )
{
 // The cost vectors are indexed by asset, so their size is the number of
 // assets, which need not equal the number of active variables (the latter is
 // larger when several components share the same variables; see
 // set_asset_variable_indices()).
 if( ! v_cost.empty() )
  if( v_cost.size() != v_asset_indices.size() )
   throw( std::logic_error("InvestmentFunction::set_variables: the number of "
                           "linear coefficients is " +
                           std::to_string( v_cost.size() ) + ", but there are " +
                           std::to_string( v_asset_indices.size() ) +
                           " assets" ) );

 if( ! v_disinvestment_cost.empty() )
  if( v_disinvestment_cost.size() != v_asset_indices.size() )
   throw( std::logic_error("InvestmentFunction::set_variables: the number of "
                           "disinvestment coefficients is " +
                           std::to_string( v_disinvestment_cost.size() ) +
                           ", but there are " +
                           std::to_string( v_asset_indices.size() ) +
                           " assets" ) );

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
 v_x.erase( v_x.begin() + i ); // erase it in v_x

 // Erase the asset index, asset type, and the linear coefficient associated
 // with the Variable being removed
 v_asset_indices.erase( v_asset_indices.begin() + i );
 v_asset_type.erase( v_asset_type.begin() + i );

 for( auto v : { & v_asset_method , & v_asset_signature ,
                  & v_asset_feasibility_cut } )
  if( ! v->empty() )
   v->erase( v->begin() + i );

 for( auto v : { & v_asset_setter_name , & v_asset_linearization_name } )
  if( ! v->empty() )
   v->erase( v->begin() + i );

 // the groups of assets are indexed by asset, so they no longer are: drop
 // them and let them be resolved again the next time the Blocks are updated
 v_asset_groups.clear();
 f_methods_resolved = false;

 f_blocks_are_updated = false;

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
  for( auto v : { & v_asset_method , & v_asset_signature ,
                   & v_asset_feasibility_cut } )
   v->clear();
  v_asset_setter_name.clear();
  v_asset_linearization_name.clear();
  v_asset_groups.clear();
  f_methods_resolved = false;

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

  for( auto v : { & v_asset_method , & v_asset_signature ,
                   & v_asset_feasibility_cut } )
   if( ! v->empty() )
    v->erase( v->begin() + range.first , v->begin() + range.second );

  for( auto v : { & v_asset_setter_name , & v_asset_linearization_name } )
   if( ! v->empty() )
    v->erase( v->begin() + range.first , v->begin() + range.second );

  // the resolved methods are indexed by asset, so they no longer are
  v_asset_groups.clear();
  f_methods_resolved = false;
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
  for( auto v : { & v_asset_method , & v_asset_signature ,
                   & v_asset_feasibility_cut } )
   v->clear();
  v_asset_setter_name.clear();
  v_asset_linearization_name.clear();
  v_asset_groups.clear();
  f_methods_resolved = false;

  f_blocks_are_updated = false;

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

 const auto erase = [ this , &indices ]() {
  compact( v_asset_indices , indices );
  compact( v_asset_type , indices );
  compact( v_cost , indices );
  compact( v_disinvestment_cost , indices );
  compact( v_x , indices );

  for( auto v : { & v_asset_method , & v_asset_signature ,
                   & v_asset_feasibility_cut } )
   if( ! v->empty() )
    compact( *v , indices );

  for( auto v : { & v_asset_setter_name , & v_asset_linearization_name } )
   if( ! v->empty() )
    compact( *v , indices );

  // the resolved methods are indexed by asset, so they no longer are
  v_asset_groups.clear();
  f_methods_resolved = false;
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

void InvestmentFunction::serialize( netCDF::NcGroup & group ,
                                    const Block * inner ,
                                    double weight ) const {

 Block::serialize( group );

 // per-component weight: written only if != 1.0, so legacy single-component
 // files stay byte-identical (the default is restored on deserialize)
 if( weight != 1.0 )
  group.putAtt( "Weight" , netCDF::NcDouble() , weight );

 // in the single-component format the group is that of the InvestmentBlock,
 // which has already written the number of assets and their lower bounds
 const auto num_assets = v_asset_indices.size();
 auto NumAssets = group.getDim( "NumAssets" );
 if( NumAssets.isNull() )
  NumAssets = group.addDim( "NumAssets" , num_assets );

 ::serialize( group , "Assets" , netCDF::NcUint() , NumAssets ,
              v_asset_indices );

 ::serialize( group , "AssetType" , netCDF::NcUint() , NumAssets ,
              v_asset_type );

 // how each asset is sized, and by what: written only if the instance said
 // it, so the files that do not carry it stay byte-identical
 if( ! v_asset_method.empty() )
  ::serialize( group , "AssetMethod" , netCDF::NcUint() , NumAssets ,
               v_asset_method );

 ::serialize( group , "AssetSetter" , NumAssets , v_asset_setter_name );
 ::serialize( group , "AssetLinearization" , NumAssets ,
              v_asset_linearization_name );

 if( ! v_asset_signature.empty() )
  ::serialize( group , "AssetSignature" , netCDF::NcUint() , NumAssets ,
               v_asset_signature );

 if( ! v_asset_feasibility_cut.empty() )
  ::serialize( group , "AssetFeasibilityCut" , netCDF::NcUint() , NumAssets ,
               v_asset_feasibility_cut );

 // asset -> active-variable mapping: written only if non-identity (non-empty),
 // so legacy files stay byte-identical
 if( ! v_asset_var_index.empty() )
  ::serialize( group , "AssetVarIndex" , netCDF::NcUint() , NumAssets ,
               v_asset_var_index );

 if( ! v_asset_baseline_var_index.empty() )
  ::serialize( group , "AssetBaselineVarIndex" , netCDF::NcUint() , NumAssets ,
               v_asset_baseline_var_index );

 if( group.getVar( "LowerBound" ).isNull() )
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

 if( inner ) {
  auto inner_block_group = group.addGroup( BLOCK_NAME );
  inner->serialize( inner_block_group );
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
 f_has_farkas_linearization = false;
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

 // Multi-replica SDDPBlock path: handles its own locking internally
 // (one lock per replica). This path is triggered as soon as the
 // InvestmentFunction was given more than one inner Block via
 // set_inner_blocks() or by the multi-replica deserialize() path.

 if( v_Block.size() > 1 )
  return( compute_SDDPBlock_replicas( changedvars ) );

 // Legacy single-Block path: lock the (only) inner Block then dispatch
 // based on its concrete type.

 bool owned = v_Block.front()->is_owned_by( f_id );
 if( ( ! owned ) && ( ! v_Block.front()->lock( f_id ) ) )
  return( kError ); // If this does not work, this is clearly an error.

 int status;
 if( get_sddp_block() ) {
  status = compute_SDDPBlock( changedvars , owned );
#ifdef USE_MPI
  // only rank 0 simulates [see compute_SDDPBlock()], and every other rank
  // takes what it found: the Solver of this Function runs on every rank, and
  // only with the same answers does it ask for the same points on each, as
  // the training of the SDDPBlock, collective, requires
  boost::mpi::communicator world;
  boost::mpi::broadcast( world , status , 0 );
  boost::mpi::broadcast( world , f_solver_status , 0 );
  boost::mpi::broadcast( world , f_value , 0 );
  boost::mpi::broadcast( world , f_has_value , 0 );
  boost::mpi::broadcast( world , f_has_diagonal_linearization , 0 );
  boost::mpi::broadcast( world , v_linearization , 0 );
#endif
  }
 else if( get_ucblock() || get_tssb_block() )
  // the same computation serves both: fix the investment in the inner
  // Block, solve it and read value and linearization off its Solver. What a
  // TwoStageStochasticBlock adds is only that the investment is written into
  // each scenario, which update_blocks() takes care of, and that the value
  // it returns is already the expected one over the scenarios
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

 // the Modification of the inner Block are ignored until the linearization
 // is read, and the flag is put back on every way out, errors included
 FlagGuard ignore_modifications( f_ignore_modifications , true );

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

 // a provably unbounded inner Block is an answer, not an error, as for
 // LagBFunction and BendersBFunction [see Solver::kUnbounded]: the value is
 // the opposite of the worst one, whether or not the Solver also has a
 // solution, and there is no linearization of an unbounded value to read
 if( f_solver_status == Solver::kUnbounded ) {
  f_value = - worst_value();
  f_has_value = true;
  output_function_value();
  solver->set_id( solver_id );
  return( f_solver_status );
  }

 if( ! solver->has_var_solution() ) {
  f_value = worst_value();
  output_function_value();

  // A *provably* infeasible inner subproblem is a kOK-type answer at the
  // worst value (+Inf for a minimization), not an unrecoverable error: report
  // it as such so the caller (e.g. the bundle) treats this point as infeasible
  // and proceeds, exactly as done above for violated linear constraints. Only
  // a genuine failure with no proof of infeasibility is a kError.
  if( f_solver_status == Solver::kInfeasible ) {
   // the point is outside the domain, and saying only that leaves whoever
   // asked with nothing to cut it away with: the master would propose the
   // same direction again. The infeasibility certificate is the vertical
   // linearization, and it is read exactly as the diagonal one is, the
   // Solver writing the dual ray where the optimal duals go
   if( f_compute_linearization )
    if( auto cda = dynamic_cast< CDASolver * >( solver ) )
     if( cda->has_dual_direction() ) {

      /* The multipliers are of use only if they are homogeneous, - A'y and
       * not c - A'y: with the Objective in them the coefficients of the
       * vertical linearization all come out zero, the master is handed the
       * empty row 0 <= - alpha and a problem that has an optimum is reported
       * infeasible. The constant comes out right either way, which is what
       * makes the mistake silent and this check worth its three lines. */

      if( const auto hd = cda->int_par_str2idx( "intHomogeneousDirection" ) ;
          ( hd < Inf< Solver::idx_type >() ) && ( ! cda->get_int_par( hd ) ) )
       throw( std::logic_error(
        "InvestmentFunction::compute: the Solver of the inner Block returns "
        "the multipliers of an unbounded dual direction with the Objective "
        "in them, and no certificate of infeasibility can be read out of "
        "those: set intHomogeneousDirection to 1 in its configuration" ) );

      cda->get_dual_direction();

      /* The value of the certificate is one number over the whole inner
       * Block, and it is summed out of the Block itself. The Solver has a
       * number of its own [CDASolver::get_dual_direction_value()] and it is
       * NOT this one: what a :MILPSolver hands over there is the violation
       * of the aggregated constraint at the point its algorithm stopped at,
       * while what the cut needs is the value of the certificate, each bound
       * taken on the side its multiplier points at. On these instances the
       * two differ, 6000 against 106000, and the difference is nine of the
       * twelve columns of the certificate taken on the opposite side. */

      reset_linearization();
      try {
       FunctionValue farkas = 0;
       for( Index i = 0 ; i < get_number_investment_sub_blocks() ; ++i ) {
        update_linearization( i , true );
        farkas += compute_farkas_value( 0 , i );
        }

       /* A certificate is a certificate only if it is positive here: the
        * inner Block is proved infeasible at this point by F( x ) > 0, and
        * F( x ) <= 0 is what cuts the point away. A non-positive value is no
        * usable proof, the degenerate ray of an empty variable domain in
        * particular, whose multipliers are all zero; claiming a cut out of it
        * would hand the master the empty statement 0 <= 0. */
       if( farkas > 0 ) {
        f_farkas_value = farkas;
        f_has_farkas_linearization = true;
        }
       }
      catch( const std::exception & e ) {
       std::cout << "InvestmentFunction::compute(): an error occurred while "
                 << "updating the certificate: '" << e.what() << "'"
                 << std::endl;
       }
      }

   solver->set_id( solver_id );
   return( Solver::kInfeasible );
   }

  // the Solver of the inner Block gave no solution and no proof that there
  // is none: which of the two it was is said, the kError that reaches the
  // caller telling neither the status nor which Solver returned it
  std::cout << "InvestmentFunction::compute(): " << solver->classname()
            << " on the inner Block returned status " << f_solver_status
            << " with no solution and no proof of infeasibility" << std::endl;

  solver->set_id( solver_id );
  return( kError );
 }

 f_value = solver->get_var_value();

 // Compute the linearization if required

 if( f_compute_linearization ) {
  reset_linearization();
  try {
   // the value function is the sum of the contributions of the sub-Blocks
   // that carry the investment, and so is its linearization: they have to be
   // read one by one, exactly as update_blocks() writes the investment into
   // each of them. With a single UCBlock inside there is one of them and the
   // loop is the previous single call; with a TwoStageStochasticBlock there
   // is one per scenario, all solved at once by the same Solver but each
   // holding its own duals, and reading only the first one would return the
   // investment cost alone wherever the other scenarios are the binding ones
   for( Index i = 0 ; i < get_number_investment_sub_blocks() ; ++i )
    update_linearization( i );
  }
  catch( const std::exception & e ) {
   // An error occurred while updating the linearization.
   std::cout << "InvestmentFunction::compute(): an error occurred while "
             << "updating the linearization: '"
             << e.what() << "'" << std::endl;
   solver->set_id( solver_id );
   output_function_value();
   // without a linearization the caller can do nothing with this point, and
   // the status of the solve would tell it all went well; it is also what a
   // call with nothing changed returns, so it is the status that is set
   f_solver_status = kError;
   return( f_solver_status );
  }
 }

 // Possibly output the solution

 if( f_output_solution ) {
  // TODO
 }

 ignore_modifications.restore();

 // Consider the linear term of the objective (investment/transition cost;
 // baseline-aware, see add_linear_term())

 add_linear_term( f_value , v_linearization );

 solver->set_id( solver_id );

 // w * F: scale the diagonal value and linearization by the component weight
 scale_diagonal_by_weight();

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

 // the Modification of the inner Blocks are ignored until the simulation is
 // over, and the flag is put back on every way out, errors included
 FlagGuard ignore_modifications( f_ignore_modifications , true );

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
  // only rank 0 simulates: the cuts reach the SDDPBlock of a rank through the
  // subproblems that rank solves in the training, and rank 0 is the only one
  // sure to solve some at every stage. compute() hands its outcome to the
  // other ranks, which have nothing to do here but give the identity back
  boost::mpi::communicator communicator;
  if( communicator.rank() ) {
   unlend_identity();
   return( kOK );
   }
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

 ignore_modifications.restore();

 // Compute the expectation of the operational costs

 f_value /= num_scenarios;

 // Compute the expectation of the linearization

 for( Index i = 0 ; i < v_linearization.size() ; ++i ) {
  v_linearization[ i ] /= num_scenarios;
 }

 // Consider the linear term of the objective (investment/transition cost;
 // baseline-aware, see add_linear_term())

 add_linear_term( f_value , v_linearization );

 // Unlock the inner Block if it is necessary
 unlend_identity();

 // w * F: scale the diagonal value and linearization by the component weight
 scale_diagonal_by_weight();

 // At this point, if a linearization has been computed, then a diagonal
 // linearization is available.
 f_has_diagonal_linearization = f_compute_linearization;

 f_has_value = true;

 // Finally, handle the events that happen at the end of compute()

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

void InvestmentFunction::handle_events( int type ) const {
 for( auto & event : v_events[ type ] )
  event();
}

/*--------------------------------------------------------------------------*/

int InvestmentFunction::compute_SDDPBlock_replicas( bool changedvars ) {

 // Verify that every replica is an SDDPBlock.

 for( auto block : v_Block )
  if( ! dynamic_cast< SDDPBlock * >( block ) )
   throw( std::invalid_argument( "InvestmentFunction::compute_SDDPBlock_"
                                 "replicas: every replica must be an "
                                 "SDDPBlock." ) );

 // For the InvestmentFunction to be correctly computed, the inner Blocks
 // cannot be modified by other entities. Therefore, every inner Block must
 // be locked.

 std::vector< bool > owned( v_Block.size() );

 for( Index i = 0 ; i < v_Block.size() ; ++i ) {
  owned[ i ] = v_Block[ i ]->is_owned_by( f_id );
  if( ( ! owned[ i ] ) && ( ! v_Block[ i ]->lock( f_id ) ) ) {
   f_value = worst_value();
   output_function_value();
   f_solver_status = kError;
   handle_events( eBeforeTermination );
   return( f_solver_status );
   }
  }

 // Since the inner Solver may need to lock the inner Block, the
 // InvestmentFunction lends its identity to the inner Solver.

 std::vector< void * > solver_ids( v_Block.size() );
 for( Index i = 0 ; i < v_Block.size() ; ++i ) {
  if( auto solver = get_solver( i ) ) {
   solver_ids[ i ] = solver->id();
   solver->set_id( f_id );
   }
  }

 if( changedvars || ( ! f_blocks_are_updated ) ) {
  // Update the Blocks.
  try {
   update_blocks();
   }
  catch( const std::exception & e ) {
   for( Index i = 0 ; i < v_Block.size() ; ++i ) {
    if( auto solver = get_solver( i ) )
     solver->set_id( solver_ids[ i ] );
    if( ! owned[ i ] )
     v_Block[ i ]->unlock( f_id );
    }
   std::cerr << "InvestmentFunction::compute_SDDPBlock_replicas: an error "
                "occurred while updating the Blocks: '" << e.what() << "'"
             << std::endl;
   f_value = worst_value();
   output_function_value();
   f_solver_status = kError;
   handle_events( eBeforeTermination );
   return( f_solver_status );
   }
  }

 const auto num_scenarios = get_number_scenarios();
 f_value = 0.0;

 // the Modification of the inner Blocks are ignored until the simulation is
 // over, and the flag is put back on every way out, errors included
 FlagGuard ignore_modifications( f_ignore_modifications , true );

 f_solver_status = kUnEval;

 // Indicates whether the loop over the scenarios must be interrupted. The
 // loop is interrupted when either a solution for a subproblem is not
 // found or when an error occurs while updating the linearization.
 bool interrupt_loop = false;

 int local_error = 0;
 // A scenario subproblem that is *provably* infeasible is not a hard error: it
 // is tracked separately so the whole InvestmentFunction can be reported as
 // infeasible (a kOK-type answer at the worst value) rather than as a kError.
 int local_infeasible = 0;

 auto simulation_value = decltype( f_value )( 0 );

 // one coefficient per active Variable, as reset_linearization() sizes it:
 // v_linearization is empty until a linearization has been computed, and
 // sizing on it wrote the first coefficient out of an empty vector
 std::vector< double > local_linearization( v_x.size() , 0 );

#ifdef USE_MPI
 boost::mpi::communicator world;
 const auto world_rank = world.rank();
 const auto world_size = world.size();

 const int chunk_size = (int)( num_scenarios / world_size );
 const int remainder = num_scenarios % world_size;

 const auto scenario_start = world_rank * chunk_size +
  std::min( world_rank , remainder );
 const auto scenario_end = scenario_start + chunk_size +
  ( world_rank < remainder ? 1 : 0 );
#else
 const auto scenario_start = 0;
 const auto scenario_end = num_scenarios;
#endif

 #pragma omp parallel for reduction( + : simulation_value )
 for( int scenario = scenario_start ; scenario < int( scenario_end ) ;
      ++scenario ) {

  if( interrupt_loop )
   continue;

  const auto sub_block_index = lock_sub_block();
  auto solver = get_solver< SDDPGreedySolver >( sub_block_index );
  if( ! solver ) {
   #pragma omp critical( InvestmentFunction )
   {
    interrupt_loop = true;
    local_error = 1;
    }
   unlock_sub_block( sub_block_index );
   continue;
   }
  solver->set_par( SDDPGreedySolver::intScenarioId , scenario );
  const auto status = solver->compute( true );

  if( ! solver->has_var_solution() ) {
   #pragma omp critical( InvestmentFunction )
   {
    interrupt_loop = true;
    // distinguish a provable infeasibility from a genuine failure
    if( status == Solver::kInfeasible )
     local_infeasible = 1;
    else
     local_error = 1;
    }
   unlock_sub_block( sub_block_index );
   continue;
   }

  try {
   #pragma omp critical( InvestmentFunction )
   {
    f_solver_status = status;
    if( f_compute_linearization )
     update_linearization( sub_block_index , local_linearization );
    }
   }
  catch( const std::exception & e ) {
   std::cerr << "InvestmentFunction::compute_SDDPBlock_replicas: an error "
                "occurred while updating the linearization: '"
             << e.what() << "'" << std::endl;
   unlock_sub_block( sub_block_index );
   #pragma omp critical( InvestmentFunction )
   {
    interrupt_loop = true;
    local_error = 1;
    }
   continue;
   }

  // Update the value of the InvestmentFunction
  simulation_value += solver->get_var_value();

  // Possibly output the solution (if compiled with the helper available)
  if( f_output_solution ) {
#ifdef InvF_HAVE_SDDP_SOLUTION_OUTPUT
   SDDPBlockSolutionOutput( f_output_solution_directory ).
    print( get_sddp_block( sub_block_index ) , scenario , true );
#endif
   }

  // Unlock the sub-Block
  unlock_sub_block( sub_block_index );

  } // end( for each scenario )

#ifdef USE_MPI
 // Check whether there was an error or an infeasibility in any scenario, on
 // any rank
 int global_error = 0;
 boost::mpi::reduce( world , local_error , global_error ,
                     boost::mpi::maximum< int >() , 0 );
 boost::mpi::broadcast( world , global_error , 0 );

 if( global_error == 1 )
  local_error = 1;

 int global_infeasible = 0;
 boost::mpi::reduce( world , local_infeasible , global_infeasible ,
                     boost::mpi::maximum< int >() , 0 );
 boost::mpi::broadcast( world , global_infeasible , 0 );

 if( global_infeasible == 1 )
  local_infeasible = 1;
#endif

 if( local_error || local_infeasible ) {
  for( Index i = 0 ; i < v_Block.size() ; ++i ) {
   if( auto solver = get_solver( i ) )
    solver->set_id( solver_ids[ i ] );
   if( ! owned[ i ] )
    v_Block[ i ]->unlock( f_id );
   unlock_sub_block( i );
   }

  // A genuine failure prevails over a provable infeasibility; a pure
  // infeasibility is a kOK-type answer at the worst value, letting the caller
  // (e.g. the bundle) treat this point as infeasible and proceed.
  f_solver_status = local_error ? kError : Solver::kInfeasible;
  f_value = worst_value();

#ifdef USE_MPI
  if( ! world.rank() ) {
#endif
   output_function_value();
   handle_events( eBeforeTermination );
#ifdef USE_MPI
   }
#endif

  return( f_solver_status );
  }

 auto local_value = simulation_value;

 ignore_modifications.restore();

 // Compute the expectation of the operational costs
 local_value /= num_scenarios;

 // Compute the expectation of the linearization
 for( Index i = 0 ; i < local_linearization.size() ; ++i )
  local_linearization[ i ] /= num_scenarios;

 // Consider the linear term of the objective
#ifdef USE_MPI
 if( ! world.rank() ) {
#endif

 // investment/transition cost; baseline-aware, see add_linear_term()
 add_linear_term( local_value , local_linearization );

#ifdef USE_MPI
 }
#endif

 f_value = 0.0;

#ifdef USE_MPI
 boost::mpi::reduce( world , local_value , f_value ,
                     std::plus< double >() , 0 );
 boost::mpi::broadcast( world , f_value , 0 );

 boost::mpi::reduce( world , local_linearization , v_linearization ,
                     std::plus< double >() , 0 );
 boost::mpi::broadcast( world , v_linearization , 0 );
#else
 f_value = local_value;
 v_linearization = local_linearization;
#endif

 // Unlock the inner Blocks if needed; restore solver identities.
 for( Index i = 0 ; i < v_Block.size() ; ++i ) {
  if( auto solver = get_solver( i ) )
   solver->set_id( solver_ids[ i ] );
  if( ! owned[ i ] )
   v_Block[ i ]->unlock( f_id );
  }

 // w * F: scale the diagonal value and linearization by the component weight
 // (after the MPI reduce/broadcast, so every rank scales identical values)
 scale_diagonal_by_weight();

 // At this point, if a linearization has been computed, then a diagonal
 // linearization is available.
 f_has_diagonal_linearization = f_compute_linearization;

 f_has_value = true;

 f_solver_status = kOK;

#ifdef USE_MPI
 if( ! world.rank() ) {
#endif
  output_function_value();
  handle_events( eBeforeTermination );
#ifdef USE_MPI
  }
#endif

 return( f_solver_status );

}  // end( InvestmentFunction::compute_SDDPBlock_replicas )

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

Function::FunctionValue InvestmentFunction::get_constant_term( void ) const
{
 // w * F: the recourse-objective constant is part of the value and scales by
 // the component weight (it does not flow through f_value)
 if( auto bk = get_nested_Block( 0 ) )
  return( f_weight * get_recours_obj( bk ) );
 return( 0 );
}

/*--------------------------------------------------------------------------*/

bool InvestmentFunction::is_convex( void ) { return( true ); }

/*--------------------------------------------------------------------------*/

bool InvestmentFunction::is_concave( void ) { return( false ); }

/*--------------------------------------------------------------------------*/

bool InvestmentFunction::has_linearization( bool diagonal )
{
 if( diagonal ) {
  f_diagonal_linearization_required = true;
  return( f_has_diagonal_linearization );
  }
 f_diagonal_linearization_required = false;

 // the certificate is already in v_linearization, put there by the same
 // chain that writes the diagonal one
 if( f_has_farkas_linearization )
  return( true );

 if( f_violated_constraint.first < Inf< Index >() ) {
  // A constraint has been violated: materialize the vertical linearization
  // (gradient of the violated constraint, per-asset columns landing on each
  // asset's variable -- see vertical_linearization()).
  vertical_linearization( v_linearization );
  return( true );
  }

 return( false );
}  // end( InvestmentFunction::has_linearization )


/*--------------------------------------------------------------------------*/

bool InvestmentFunction::compute_new_linearization( bool diagonal )
{
 if( diagonal )
  return( false );
 if( f_has_farkas_linearization )
  return( true );
 return( ! constraints_are_satisfied() );
 }

/*--------------------------------------------------------------------------*/

void InvestmentFunction::store_linearization( Index name , ModParam issueMod )
{
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
 else if( f_has_farkas_linearization ) {
  // Vertical linearization out of the infeasibility certificate
  for( Index j = range.first ; j < range.second ; ++j )
   g[ j - range.first ] = v_linearization[ j ];
 }
 else {
  // Vertical linearization (per-asset columns, see vertical_linearization())
  std::vector< double > vl;
  vertical_linearization( vl );
  for( Index j = range.first ; j < range.second ; ++j )
   g[ j - range.first ] = vl[ j ];
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
 else if( f_has_farkas_linearization ) {
  // Vertical linearization out of the infeasibility certificate
  for( Index j = range.first ; j < range.second ; ++j )
   g.coeffRef( j ) = v_linearization[ j ];
 }
 else {
  // Vertical linearization (per-asset columns, see vertical_linearization())
  std::vector< double > vl;
  vertical_linearization( vl );
  for( Index j = range.first ; j < range.second ; ++j )
   g.coeffRef( j ) = vl[ j ];
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
 else if( f_has_farkas_linearization ) {
  // Vertical linearization out of the infeasibility certificate
  Index k = 0;
  for( auto j : subset )
   g[ k++ ] = v_linearization[ j ];
 }
 else {
  // Vertical linearization (per-asset columns, see vertical_linearization())
  std::vector< double > vl;
  vertical_linearization( vl );
  Index k = 0;
  for( auto j : subset )
   g[ k++ ] = vl[ j ];
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
 else if( f_has_farkas_linearization ) {
  // Vertical linearization out of the infeasibility certificate
  for( auto j : subset )
   g.coeffRef( j ) = v_linearization[ j ];
 }
 else {
  // Vertical linearization (per-asset columns, see vertical_linearization())
  std::vector< double > vl;
  vertical_linearization( vl );
  for( auto j : subset )
   g.coeffRef( j ) = vl[ j ];
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
  else if( f_has_farkas_linearization ) {
   // Vertical linearization out of the infeasibility certificate. F is
   // affine in the design and the cut is F <= 0, so the constant follows
   // from its value exactly as the diagonal one follows from that of the
   // Function; the values read here are the ones the coefficients are
   // relative to, so a reformulated bound needs no undoing
   auto alpha = f_farkas_value;
   for( Index i = 0 ; i < v_linearization.size() ; ++i )
    alpha -= v_linearization[ i ] * get_var_value( i );

   return( alpha );
  }
  else {
   // Vertical linearization out of a violated implicit constraint
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

Function::FunctionValue InvestmentFunction::get_value( void )
{
 if( f_has_value )
  return( f_value );

 return( worst_value() );

 }  // end ( InvestmentFunction::get_value )

/*--------------------------------------------------------------------------*/

double InvestmentFunction::compute_linear_constraint_value( Index i ) const {
 // per-asset semantics: column j multiplies the variable of ASSET j
 double value = 0;
 for( Index j = 0 ; j < v_A[ i ].size() ; ++j )
  value += v_A[ i ][ j ] * get_var_value( asset_var( j ) , false );
 return( value );
}

/*--------------------------------------------------------------------------*/

bool InvestmentFunction::constraints_are_satisfied( void ) {

 Index start = 0;
 if( f_violated_constraint.first < Inf< Index >() )
  start = f_violated_constraint.first + 1;

 for( Index i = start ; i < v_A.size() ; ++i ) {
  auto constraint_value = compute_linear_constraint_value( i );

  // Lower bound: declare infeasible only if the relative violation exceeds
  // f_constraints_tolerance (with the standard "max(1, |rhs|)" denominator
  // to handle zero/tiny bounds).
  {
   auto lower_violation = v_constraints_lower_bound[ i ] - constraint_value;
   if( lower_violation > 0 ) {
    lower_violation /=
     std::max( decltype( v_constraints_lower_bound )::value_type( 1 ) ,
               std::abs( v_constraints_lower_bound[ i ] ) );
    if( lower_violation > f_constraints_tolerance ) {
     f_violated_constraint = { i , eLHS };
     return( false );
     }
    }
   }

  // Upper bound: symmetric check.
  {
   auto upper_violation = constraint_value - v_constraints_upper_bound[ i ];
   if( upper_violation > 0 ) {
    upper_violation /=
     std::max( decltype( v_constraints_upper_bound )::value_type( 1 ) ,
               std::abs( v_constraints_upper_bound[ i ] ) );
    if( upper_violation > f_constraints_tolerance ) {
     f_violated_constraint = { i , eRHS };
     return( false );
     }
    }
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
 if( const auto inner_block = get_ucblock( 0 , 0 ) )
  return( inner_block->get_objective_sense() );
 // an inner Block with no UCBlock is an error that compute() reports at the
 // worst value, whose sign is asked here: the inner Block answers for its
 // sub-Blocks, so that the report does not fail in turn
 if( v_Block.empty() || ( ! v_Block.front() ) )
  return( Objective::eUndef );
 return( v_Block.front()->get_objective_sense() );
}

/*--------------------------------------------------------------------------*/

UCBlock * InvestmentFunction::get_ucblock( Index stage , Index i ) const {
 if( auto ucblock = get_ucblock() )
  return( ucblock );
 // in a TwoStageStochasticBlock the here-and-now Variable live in the Block
 // the first-stage AbstractPath are resolved against, which for a plain one
 // is the scenario sub-Block itself and for a nested one is the sub-Block
 // the nesting descends to
 if( const auto tssb = get_tssb_block() )
  // every leaf of the scenario structure carries its own copy of the units
  // the investment scales, and a nested one has more leaves than scenarios
  return( dynamic_cast< UCBlock * >( tssb->get_leaf_block( i ) ) );
 if( v_Block.size() > 1 )
  assert( i < v_Block.size() );
 else
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

TwoStageStochasticBlock * InvestmentFunction::get_tssb_block() const {
 assert( ! v_Block.empty() );
 return( dynamic_cast< TwoStageStochasticBlock * >( v_Block.front() ) );
}

/*--------------------------------------------------------------------------*/

SDDPBlock * InvestmentFunction::get_sddp_block( Index i ) const {
 if( i >= v_Block.size() )
  return( nullptr );
 return( dynamic_cast< SDDPBlock * >( v_Block[ i ] ) );
}

/*--------------------------------------------------------------------------*/

CDASolver * InvestmentFunction::get_ucblock_solver( Index stage ,
                                                    Index i ) const {
 // for a TwoStageStochasticBlock the Solver to drive is the one of the
 // TwoStageStochasticBlock itself, which solves all the scenarios at once,
 // not the one of any single scenario
 if( const auto tssb = get_tssb_block() ) {
  if( ! tssb->get_registered_solvers().empty() )
   return( dynamic_cast< CDASolver * >
           ( tssb->get_registered_solvers().front() ) );
  return( nullptr );
  }

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

 // In the multi-replica path (v_Block.size() > 1) sub_block_index is the
 // index of the SDDPBlock replica; in the legacy single-Block path it is
 // the index of the sub-Block per stage within the (single) SDDPBlock.

 SDDPBlock * sddp_block = nullptr;
 BendersBlock * benders_block = nullptr;

 if( v_Block.size() > 1 ) {
  sddp_block = get_sddp_block( sub_block_index );
  assert( sddp_block );
  if( stage >= sddp_block->get_time_horizon() )
   throw( std::invalid_argument( "InvestmentFunction::get_benders_function: "
                                 "invalid stage index: " +
                                 std::to_string( stage ) ) );
  benders_block = static_cast< BendersBlock * >
   ( sddp_block->get_sub_Block( stage )->get_inner_block() );
  }
 else {
  sddp_block = get_sddp_block();
  assert( sddp_block );
  if( stage >= sddp_block->get_time_horizon() )
   throw( std::invalid_argument( "InvestmentFunction::get_benders_function: "
                                 "invalid stage index: " +
                                 std::to_string( stage ) ) );
  benders_block = static_cast< BendersBlock * >
   ( sddp_block->get_sub_Block( stage , sub_block_index )->get_inner_block() );
  }

 auto objective = static_cast< FRealObjective * >
  ( benders_block->get_objective() );

 return( static_cast< BendersBFunction * >( objective->get_function() ) );
}

/*--------------------------------------------------------------------------*/

void InvestmentFunction::reset_linearization() {
 v_linearization.assign( v_x.size() , 0 );
}

/*--------------------------------------------------------------------------*/

Function::FunctionValue
InvestmentFunction::compute_farkas_value( Index stage ,
                                          Index sub_block_index ) {
 auto block = get_ucblock( stage , sub_block_index );
 if( ! block )
  return( 0 );

 FunctionValue value = 0;

 const auto obj_sign =
  ( block->get_objective_sense() == Objective::eMin ) ? -1 : 1;

 // the multiplier of a two-sided row or bound belongs to the side its sign
 // points at, which is the same rule the linearization of a UnitBlock reads
 // its duals by
 auto add = [ & value , obj_sign ]( const auto & c ) {
  const auto dual = c.get_dual();
  if( dual == 0 )
   return;

  const auto lhs = c.get_lhs();
  const auto rhs = c.get_rhs();

  RowConstraint::RHSValue b;
  if( lhs == rhs )
   b = lhs;
  else if( ( lhs > -Inf< RowConstraint::RHSValue >() ) &&
           ( rhs < Inf< RowConstraint::RHSValue >() ) )
   b = ( obj_sign * dual >= 0 ) ? lhs : rhs;
  else
   b = ( rhs < Inf< RowConstraint::RHSValue >() ) ? rhs : lhs;


  // an infinite side carries no information: the multiplier of a row that
  // does not constrain anything is zero, and a stray infinity here would
  // poison the whole sum
  if( ( b <= -Inf< RowConstraint::RHSValue >() ) ||
      ( b >= Inf< RowConstraint::RHSValue >() ) )
   return;

  value -= dual * b;
  };

 std::queue< Block * > Q;
 Q.push( block );
 while( ! Q.empty() ) {
  auto b = Q.front();
  Q.pop();
  for( auto * sub : b->get_nested_Blocks() )
   Q.push( sub );

  b->for_each_constraint_group( [ & add ]( const BaseGroup & group ) {
    for_each_as_any_of< FRowConstraint , BoxConstraint , LB0Constraint ,
			UB0Constraint , LBConstraint , UBConstraint ,
			NNConstraint , NPConstraint >( group , add ); } );
  }

 return( value );
 }  // end( InvestmentFunction::compute_farkas_value )

/*--------------------------------------------------------------------------*/

void InvestmentFunction::add_asset_linearization
( Index stage , Index sub_block_index , std::vector< double > & linearization ,
  bool direction ) {

 const auto ucblock = get_ucblock( stage , sub_block_index );
 if( ! ucblock )
  throw( std::logic_error( "InvestmentFunction::update_linearization: the "
                           "inner Block has no UCBlock in stage " +
                           std::to_string( stage ) + " of sub-Block " +
                           std::to_string( sub_block_index ) ) );

 /* A kappa enters the constraints it appears in through their right-hand
  * side alone, so the set of designs that a certificate of infeasibility
  * proves infeasible is a half-space, and the certificate is a cut as it
  * stands. A scale factor instead multiplies the Variable of its UnitBlock
  * wherever the UCBlock uses them [see UnitBlock::scale()], hence it
  * multiplies the COLUMNS of that unit: a certificate then holds only over
  * the scales on which it stays dual feasible, and that interval ends at the
  * current scale. Measured on a replicated intermittent unit: the one
  * column the ray charges carries - 1 / scale against the linking constraint
  * and 1 against its own bound, at scale 1200 and again at scale 20000, so
  * the sharpest cut the certificate supports is x >= x_bar and it cuts
  * nothing away. There is therefore nothing to read here, rather than
  * something hard to read: an investment that can make the inner Block
  * infeasible has to be represented by a kappa.
  *
  * Which getter gives a cut is not known here, what it reads being unknown:
  * the instance says it [see "AssetFeasibilityCut" in serialize()], and
  * when it does not, the assets that it says to be replicated give none. */

 std::vector< double > answer;

 for( const auto & group : v_asset_groups ) {

  if( direction && ( ! group.feasibility_cut ) )
   throw( std::logic_error( "InvestmentFunction::update_linearization: the "
                            "coefficient of asset " +
                            std::to_string( group.assets.front() ) +
                            ", read by '" + group.linearization_name +
                            "', cannot be read out of an unbounded dual "
                            "direction." ) );

  answer.resize( group.indices.size() );
  std::invoke( *group.linearization , ucblock , Block::MF_dbl_msp( answer ) ,
               group.indices , group.ordered );

  for( Index k = 0 ; k < group.assets.size() ; ++k )
   linearization[ asset_var( group.assets[ k ] ) ] += answer[ k ];
  }
} // end( InvestmentFunction::add_asset_linearization )

/*--------------------------------------------------------------------------*/

void InvestmentFunction::update_linearization( Index sub_block_index ,
					       bool direction ) {

 const auto num_stages = get_number_stages();

 // which Solver produced the solution is already told by the state: the
 // greedy ones are built only in the SDDPBlock branch of compute(), which
 // has dispatched upstream, so an empty v_greedy_solvers *is* the answer.
 // Asking the type again would leave out any other inner Block, a
 // TwoStageStochasticBlock in particular, for which both casts are null
 auto * solver = v_greedy_solvers.empty()
                 ? get_ucblock_solver( 0 , sub_block_index )
                 : get_solver< CDASolver >( v_greedy_solvers[
						      sub_block_index ] );

 // Retrieve the dual solution. A dual direction is written where the dual
 // solution goes and is already in place, the caller having asked for it;
 // there is no primal solution to go with it.

 if( ! direction ) {
  if( solver && solver->has_dual_solution() )
   solver->get_dual_solution(); // TODO pass Configuration
  else
   throw( std::logic_error( "InvestmentFunction::update_linearization: "
                            "dual solution not available." ) );

  // Retrieve the primal solution.

  if( std::find( v_asset_type.begin() , v_asset_type.end() , eUnitBlock ) !=
      v_asset_type.end() ) {
   // The primal solution may only be necessary if there are UnitBlocks
   // subject to investment.
   if( solver && solver->has_var_solution() )
    solver->get_var_solution(); // TODO pass Configuration
   else
    throw( std::logic_error( "InvestmentFunction::update_linearization: "
                             "primal solution not available." ) );
  }
 }

 for( Index stage = 0 ; stage < num_stages ; ++stage )
  add_asset_linearization( stage , sub_block_index , v_linearization ,
                           direction );

}  // end( InvestmentFunction::update_linearization() )

/*--------------------------------------------------------------------------*/

void InvestmentFunction::update_linearization
( Index sub_block_index , std::vector< double > & linearization ) {

 // Multi-replica variant: identifies the appropriate solver in the
 // sub_block_index-th replica SDDPBlock, retrieves its dual/primal
 // solution, and accumulates the per-stage contributions into the
 // caller-supplied `linearization` vector.

 SDDPBlock * sddp_block = nullptr;
 if( v_Block.size() > 1 )
  sddp_block = get_sddp_block( sub_block_index );
 else
  sddp_block = get_sddp_block();

 if( ! sddp_block )
  throw( std::logic_error( "InvestmentFunction::update_linearization: the "
                           "multi-replica linearization update requires an "
                           "SDDPBlock inner Block." ) );

 const auto num_stages = sddp_block->get_time_horizon();

 auto solver = get_solver< CDASolver >( sub_block_index );

 if( solver && solver->has_dual_solution() )
  solver->get_dual_solution();
 else
  throw( std::logic_error( "InvestmentFunction::update_linearization: "
                           "dual solution not available." ) );

 if( std::find( v_asset_type.begin() , v_asset_type.end() , eUnitBlock ) !=
     v_asset_type.end() ) {
  if( solver && solver->has_var_solution() )
   solver->get_var_solution();
  else
   throw( std::logic_error( "InvestmentFunction::update_linearization: "
                            "primal solution not available." ) );
  }

 for( Index stage = 0 ; stage < num_stages ; ++stage )
  add_asset_linearization( stage , sub_block_index , linearization );

}  // end( InvestmentFunction::update_linearization, out variant )

/*--------------------------------------------------------------------------*/

void InvestmentFunction::resolve_asset_methods( void ) {

 if( f_methods_resolved )
  return;

 const auto num_assets = v_asset_indices.size();

 for( Index i = 0 ; i < num_assets ; ++i )
  if( ( v_asset_type[ i ] != eUnitBlock ) && ( v_asset_type[ i ] != eLine ) )
   throw( std::logic_error( "InvestmentFunction::resolve_asset_methods: "
                            "invalid type of asset " + std::to_string( i ) +
                            ": " + std::to_string( v_asset_type[ i ] ) ) );

 // the names of the two methods of each asset, and whether its getter gives
 // a feasibility cut: those of the instance, or worked out below

 auto setter_name = v_asset_setter_name;
 auto linearization_name = v_asset_linearization_name;
 auto feasibility_cut = v_asset_feasibility_cut;

 if( setter_name.empty() && num_assets ) {

  /* An instance written before the assets named their methods says how each
   * one is sized by AssetType and AssetMethod, and without AssetMethod not
   * even that: a unit is then resized if its class offers it, and replicated
   * otherwise, which is what such an instance has always meant. Being the
   * conversion of the instance to the names, this is the one place here
   * that looks at the class of a unit, and only to choose a name. */

  setter_name.resize( num_assets );
  linearization_name.resize( num_assets );
  const bool cuts_given = ! feasibility_cut.empty();
  if( ! cuts_given )
   feasibility_cut.assign( num_assets , 1 );

  for( Index i = 0 ; i < num_assets ; ++i ) {

   if( v_asset_type[ i ] == eLine ) {
    setter_name[ i ] = "UCBlock::resize_line";
    linearization_name[ i ] = "UCBlock::get_resize_line_linearization";
    continue;
    }

   int method;
   if( ! v_asset_method.empty() )
    method = v_asset_method[ i ];
   else {
    const auto ucblock = get_ucblock( 0 , 0 );
    if( ! ucblock )
     throw( std::logic_error( "InvestmentFunction::resolve_asset_methods: "
                              "asset " + std::to_string( i ) + " names no "
                              "method, and the inner Block has no UCBlock "
                              "to work it out of" ) );
    const auto & cls =
     ucblock->get_unit_block( v_asset_indices[ i ] )->classname();
    method = Block::get_method_fs< Block::MF_dbl_it , Block::Range >
              ( cls + "::resize" ) ? eResize : eReplicate;
    }

   if( method == eResize ) {
    setter_name[ i ] = "UCBlock::resize_unit";
    linearization_name[ i ] = "UCBlock::get_resize_unit_linearization";
    }
   else {
    setter_name[ i ] = "UCBlock::replicate";
    linearization_name[ i ] = "UCBlock::get_replicate_linearization";
    if( ! cuts_given )
     feasibility_cut[ i ] = 0;
    }
   }
  }

 // the assets naming the same methods go together, and are written and read
 // back with one call each [see update_blocks()]

 v_asset_groups.clear();

 for( Index i = 0 ; i < num_assets ; ++i ) {

  const bool cut = feasibility_cut.empty() || feasibility_cut[ i ];

  auto group = std::find_if( v_asset_groups.begin() , v_asset_groups.end() ,
                             [ & ]( const AssetGroup & g ) {
                              return( ( g.setter_name == setter_name[ i ] ) &&
                                      ( g.linearization_name ==
                                        linearization_name[ i ] ) &&
                                      ( g.feasibility_cut == cut ) );
                              } );

  if( group == v_asset_groups.end() ) {

   AssetGroup g;
   g.setter_name = setter_name[ i ];
   g.linearization_name = linearization_name[ i ];
   g.feasibility_cut = cut;

   g.setter = Block::get_method_fs< Block::MF_dbl_it , Block::Subset && ,
                                    bool >( g.setter_name );
   if( ! g.setter )
    throw( std::logic_error( "InvestmentFunction::resolve_asset_methods: "
                             "asset " + std::to_string( i ) + " is written "
                             "by '" + g.setter_name + "', which the methods "
                             "factory does not have as a setter taking a "
                             "Subset." ) );

   g.linearization = Block::get_query_fs< Block::MF_dbl_msp ,
                                          Block::c_Subset & , bool >
                      ( g.linearization_name );
   if( ! g.linearization )
    throw( std::logic_error( "InvestmentFunction::resolve_asset_methods: "
                             "asset " + std::to_string( i ) + " is read by '"
                             + g.linearization_name + "', which the methods "
                             "factory does not have as a getter taking a "
                             "Subset." ) );

   v_asset_groups.push_back( std::move( g ) );
   group = std::prev( v_asset_groups.end() );
   }

  group->assets.push_back( i );
  group->indices.push_back( v_asset_indices[ i ] );
  }

 for( auto & g : v_asset_groups )
  g.ordered = std::is_sorted( g.indices.begin() , g.indices.end() );

 f_methods_resolved = true;

} // end( InvestmentFunction::resolve_asset_methods )

/*--------------------------------------------------------------------------*/

void InvestmentFunction::update_blocks() {

 // the Modification issued while writing the investment are this Function's
 // own: they are ignored up to the end, whichever way the end comes
 FlagGuard ignore_modifications( f_ignore_modifications , true );

 // the names are resolved once, the first time the investment is written
 resolve_asset_methods();

 // the investment in the assets of each group, in the order of the group
 std::vector< std::vector< double > > investment( v_asset_groups.size() );
 for( Index g = 0 ; g < v_asset_groups.size() ; ++g ) {
  investment[ g ].reserve( v_asset_groups[ g ].assets.size() );
  for( const auto asset : v_asset_groups[ g ].assets )
   investment[ g ].push_back( get_var_value( asset_var( asset ) , false ) );
  }

 const auto num_stages = get_number_stages();

 for( Index i = 0 ; i < get_number_investment_sub_blocks() ; ++i )
  for( Index stage = 0 ; stage < num_stages ; ++stage ) {

   const auto ucblock = get_ucblock( stage , i );
   if( ! ucblock )
    throw( std::logic_error( "InvestmentFunction::update_blocks: the inner "
                             "Block has no UCBlock in stage " +
                             std::to_string( stage ) + " of sub-Block " +
                             std::to_string( i ) ) );

   for( Index g = 0 ; g < v_asset_groups.size() ; ++g ) {
    const auto & group = v_asset_groups[ g ];
    try {
     std::invoke( *group.setter , ucblock , investment[ g ].cbegin() ,
                  Subset( group.indices ) , group.ordered , eNoBlck ,
                  eNoBlck );
     }
    catch( const std::exception & e ) {
     throw( std::logic_error( "InvestmentFunction::update_blocks: writing "
                              "asset " + std::to_string( group.assets.front() )
                              + " and the others written by '" +
                              group.setter_name + "': " + e.what() ) );
     }
    }
   }

 f_blocks_are_updated = true;
}  // end( InvestmentFunction::update_blocks )

/*--------------------------------------------------------------------------*/

Index InvestmentFunction::get_number_investment_sub_blocks( void ) const {
 if( get_ucblock() )
  return( 1 );
 if( const auto tssb = get_tssb_block() )
  // the investment is the same in every leaf, being here-and-now, but it has
  // to be written into each of them, and their number is not the number of
  // scenarios once a scenario carries a subtree of its own
  return( tssb->get_number_leaves() );
 return( f_num_sub_blocks_per_stage );
}  // end( InvestmentFunction::get_number_investment_sub_blocks )

/*--------------------------------------------------------------------------*/

void InvestmentFunction::send_nuclear_modification
( const Observer::ChnlName chnl ) {
 // "nuclear modification" for Function: everything changed
 global_pool.invalidate();
 f_blocks_are_updated = false;
 // what the names of the assets resolve to may depend on the inner Block,
 // when they are worked out of the classes of its units
 v_asset_groups.clear();
 f_methods_resolved = false;
 if( f_Observer )
  f_Observer->add_Modification
   ( std::make_shared< FunctionMod >( this , FunctionMod::NaNshift ) , chnl );
}  // end( InvestmentFunction::send_nuclear_modification )


/*--------------------------------------------------------------------------*/

Index InvestmentFunction::get_number_scenarios() const {
 if( v_Block.empty() )
  return( 0 );
 // the cast has to be the checked one: this is called from the SDDP paths,
 // but nothing in the signature says so, and a static_cast of an inner Block
 // that is a UCBlock or a TwoStageStochasticBlock is undefined behaviour
 const auto sddp_block = get_sddp_block();
 if( ! sddp_block )
  throw( std::logic_error( "InvestmentFunction::get_number_scenarios: the "
                           "inner Block is not an SDDPBlock." ) );
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

  ::deserialize( group , "InvestmentFunction_Constants" , global_pool_size ,
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
