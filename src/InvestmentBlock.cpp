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
 * \author Antonio Frangioni \n
 *         Dipartimento di Informatica \n
 *         Universita' di Pisa \n
 *
 * \copyright &copy; by Rafael Durbano Lobato, Antonio Frangioni
 */
/*--------------------------------------------------------------------------*/
/*---------------------------- IMPLEMENTATION ------------------------------*/
/*--------------------------------------------------------------------------*/
/*------------------------------ INCLUDES ----------------------------------*/
/*--------------------------------------------------------------------------*/

#include "InvestmentBlock.h"

#include "OneVarConstraint.h"

#include "FRowConstraint.h"

#include "LinearFunction.h"

#include "AbstractBlock.h"

#include "AbstractPath.h"

#include "Group.h"


#include <algorithm>

#include <cctype>

#include <map>

#include <memory>

#include <iostream>

#include <set>

#include <tuple>

/*--------------------------------------------------------------------------*/
/*------------------------- NAMESPACE AND USING ----------------------------*/
/*--------------------------------------------------------------------------*/

using namespace SMSpp_di_unipi_it;

/*--------------------------------------------------------------------------*/
/*------------------ FILE-LOCAL COMPONENT NAVIGATION -----------------------*/
/*--------------------------------------------------------------------------*/

// reach the InvestmentFunction of the k-th disaggregated component: each
// component sub-Block is a bare AbstractBlock whose FRealObjective wraps one
// InvestmentFunction (as add_component() builds it). The defensive
// dynamic_cast + throw mirrors how the consumer of these blocks navigates
// them (see BundleSolver: one FRealObjective / C05Function per component
// sub-Block).

static InvestmentFunction * component_function( const Block * IB ,
						Block::Index k )
{
 auto sub = dynamic_cast< AbstractBlock * >( IB->get_nested_Block( k ) );
 if( ! sub )
  throw( std::invalid_argument( "InvestmentBlock: component " +
	 std::to_string( k ) + " is not an AbstractBlock" ) );
 auto obj = dynamic_cast< FRealObjective * >( sub->get_objective() );
 if( ! obj )
  throw( std::invalid_argument( "InvestmentBlock: component " +
	 std::to_string( k ) + " has no FRealObjective" ) );
 auto fk = dynamic_cast< InvestmentFunction * >( obj->get_function() );
 if( ! fk )
  throw( std::invalid_argument( "InvestmentBlock: component " +
	 std::to_string( k ) + " has no InvestmentFunction" ) );
 return( fk );
 }

/*--------------------------------------------------------------------------*/

// collect the InvestmentFunction(s) to read from / write into: exactly one in
// the legacy path (the one in the master objective) or K in the disaggregated
// path (one per component sub-Block, since the master objective is then
// empty).
// This is the single load-bearing "legacy vs 1:K" dispatch; read() and write()
// share it so the two paths cannot drift apart.

static std::vector< InvestmentFunction * > component_functions(
                                                         const Block * IB )
{
 std::vector< InvestmentFunction * > funcs;
 if( auto IF = dynamic_cast< InvestmentFunction * >(
        static_cast< const InvestmentBlock * >( IB )->get_function() ) )
  funcs.push_back( IF );                                    // legacy: 1
 else
  for( Block::Index k = 0 ; k < IB->get_number_nested_Blocks() ; ++k )
   funcs.push_back( component_function( IB , k ) );   // disaggregated: K
 return( funcs );
 }

/*--------------------------------------------------------------------------*/

// a limit on the change of a design Variable x over its baseline, lo <= x -
// b - q <= hi, the baseline being either another design Variable b or the
// InstalledQuantity q [see InvestmentFunction::set_variation_limits()]

struct VariationLimit {
 ColVariable * x;
 ColVariable * b;   // nullptr if the baseline is the datum q
 double q;
 double lo;
 double hi;
 Block::Index k;    // the first component giving it, for the messages
 Block::Index i;    // and its asset there
 };

// the limits that the components give, one for each pair of x and baseline,
// ordered as the design Variable: components sharing the pair, as the
// scenarios of a period with a common investment do, give it once, and two of
// them giving it different limits make it throw, the change of one decision
// having one limit

static std::map< std::tuple< Block::Index , Block::Index , double > ,
                 VariationLimit > variation_limits( const Block * IB )
{
 const auto x0 = static_cast< const InvestmentBlock * >( IB )->
                                                   get_variables().data();
 std::map< std::tuple< Block::Index , Block::Index , double > ,
           VariationLimit > limits;
 const auto funcs = component_functions( IB );
 for( Block::Index k = 0 ; k < funcs.size() ; ++k ) {
  const auto f = funcs[ k ];
  const auto & avi = f->get_asset_variable_indices();
  const auto & bvi = f->get_asset_baseline_variable_indices();
  for( Block::Index i = 0 ; i < f->get_asset_indices().size() ; ++i ) {
   const double hi = f->get_max_investment( i );
   const double lo = - f->get_max_disinvestment( i );
   if( ( hi == Inf< double >() ) && ( lo == - Inf< double >() ) )
    continue;
   const auto x = static_cast< ColVariable * >(
                       f->get_active_var( avi.empty() ? i : avi[ i ] ) );
   const auto b = bvi.empty() ? nullptr :
                 static_cast< ColVariable * >( f->get_active_var( bvi[ i ] ) );
   const double q = b ? 0 : f->get_installed_quantity( i );
   const auto [ it , added ] = limits.try_emplace(
                  { Block::Index( x - x0 ) ,
                    b ? Block::Index( b - x0 ) : Inf< Block::Index >() , q } ,
                  VariationLimit{ x , b , q , lo , hi , k , i } );
   if( ( ! added ) && ( ( it->second.lo != lo ) || ( it->second.hi != hi ) ) )
    throw( std::logic_error( "InvestmentBlock: the components " +
           std::to_string( it->second.k ) + " (asset " +
           std::to_string( it->second.i ) + ") and " + std::to_string( k ) +
           " (asset " + std::to_string( i ) + ") give the change of the same "
           "design Variable over the same baseline different MaxInvestment "
           "or MaxDisinvestment" ) );
   }
  }
 return( limits );
 }

/*--------------------------------------------------------------------------*/
/*------------------- FILE-LOCAL: THE DESIGN MODE --------------------------*/
/*--------------------------------------------------------------------------*/

// An inner Block in design mode decides the investment itself, with the
// design Variable of its units and lines, and pays it in its own Objective,
// whereas under an InvestmentBlock the investment is the ColVariable of the
// InvestmentBlock. Such an instance is refused: design_converter [see
// tools/design_converter.cpp] converts its file into one of InvestmentBlock,
// each design an asset. Telling the design mode is the only thing here that
// knows the format of the classes of the assets.

// the scalar variable name of g, def if g does not have it

static double scalar( const netCDF::NcGroup & g , const std::string & name ,
                      double def )
{
 ::deserialize( g , def , name );
 return( def );
 }

/*--------------------------------------------------------------------------*/

// the "type" attribute of g, the empty string if it has none

static std::string type_of( const netCDF::NcGroup & g )
{
 std::string type;
 if( auto att = g.getAtt( "type" ) ; ! att.isNull() )
  att.getValues( type );
 return( type );
 }

/*--------------------------------------------------------------------------*/

// whether the group g describes, or holds, a unit or a network in design
// mode: a unit with an investment cost, a DesignNetworkBlock

static bool has_design( const netCDF::NcGroup & g )
{
 const auto type = type_of( g );
 if( type == "DesignNetworkBlock" )
  return( true );
 if( ( type == "IntermittentUnitBlock" ) || ( type == "ThermalUnitBlock" ) ||
     ( type == "NuclearUnitBlock" ) || ( type == "ConversionUnitBlock" ) )
  return( scalar( g , "InvestmentCost" , 0 ) != 0 );
 if( type == "BatteryUnitBlock" )
  return( ( scalar( g , "BatteryInvestmentCost" , 0 ) != 0 ) ||
          ( scalar( g , "ConverterInvestmentCost" , 0 ) != 0 ) );

 for( const auto & sub : g.getGroups() )
  if( has_design( sub.second ) )
   return( true );
 return( false );
 }

/*--------------------------------------------------------------------------*/
/*----------------------------- STATIC MEMBERS -----------------------------*/
/*--------------------------------------------------------------------------*/

SMSpp_insert_in_factory_cpp_1( InvestmentBlock );

SMSpp_insert_in_factory_cpp_0( InvestmentBlockSolution );

/*--------------------------------------------------------------------------*/
/*------------------------- METHODS of InvestmentBlock ---------------------*/
/*--------------------------------------------------------------------------*/

/*--------------------------------------------------------------------------*/
/*-------------- CONSTRUCTING AND DESTRUCTING InvestmentBlock --------------*/
/*--------------------------------------------------------------------------*/

// a "ScenarioGenerator" group in \p grp asks for a StochasticBlock template
// to be expanded, which is not done: the scenarios are written as a
// TwoStageStochasticBlock, which is split [see split_inner()]

static void refuse_generator( const netCDF::NcGroup & grp ,
                              const std::string & where )
{
 if( ! grp.getGroup( "ScenarioGenerator" ).isNull() )
  throw( std::logic_error( "InvestmentBlock::deserialize: " + where +
         " has a 'ScenarioGenerator' group, but a StochasticBlock template "
         "is not expanded: write its scenarios as a "
         "TwoStageStochasticBlock" ) );
 }

/*--------------------------------------------------------------------------*/

void InvestmentBlock::wire_component_actives( InvestmentFunction * f ,
                                              Index k )
{
 // derive the component's active subset from the file's global mappings:
 // U = sorted union of the asset variables (identity 0..NumAssets-1 when
 // AssetVarIndex is absent) and the baseline variables. Sorted order => the
 // BundleSolver increasing-union-order rule holds for the per-period chain
 // layout. A full-identity component degrades to the dense wiring,
 // byte-identical to the historical behaviour.
 const auto na = Index( f->get_asset_indices().size() );
 const auto & gavi = f->get_asset_variable_indices();
 const auto & gbvi = f->get_asset_baseline_variable_indices();

 std::set< Index > U;
 for( Index a = 0 ; a < na ; ++a )
  U.insert( gavi.empty() ? a : gavi[ a ] );
 U.insert( gbvi.begin() , gbvi.end() );

 for( auto g : U )
  if( g >= v_variables.size() )
   throw( std::logic_error( "InvestmentBlock::deserialize: variable index " +
	  std::to_string( g ) + " of Component_" + std::to_string( k ) +
	  " is out of range (the root has " +
	  std::to_string( v_variables.size() ) + " design variables)." ) );

 if( U.empty() )
  return;   // no assets: add_component() wires ALL the design variables

 const std::vector< Index > sorted_U( U.begin() , U.end() );
 std::vector< ColVariable * > p;
 p.reserve( sorted_U.size() );
 for( auto g : sorted_U )
  p.push_back( & v_variables[ g ] );

 auto local = [ & sorted_U ]( Index g ) {
  return( Index( std::lower_bound( sorted_U.begin() , sorted_U.end() , g )
		 - sorted_U.begin() ) );
  };

 // translate the mappings global -> local position in the subset; a mapping
 // that reduces to the identity stays implicit (empty), keeping the
 // dense/legacy serialize byte-identical. Note that an absent AssetVarIndex
 // (global identity, asset a <-> variable a) always translates to the local
 // identity: the identity block 0..na-1 sorts first in U, so local( a ) == a.
 std::vector< Index > lavi , lbvi;
 if( ! gavi.empty() ) {
  lavi.reserve( na );
  for( auto g : gavi )
   lavi.push_back( local( g ) );
  bool identity = true;
  for( Index a = 0 ; a < lavi.size() ; ++a )
   if( lavi[ a ] != a ) { identity = false; break; }
  if( identity )
   lavi.clear();
  }
 lbvi.reserve( gbvi.size() );
 for( auto g : gbvi )
  lbvi.push_back( local( g ) );

 f->set_variables( std::move( p ) );
 f->set_asset_variable_indices( std::move( lavi ) );
 f->set_asset_baseline_variable_indices( std::move( lbvi ) );

 }  // end( InvestmentBlock::wire_component_actives )

/*--------------------------------------------------------------------------*/

bool InvestmentBlock::split_inner( const netCDF::NcGroup & grp , Index k ,
                                   Index & num_components ,
                                   bool & all_unit_weights )
{
 if( ! f_scenario_decomposition )
  return( false );   // left whole, as the file asks

 // an inner Block of the class of the methods of the assets is its own
 // piece, and an SDDPBlock has none [see InvestmentFunction::set_pieces()]:
 // neither is built here, so that it is built once
 const auto inner_grp = grp.getGroup( "InnerBlock" );
 if( inner_grp.isNull() )
  return( false );
 const auto cls = InvestmentFunction::asset_class( grp );
 const auto type = type_of( inner_grp );
 if( ( type == cls ) || ( type == "SDDPBlock" ) )
  return( false );

 // any other is built, and split if it is made of the Blocks of that class,
 // each a copy of the assets [see InvestmentFunction::find_pieces()]: one
 // component per piece, as nothing is generated yet. What ties the pieces is
 // seen only once they are generated, after their BlockConfig, and a split
 // inner Block whose pieces something ties is refused then [see
 // check_parked()]
 std::unique_ptr< Block > root( Block::new_Block( inner_grp , this ) );
 std::vector< Block * > pieces;
 if( ( ! root ) ||
     ( ! InvestmentFunction::find_pieces( root.get() , cls , pieces ) ) ||
     ( ( pieces.size() == 1 ) && ( pieces.front() == root.get() ) ) ) {
  return( false );
  }

 // a piece goes to its component only once the component is in place: until
 // then it belongs to the root alone, since add_component() deletes a
 // rejected function, and with it its inner Block. Should a piece be
 // rejected, the pieces handed out so far go back under their fathers, and
 // the root deletes them with itself
 const Index first = get_number_nested_Blocks();
 std::vector< Block * > fathers( pieces.size() , nullptr );
 try {
  for( Index l = 0 ; l < pieces.size() ; ++l ) {
   fathers[ l ] = pieces[ l ]->get_f_Block();
   // the piece leaves the Blocks above it, so it takes their weights: the
   // weights of the Objective from it up to this InvestmentBlock [see
   // Block::get_objective_weight()], which its component applies
   const auto w_l = pieces[ l ]->get_objective_weight( this );
   pieces[ l ]->set_objective_weight( w_l , eNoMod );
   auto f_l = std::make_unique< InvestmentFunction >();
   f_l->deserialize( grp , pieces[ l ] );
   f_l->set_inner_block( nullptr , false );
   wire_component_actives( f_l.get() , k );
   add_component( f_l.release() );
   component_function( this , first + l )->set_inner_block( pieces[ l ] ,
                                                           false );
   if( w_l != 1.0 )
    all_unit_weights = false;
   ++num_components;
   }
  }
 catch( ... ) {
  for( Index c = first ; c < get_number_nested_Blocks() ; ++c )
   component_function( this , c )->set_inner_block( nullptr , false );
  for( Index l = 0 ; l < pieces.size() ; ++l )
   if( fathers[ l ] )
    pieces[ l ]->set_f_Block( fathers[ l ] );
  throw;
  }

 v_parked.push_back( { root.release() , first , k ,
                       std::move( pieces ) , std::move( fathers ) } );
 return( true );

 }  // end( InvestmentBlock::split_inner )


/*--------------------------------------------------------------------------*/

void InvestmentBlock::check_parked( void )
{
 for( auto & parked : v_parked ) {
  if( parked.checked )
   continue;

  // the pieces are generated by their own Solver, with their BlockConfig,
  // when it is registered [see Solver::set_Block()]; Block has no way of
  // telling whether it is generated, while a Solver registered on a Block
  // has generated it. A piece with no Solver yet would be generated here,
  // before its BlockConfig, by the root: refused
  for( Index l = 0 ; l < parked.pieces.size() ; ++l )
   if( parked.pieces[ l ]->get_registered_solvers().empty() )
    throw( std::logic_error( "InvestmentBlock::generate_abstract_constraints:"
           " the inner Block of the component " +
           std::to_string( parked.first + l ) + " has no Solver yet: "
           "configure the inner Blocks (register their Solver) before the "
           "InvestmentBlock generates its Constraint, which checks that the "
           "pieces of the split " + parked.root->classname() + " of "
           "Component_" + std::to_string( parked.k ) + " share nothing" ) );

  // the root and every Block between it and the pieces must have nothing of
  // their own: what they have ties the pieces, which then are not separable
  parked.root->generate_abstract_variables();
  parked.root->generate_abstract_constraints();
  if( const auto tie = first_with_own( parked.root , parked.pieces ) )
   throw( std::logic_error( "InvestmentBlock::generate_abstract_constraints:"
           " the components " + std::to_string( parked.first ) + " to " +
           std::to_string( parked.first + parked.pieces.size() - 1 ) +
           " are the pieces of the " + parked.root->classname() + " of "
           "Component_" + std::to_string( parked.k ) + ", and the " +
           tie->classname() + " that holds them has Variable or Constraint "
           "of its own, which tie them: write a ScenarioDecomposition of 0 "
           "to keep it whole" ) );
  parked.checked = true;
  }
 }

/*--------------------------------------------------------------------------*/

const Block * InvestmentBlock::first_with_own( const Block * block ,
                                        const std::vector< Block * > & pieces )
{
 if( std::find( pieces.begin() , pieces.end() , block ) != pieces.end() )
  return( nullptr );
 if( ( ! block->get_static_variable_groups().empty() ) ||
     ( ! block->get_dynamic_variable_groups().empty() ) ||
     ( ! block->get_static_constraint_groups().empty() ) ||
     ( ! block->get_dynamic_constraint_groups().empty() ) )
  return( block );
 for( const auto sub : block->get_nested_Blocks() )
  if( const auto tie = first_with_own( sub , pieces ) )
   return( tie );
 return( nullptr );
 }



/*--------------------------------------------------------------------------*/

void InvestmentBlock::add_component( InvestmentFunction * f )
{
 // f is adopted: on success an FRealObjective deletes it, so on failure it
 // must be freed here or it leaks

 // the structure must be defined before the abstract representation
 if( objective_generated() || constraints_generated() ) {
  delete f;
  throw( std::logic_error( "InvestmentBlock::add_component: "
                           "abstract representation already generated" ) );
  }

 if( v_variables.empty() ) {   // a component has no active Variable to bind to
  delete f;
  throw( std::logic_error( "InvestmentBlock::add_component: "
                           "no design variables" ) );
  }

 if( ! f )
  throw( std::invalid_argument( "InvestmentBlock::add_component: "
                                "null component" ) );

 // a component with no active Variable yet is bound to ALL the design
 // ColVariable, in their natural order (the common case); a component that
 // already has actives (the deserialize() path, or a caller picking a proper
 // subset) is left untouched
 if( ! f->get_num_active_var() ) {
  std::vector< ColVariable * > p;
  p.reserve( v_variables.size() );
  for( auto & variable : v_variables )
   p.push_back( & variable );
  try {
   f->set_variables( std::move( p ) );
   }
  catch( ... ) {
   delete f;
   throw;
   }
  }

 // one bare AbstractBlock per component, holding an FRealObjective over f:
 // BundleSolver sees one component per sub-Block
 auto sub = new AbstractBlock( this );
 f->set_f_Block( sub );
 auto obj = new FRealObjective( sub , f );
 obj->set_sense( Objective::eMin );          // convex component => eMin only
 sub->set_objective( obj , eNoMod );
 add_nested_Block( sub );
 }

/*--------------------------------------------------------------------------*/

std::vector< InvestmentFunction * >
InvestmentBlock::get_investment_functions( void ) const
{
 return( component_functions( this ) );
 }

/*--------------------------------------------------------------------------*/

InvestmentBlock::~InvestmentBlock()
{
 // each piece of a split inner Block goes back under its father, out of
 // a component that must not delete it with itself, and the root deletes
 // them all [see split_inner()]
 for( const auto & parked : v_parked ) {
  for( Index l = 0 ; l < parked.pieces.size() ; ++l ) {
   component_function( this , parked.first + l )->set_inner_block( nullptr ,
                                                                   false );
   parked.pieces[ l ]->set_f_Block( parked.fathers[ l ] );
   }
  delete parked.root;
  }

 for( auto block : v_Block )
  delete block;
 v_Block.clear();

 Constraint::clear( v_constraints );
 Constraint::clear( v_variation_constraints );

 objective.clear();
 }

/*--------------------------------------------------------------------------*/

void InvestmentBlock::deserialize( const netCDF::NcGroup & group )
{
 // an inner Block in design mode is refused [see has_design()]
 if( has_design( group ) )
  throw( std::logic_error( "InvestmentBlock::deserialize: the inner Block "
         "is in design mode, which is not read: convert the file with "
         "design_converter first" ) );

 Index num_assets = 0;

 if( ! deserialize_dim( group , "NumAssets" , num_assets ) )
  num_assets = 0;

 v_variables.resize( num_assets );
 for( auto & variable : v_variables )
  variable.set_Block( this );

 // one value for all the assets, or one each, and any other size refused
 InvestmentFunction::deserialize_one_each( group , "LowerBound" , num_assets ,
                                           v_lower_bound );
 InvestmentFunction::deserialize_one_each( group , "UpperBound" , num_assets ,
                                           v_upper_bound );
 InvestmentFunction::deserialize_one_each( group , "Integer" , num_assets ,
                                           v_integer );

 // the size of the dimension says the sense, as in BendersBlock: 0 is a
 // maximization, any other size, or no dimension, a minimization
 const auto objective_sense = group.getDim( "ObjectiveSense" );
 f_objective_sense = ( objective_sense.isNull() || objective_sense.getSize() )
                     ? Objective::eMin : Objective::eMax;

 Index scenario_decomposition = 1;
 deserialize_dim( group , "ScenarioDecomposition" , scenario_decomposition );
 if( scenario_decomposition > 1 )
  throw( std::logic_error( "InvestmentBlock::deserialize: the "
			   "'ScenarioDecomposition' dimension, if provided, "
			   "must be 0 or 1." ) );
 f_scenario_decomposition = scenario_decomposition;

 if( ! v_integer.empty() ) {
  for( Index i = 0 ; i < num_assets ; ++i )
   if( v_integer[ i ] )
    v_variables[ i ].is_integer( true , eNoMod );

  if( std::find_if( v_integer.begin() , v_integer.end() ,
                    []( int b ) { return( b != 0 ); } ) == v_integer.end() )
   v_integer.clear();
  }

 // bind every component to the SAME master design ColVariables, in the same
 // order (the BundleSolver "same active variables" rule); a fresh pointer
 // vector per call so set_variables() can move it
 auto master_var_pointers = [ this ]() {
  std::vector< ColVariable * > p;
  p.reserve( v_variables.size() );
  for( auto & variable : v_variables )
   p.push_back( & variable );
  return( p );
  };

 // discriminator: the presence of the group "Component_0" selects the
 // disaggregated (1:K) format; otherwise the legacy single-component format
 // (which stays byte-identical, since nothing below is written for K == 1)
 if( ! group.getGroup( "Component_0" ).isNull() ) {

  // ---- disaggregated (1:K) path ----

  // the root has no function of its own: the linear constraints and the
  // sense of each component are said by the component, and those written
  // at the root would be ignored -- fail loud instead
  if( ! group.getDim( "NumConstraints" , netCDF::NcGroup::Current ).isNull() )
   throw( std::logic_error( "InvestmentBlock::deserialize: the root of a "
	  "file with Component_<k> groups carries 'NumConstraints', but its "
     "linear constraints would be ignored: write them in a component." ) );
  for( const char * name : { "Constraints_A" , "Constraints_LowerBound" ,
			     "Constraints_UpperBound" } )
   if( ! group.getVar( name ).isNull() )
    throw( std::logic_error( std::string( "InvestmentBlock::deserialize: the "
	   "root of a file with Component_<k> groups carries '" ) + name +
	   "', but its linear constraints would be ignored: write them in a "
	   "component." ) );
  const auto sense = group.getDim( "ObjectiveSense" ,
				   netCDF::NcGroup::Current );
  if( ( ! sense.isNull() ) && ( ! sense.getSize() ) )
   throw( std::logic_error( "InvestmentBlock::deserialize: the root of a "
	  "file with Component_<k> groups asks for a maximization, but every "
	  "component is minimized." ) );

  // access by numeric suffix => deterministic order; the count is implicit
  // (the loop stops at the first missing Component_<k> group)
  Index num_components = 0;
  bool all_unit_weights = true;
  refuse_generator( group , "the root" );

  Index k = 0;   // survives the loop: it is the count of consecutive
                 // Component_<k> groups read, checked against the total below
  for( ; ; ++k ) {
   auto grp_k = group.getGroup( "Component_" + std::to_string( k ) );
   if( grp_k.isNull() )
    break;

   refuse_generator( grp_k , "Component_" + std::to_string( k ) );

   // an inner Block made of Blocks of the class of the methods of the assets
   // is split, one component per such Block [see split_inner()]
   if( split_inner( grp_k , k , num_components , all_unit_weights ) )
    continue;

   auto f_k = new InvestmentFunction();
   try {   // f_k (and its inner Block) must not leak on a rejected file:
           // Block::new_Block swallows the exception and returns nullptr
   f_k->deserialize( grp_k );   // no actives yet: the mappings stay GLOBAL,
                                // as the file speaks (range-checked below)
   wire_component_actives( f_k , k );
   }
   catch( ... ) {
    delete f_k;
    throw;
    }

   add_component( f_k );   // wraps in a bare AbstractBlock
   if( f_k->get_nested_Block( 0 )->get_objective_weight() != 1.0 )
    all_unit_weights = false;
   ++num_components;
   }

  // the scan stopped at the first missing index k; a Component_<j> with j > k
  // would be silently dropped. Order is irrelevant (chaining binds by global
  // variable index, not by position), so this is a completeness check: count
  // the Component_* groups and require they be exactly the k consecutive ones
  // read -- fail loud on a gap, as every count-guarded deserializer does.
  Index n_component_groups = 0;
  for( const auto & named : group.getGroups() ) {
   const std::string & nm = named.first;
   if( ( nm.rfind( "Component_" , 0 ) == 0 ) &&
       ( nm.size() > 10 ) && std::isdigit( (unsigned char) nm[ 10 ] ) )
    ++n_component_groups;
   }
  if( n_component_groups != k ) {
   // find the first orphan index to name it in the message
   std::string orphan;
   for( Index j = k + 1 ; orphan.empty() &&
	  ( j <= k + n_component_groups ) ; ++j )
    if( ! group.getGroup( "Component_" + std::to_string( j ) ).isNull() )
     orphan = "Component_" + std::to_string( j );
   throw( std::logic_error( "InvestmentBlock::deserialize: a gap in the "
	  "Component_ numbering was found -- the scan loaded " +
	  std::to_string( k ) + " consecutive components (Component_0.." +
	  std::to_string( k ? k - 1 : 0 ) + ") but the group holds " +
	  std::to_string( n_component_groups ) + " Component_* groups" +
	  ( orphan.empty() ? "" : " (e.g. " + orphan + " is unreachable)" ) +
	  ". Number the components consecutively from 0." ) );
   }

  // guardrail: K > 1 components all at the default weight 1.0 almost always
  // means the per-component weights were forgotten (a weighted expectation
  // needs w_k = probability x discount). Warn loudly; do not fail.
  if( ( num_components > 1 ) && all_unit_weights )
   std::cerr << "InvestmentBlock::deserialize: WARNING - " << num_components
             << " components all have weight 1.0; if these are "
                "scenarios/periods of a weighted expectation, set the "
                "'ObjectiveWeight' of each 'InnerBlock' (probability x "
                "discount)." << std::endl;

  variation_limits( this );  // refuses the components that disagree on one

  Block::deserialize( group );
  return;
  }

 // ---- legacy single-inner path ----
 {
  // the components made by the inner Block at the root are minimized, as
  // those of Component_<k> groups are: a maximization asked at the root is
  // refused rather than ignored
  const auto refuse_maximization = [ this ]() {
   if( f_objective_sense == Objective::eMax )
    throw( std::logic_error( "InvestmentBlock::deserialize: the root asks "
	   "for a maximization, but its inner Block is separated into "
	   "components, each of which is minimized." ) );
   };
  Index num_components = 0;
  bool all_unit_weights = true;
  refuse_generator( group , "the root" );

  // an inner Block at the root is split as that of a Component_0 is
  if( split_inner( group , 0 , num_components , all_unit_weights ) ) {
   refuse_maximization();
   Block::deserialize( group );
   return;
   }
 }

 // ---- legacy single-component path (unchanged behaviour) ----
 auto investment_function = new InvestmentFunction();

 investment_function->set_variables( master_var_pointers() );

 investment_function->set_num_sub_blocks_per_stage(
					       f_num_sub_blocks_per_stage );
 investment_function->set_number_sub_blocks( f_num_sub_blocks );
 // as for a Component_<k>: the function must not leak on a rejected file
 try {
  investment_function->deserialize( group );
  }
 catch( ... ) {
  delete investment_function;
  throw;
  }
 set_function( investment_function );
 investment_function->set_f_Block( this );

 Block::deserialize( group );
 }


/*--------------------------------------------------------------------------*/
/*-------------------------- OTHER INITIALIZATIONS -------------------------*/
/*--------------------------------------------------------------------------*/

void InvestmentBlock::generate_abstract_variables( Configuration * stvv )
{
 if( variables_generated() )
  return; // variables have already been generated

 if( ! v_variables.empty() )
  add_static_variable( v_variables , "investment" );

 set_variables_generated();
 }

/*--------------------------------------------------------------------------*/

void InvestmentBlock::generate_objective( Configuration * objc )
{
 if( objective_generated() )
  return;  // Objective has already been generated

 if( ! is_disaggregated() ) {
  // single-component path: the InvestmentFunction lives in this Block's own
  // FRealObjective
  objective.set_sense( f_objective_sense );
  set_objective( & objective );
  }
 else
  // multi-component path: each component sub-Block already holds its own
  // FRealObjective (set at construction); just let them generate it
  for( Index k = 0 ; k < get_number_nested_Blocks() ; ++k )
   get_nested_Block( k )->generate_objective( objc );

 set_objective_generated();
 }

/*--------------------------------------------------------------------------*/

void InvestmentBlock::generate_abstract_constraints( Configuration * stcc )
{
 check_parked();  // [see split_inner()]

 if( constraints_generated() )
  return; // constraints have already been generated

 add_bound_constraints( stcc );  // the bounds of the design Variable
 add_variation_constraints();    // the limits on their change

 // marked only if there is a row: until then the bounds can still be set and
 // the components added [see set_variable_bounds() and add_component()]
 if( ! ( v_lower_bound.empty() && v_upper_bound.empty() &&
         v_variation_constraints.empty() ) )
  set_constraints_generated();
 }

/*--------------------------------------------------------------------------*/

void InvestmentBlock::add_bound_constraints( Configuration * stcc )
{
 if( v_lower_bound.empty() && v_upper_bound.empty() )
  return; // there is no bound constraint

 f_reformulate_bounds = 0;
 auto config = dynamic_cast< SimpleConfiguration< int > * >( stcc );
 if( ( ! config ) && f_BlockConfig )
  config = dynamic_cast< SimpleConfiguration< int > * >(
			f_BlockConfig->f_static_constraints_Configuration );
 if( config )
  f_reformulate_bounds = config->f_value;

 // each InvestmentFunction computes at the shifted Variable plus its own
 // lower bounds, which are those of this InvestmentBlock if it is the only
 // one; a component reads them out of its own group, so they must be those of
 // the design Variable it is active in, or it would compute at a wrong point
 for( auto function : component_functions( this ) ) {
  function->reformulated_bounds( f_reformulate_bounds );
  if( f_reformulate_bounds && is_disaggregated() )
   for( Index i = 0 ; i < function->get_num_active_var() ; ++i ) {
    const auto g = Index( static_cast< const ColVariable * >(
                        function->get_active_var( i ) ) - v_variables.data() );
    if( function->get_var_lower_bound( i ) !=
        ( v_lower_bound.empty() ? - Inf< double >() : v_lower_bound[ g ] ) )
     throw( std::logic_error( "InvestmentBlock::generate_abstract_constraints:"
                              " the bounds cannot be reformulated, the lower "
                              "bound of the active Variable " +
                              std::to_string( i ) + " of a component differs "
                              "from that of the design Variable " +
                              std::to_string( g ) ) );
    }
  }

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
   // shifting an integer Variable by its lower bound keeps it integer only
   // if the bound is
   if( f_reformulate_bounds && ( i < v_integer.size() ) && v_integer[ i ] &&
       std::isfinite( v_lower_bound[ i ] ) &&
       ( v_lower_bound[ i ] != std::floor( v_lower_bound[ i ] ) ) )
    throw( std::logic_error( "InvestmentBlock::generate_abstract_constraints:"
			     " the lower bound of an integer Variable must be"
			     " integer for the bounds to be reformulated" ) );

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
 }

/*--------------------------------------------------------------------------*/

void InvestmentBlock::add_variation_constraints( void )
{
 // the shift of a design Variable whose bounds are reformulated, whose value
 // is then the one of the asset minus its lower bound
 const auto shift = [ this ]( const ColVariable * x ) {
  if( ( ! x ) || ( ! f_reformulate_bounds ) || v_lower_bound.empty() )
   return( 0.0 );
  const auto lb = v_lower_bound[ x - v_variables.data() ];
  return( lb > - Inf< double >() ? lb : 0.0 );
  };

 const auto limits = variation_limits( this );
 v_variation_constraints.resize( limits.size() );
 auto row = v_variation_constraints.begin();
 for( const auto & [ pair , l ] : limits ) {
  LinearFunction::v_coeff_pair vars = { std::make_pair( l.x , 1.0 ) };
  if( l.b )
   vars.push_back( std::make_pair( l.b , -1.0 ) );
  const double s = l.q - shift( l.x ) + shift( l.b );
  row->set_lhs( l.lo + s );
  row->set_rhs( l.hi + s );
  ( row++ )->set_function( new LinearFunction( std::move( vars ) ) );
  }

 if( ! v_variation_constraints.empty() )
  add_static_constraint( v_variation_constraints , "variation_bounds" );
 }

/*--------------------------------------------------------------------------*/
/*----------------------- Methods for handling Solution --------------------*/
/*--------------------------------------------------------------------------*/

Solution * InvestmentBlock::get_Solution( Configuration *solc , bool emptys )
{
 int wsol = 1;
 Configuration * innr_cfg = nullptr;

 if( ( ! solc ) && f_BlockConfig )
  solc = f_BlockConfig->f_solution_Configuration;

 if( auto tsolc = dynamic_cast< SimpleConfiguration< int > * >( solc ) )
  wsol = tsolc->f_value;

 if( auto tsolc = dynamic_cast< SimpleConfiguration<
                                    std::pair< int ,  Configuration * > > *
                              >( solc ) ) {
  wsol = tsolc->f_value.first;
  innr_cfg = tsolc->f_value.second;
  }

 auto sol = new InvestmentBlockSolution();

 // wsol != 0 means "read the inner Block :Solution(s) when asked to"; the
 // actual K (1 legacy / K disaggregated) is resolved at read() time
 sol->f_inner_wanted = ( wsol != 0 );
 sol->f_inner_Configuration = innr_cfg;

 if( ! emptys )
  sol->read( this );

 return( sol );

 }  // end( InvestmentBlock::get_Solution )

/*--------------------------------------------------------------------------*/
/*------------- METHODS FOR Saving THE DATA OF THE InvestmentBlock ---------*/
/*--------------------------------------------------------------------------*/

void InvestmentBlock::serialize( netCDF::NcGroup & group ) const
{
 Block::serialize( group );

 group.putAtt( "type" , "InvestmentBlock" );

 auto NumAssets = group.addDim( "NumAssets" , v_variables.size() );

 if( f_objective_sense == Objective::eMax )
  group.addDim( "ObjectiveSense" , 0 );

 if( ! f_scenario_decomposition )
  group.addDim( "ScenarioDecomposition" , 0 );

 ::serialize( group , "LowerBound" , netCDF::NcDouble() , NumAssets ,
              v_lower_bound );

 ::serialize( group , "UpperBound" , netCDF::NcDouble() , NumAssets ,
              v_upper_bound );

 if( ! v_integer.empty() )
  ::serialize( group , "Integer" , netCDF::NcInt() , NumAssets , v_integer );

 if( ! is_disaggregated() ) {
  // legacy single-component: write the InvestmentFunction at the root group
  // (byte-identical to before: no Component_<k>), which it would leave
  // with its own type
  if( auto function = objective.get_function() ) {
   static_cast< InvestmentFunction * >( function )->serialize( group );
   group.putAtt( "type" , "InvestmentBlock" );
   }
  }
 else {
  // disaggregated (1:K): one Component_<k> group per nested sub-Block, each
  // the existing InvestmentFunction serialize (which writes its AssetVarIndex
  // when non-default); the count is implicit in the suffixes
  auto parked = v_parked.begin();
  for( Index k = 0 , j = 0 ; k < get_number_nested_Blocks() ; ++k , ++j ) {
   // reach the component's InvestmentFunction through the single navigation
   // helper (checked in debug), so serialize does not re-encode the component
   // structure sub-Block -> FRealObjective -> InvestmentFunction
   const auto f_k = component_function( this , k );
   auto grp_k = group.addGroup( "Component_" + std::to_string( j ) );
   // the components made out of the pieces of a split inner Block are
   // written as one, the inner Block [see split_inner()]
   if( ( parked != v_parked.end() ) && ( parked->first == k ) ) {
    f_k->serialize( grp_k , parked->root );
    k += parked->pieces.size() - 1;
    ++parked;
    }
   else
    f_k->serialize( grp_k );

   // the file speaks GLOBAL indices: when the component's actives are a
   // proper subset of the design variables (the per-period binding), its
   // mappings -- which are LOCAL positions in that subset -- must be
   // re-translated to global design indices before they hit the disk (the
   // exact inverse of the derivation done in deserialize). Dense components
   // have local == global and are left untouched (byte-identical legacy).
   const Index nact = f_k->get_num_active_var();
   bool dense_identity = ( nact == v_variables.size() );
   if( dense_identity )   // same size is NOT enough: the wiring must really
    for( Index l = 0 ; l < nact ; ++l )   // be the identity, or a full-size
     if( f_k->get_active_var( l ) != & v_variables[ l ] ) {  // permutation
      dense_identity = false;             // would silently corrupt the file
      break;
      }
   if( dense_identity )
    continue;
   std::vector< Index > l2g( nact );
   for( Index l = 0 ; l < nact ; ++l ) {
    auto vp = static_cast< const ColVariable * >( f_k->get_active_var( l ) );
    const auto g = Index( vp - v_variables.data() );
    if( g >= v_variables.size() )
     throw( std::logic_error( "InvestmentBlock::serialize: active variable "
	    + std::to_string( l ) + " of component " + std::to_string( k ) +
	    " is not a design variable of this InvestmentBlock" ) );
    l2g[ l ] = g;
    }
   auto na_dim = grp_k.getDim( "NumAssets" );
   const Index na = na_dim.isNull() ? 0 : Index( na_dim.getSize() );
   auto put_global = [ & ]( const std::string & name ,
			    const std::vector< Index > & localmap ,
			    bool identity_when_empty ) {
    if( localmap.empty() && ! identity_when_empty )
     return;
    std::vector< unsigned int > g( localmap.empty() ? na : localmap.size() );
    for( Index i = 0 ; i < g.size() ; ++i )
     g[ i ] = l2g[ localmap.empty() ? i : localmap[ i ] ];
    // a GLOBAL identity needs no field (absent already means exactly that):
    // keeps the round-trip stable when the subset is the prefix 0..na-1
    bool identity = true;
    for( Index i = 0 ; i < g.size() ; ++i )
     if( g[ i ] != i ) {
      identity = false;
      break;
      }
    if( identity && grp_k.getVar( name ).isNull() )
     return;
    auto var = grp_k.getVar( name );
    if( var.isNull() )
     var = grp_k.addVar( name , netCDF::NcUint() , na_dim );
    var.putVar( g.data() );
    };
   if( na ) {
    // AssetVarIndex: an implicit local identity over a subset is a NON
    // trivial global mapping (asset a -> l2g[ a ]) and must be written out
    put_global( "AssetVarIndex" , f_k->get_asset_variable_indices() , true );
    put_global( "AssetBaselineVarIndex" ,
		f_k->get_asset_baseline_variable_indices() , false );
    }
   }
  }
 }

/*--------------------------------------------------------------------------*/
/*---------------- Methods for checking the InvestmentBlock ----------------*/
/*--------------------------------------------------------------------------*/

bool InvestmentBlock::is_feasible( bool useabstract , Configuration * fsbc )
{
 if( v_variables.empty() )
  return( true );

 // Retrieve the tolerance.

 auto config = dynamic_cast< SimpleConfiguration< double > * >( fsbc );

 if( ( ! config ) && f_BlockConfig )
  config = dynamic_cast< SimpleConfiguration< double > * >(
			      f_BlockConfig->f_is_feasible_Configuration );

 // If a tolerance has not been provided, use the default tolerance.
 const auto tolerance = config ? config->f_value : 1.0e-8;

 if( useabstract && ( ! ( v_constraints.empty() &&
                             v_variation_constraints.empty() ) ) ) {
  // Use the set of Constraint to decide whether the current solution is
  // feasible.
  return( RowConstraint::is_feasible( v_constraints , tolerance , false ) &&
          RowConstraint::is_feasible( v_variation_constraints , tolerance ,
                                      false ) );
  }

 // Check the "physical representation"

 for( Index i = 0 ; i < v_lower_bound.size() ; ++i )
  if( v_lower_bound[ i ] > -Inf< double >() )
   if( v_variables[ i ].get_value() < v_lower_bound[ i ] - tolerance )
    return( false );

 for( Index i = 0 ; i < v_upper_bound.size() ; ++i )
  if( v_upper_bound[ i ] < Inf< double >() )
   if( v_variables[ i ].get_value() > v_upper_bound[ i ] + tolerance )
    return( false );

 for( const auto & [ pair , l ] : variation_limits( this ) ) {
  const double d = l.x->get_value() - ( l.b ? l.b->get_value() : 0 ) - l.q;
  if( ( d < l.lo - tolerance ) || ( d > l.hi + tolerance ) )
   return( false );
  }

 return( true );
 }

/*--------------------------------------------------------------------------*/
/*----------------- METHODS OF InvestmentBlockSolution ---------------------*/
/*--------------------------------------------------------------------------*/

void InvestmentBlockSolution::deserialize( const netCDF::NcGroup & group )
{
 // "NumDesignVariables" is mandatory - - - - - - - - - - - - - - - - - - - -
 Index num_design;
 deserialize_dim( group , "NumDesignVariables" , num_design , false );

 // deserialize the DesignVariables - - - - - - - - - - - - - - - - - - - - -
 ::deserialize< double >( group , "DesignVariables" , num_design ,
			  v_design , false );

 // deserialize the inner :Solution(s) - - - - - - - - - - - - - - - - - - - -
 for( auto s : v_inner_Solutions )
  delete s;
 v_inner_Solutions.clear();

 // legacy format: a single "InnerSolution" group
 auto sub_group = group.getGroup( "InnerSolution" );
 if( ! sub_group.isNull() )
  v_inner_Solutions.push_back( Solution::new_Solution( sub_group ) );
 else {
  // disaggregated format: "InnerSolution_<k>" groups, in numeric order;
  // "NumInnerSolutions", if present, is a round-trip guardrail
  Index num_inner = 0;
  const bool have_count =
   deserialize_dim( group , "NumInnerSolutions" , num_inner );
  for( Index k = 0 ; ; ++k ) {
   auto gk = group.getGroup( "InnerSolution_" + std::to_string( k ) );
   if( gk.isNull() ) {
    if( have_count && ( k != num_inner ) )
     throw( std::logic_error(
      "InvestmentBlockSolution::deserialize: NumInnerSolutions = " +
      std::to_string( num_inner ) + " but found " + std::to_string( k ) +
      " InnerSolution_<k> groups." ) );
    break;
    }
   v_inner_Solutions.push_back( Solution::new_Solution( gk ) );
   }
  }

 // a Solution deserialized with inner Solution(s) keeps refreshing them on
 // read(), exactly like one produced by get_Solution() with wsol != 0
 f_inner_wanted = ! v_inner_Solutions.empty();

 }  // end( InvestmentBlockSolution::deserialize )

/*--------------------------------------------------------------------------*/

void InvestmentBlockSolution::read( const Block * block )
{
 auto IB = dynamic_cast< const InvestmentBlock * >( block );
 if( ! IB )
  throw( std::invalid_argument(
	"InvestmentBlockSolution::read: block is not a InvestmentBlock" ) );

 v_design = IB->get_variable_values();
 if( ( ! IB->get_variable_lower_bound().empty() ) &&
     IB->get_reformulate_bounds() ) {
  for( Index i = 0 ; i < v_design.size() ; ++i )
   if( IB->get_variable_lower_bound()[ i ] > -Inf< double >() )
    v_design[ i ] += IB->get_variable_lower_bound()[ i ];
  }

 // collect the inner Block :Solution(s) if requested. The InvestmentFunction
 // to read from is 1 in the legacy path (the one in the master objective) or K
 // in the disaggregated path (one per component sub-Block, reached through
 // component_function() since the master objective is then empty).
 for( auto s : v_inner_Solutions )
  delete s;
 v_inner_Solutions.clear();

 if( ! f_inner_wanted )
  return;

 auto funcs = component_functions( IB );

 if( funcs.empty() )
  throw( std::invalid_argument(
	    "InvestmentBlockSolution::read: empty InvestmentFunction" ) );

 v_inner_Solutions.reserve( funcs.size() );
 for( auto fk : funcs ) {
  if( fk->get_nested_Blocks().empty() )
   throw( std::invalid_argument(
            "InvestmentBlockSolution::read: empty inner Block" ) );
  auto s = fk->get_nested_Blocks().front()->get_Solution(
                                              f_inner_Configuration , false );
  if( ! s )
   throw( std::invalid_argument(
     "InvestmentBlockSolution::read: cannot read desired inner Solution" ) );
  v_inner_Solutions.push_back( s );
  }
 }  // end( InvestmentBlockSolution::read )

/*--------------------------------------------------------------------------*/

void InvestmentBlockSolution::write( Block * block )
{
 auto IB = dynamic_cast< InvestmentBlock * >( block );
 if( ! IB )
  throw( std::invalid_argument(
         "InvestmentBlockSolution::write: block is not a InvestmentBlock" ) );

 if( v_design.size() != IB->get_number_variables() )
  throw( std::invalid_argument(
	    "InvestmentBlockSolution::write: inconsistent variables size" ) );

 if( ( ! IB->get_variable_lower_bound().empty() ) &&
     IB->get_reformulate_bounds() ) {
  auto td = v_design;
  for( Index i = 0 ; i < td.size() ; ++i )
   if( IB->get_variable_lower_bound()[ i ] > -Inf< double >() )
    td[ i ] -= IB->get_variable_lower_bound()[ i ];

  IB->set_variable_values< double >( td );
  }
 else
  IB->set_variable_values< double >( v_design );

 if( v_inner_Solutions.empty() )
  return;

 // the InvestmentFunction to write into: 1 in the legacy path, K in the
 // disaggregated path (reached through component_function()); the inner
 // :Solution count must match.
 auto funcs = component_functions( IB );

 if( funcs.empty() )
  throw( std::invalid_argument(
	   "InvestmentBlockSolution::write: empty InvestmentFunction" ) );

 if( v_inner_Solutions.size() != funcs.size() )
  throw( std::invalid_argument(
    "InvestmentBlockSolution::write: inconsistent inner Solution count" ) );

 for( Index k = 0 ; k < funcs.size() ; ++k ) {
  if( funcs[ k ]->get_nested_Blocks().empty() )
   throw( std::invalid_argument(
            "InvestmentBlockSolution::write: empty inner Block" ) );
  if( v_inner_Solutions[ k ] )
   v_inner_Solutions[ k ]->write( funcs[ k ]->get_nested_Blocks().front() );
  }
 }  // end( InvestmentBlockSolution::write )

/*--------------------------------------------------------------------------*/

void InvestmentBlockSolution::serialize( netCDF::NcGroup & group ) const
{
 Solution::serialize( group );

 auto ndv = group.addDim( "NumDesignVariables" , v_design.size() );

 ::serialize< double >( group , "DesignVariables" , netCDF::NcDouble() ,
			ndv , v_design );

 if( v_inner_Solutions.size() == 1 ) {
  // legacy format: a single "InnerSolution" group (byte-compatible)
  if( v_inner_Solutions.front() ) {
   auto sub_group = group.addGroup( "InnerSolution" );
   v_inner_Solutions.front()->serialize( sub_group );
   }
  }
 else if( ! v_inner_Solutions.empty() ) {
  // disaggregated format: one "InnerSolution_<k>" group per component
  group.addDim( "NumInnerSolutions" , v_inner_Solutions.size() );
  for( Index k = 0 ; k < v_inner_Solutions.size() ; ++k )
   if( v_inner_Solutions[ k ] ) {
    auto sub_group = group.addGroup( "InnerSolution_" + std::to_string( k ) );
    v_inner_Solutions[ k ]->serialize( sub_group );
    }
  }
 }  // end( InvestmentBlockSolution::serialize )

/*--------------------------------------------------------------------------*/

InvestmentBlockSolution * InvestmentBlockSolution::scale( double factor )
 const
{
 auto sol = clone();

 if( factor == 1 )
  return( sol );

 for( auto & i : sol->v_design )
  i *= factor;

 for( auto s : sol->v_inner_Solutions )
  if( s )
   s->scale( factor );

 return( sol );

 }  // end( InvestmentBlockSolution::scale )

/*--------------------------------------------------------------------------*/

void InvestmentBlockSolution::sum( const Solution * solution ,
				   double multiplier )
{
 auto IBS = dynamic_cast< const InvestmentBlockSolution * >( solution );
 if( ! IBS )
  throw( std::invalid_argument( "InvestmentBlockSolution::sum: solution is "
				"not a InvestmentBlockSolution" ) );

 if( v_design.size() != IBS->v_design.size() )
  throw( std::invalid_argument(
	    "InvestmentBlockSolution::sum: inconsistent variables size" ) );

 if( v_inner_Solutions.size() != IBS->v_inner_Solutions.size() )
  throw( std::invalid_argument(
	    "InvestmentBlockSolution::sum: inconsistent inner Solution" ) );

 auto dit = IBS->v_design.begin();
 for( auto & i : v_design )
  i += *(dit++) * multiplier;

 for( Index k = 0 ; k < v_inner_Solutions.size() ; ++k )
  if( v_inner_Solutions[ k ] && IBS->v_inner_Solutions[ k ] )
   v_inner_Solutions[ k ]->sum( IBS->v_inner_Solutions[ k ] , multiplier );

 }  // end( InvestmentBlockSolution::sum )

/*--------------------------------------------------------------------------*/

InvestmentBlockSolution * InvestmentBlockSolution::clone( bool empty ) const
{
 auto sol = new InvestmentBlockSolution();

 if( ! empty ) {
  sol->v_design = v_design;

  sol->v_inner_Solutions.reserve( v_inner_Solutions.size() );
  for( auto s : v_inner_Solutions )
   sol->v_inner_Solutions.push_back( s ? s->clone() : nullptr );

  sol->f_inner_wanted = f_inner_wanted;
  }

 return( sol );

 }  // end( InvestmentBlockSolution::clone )

/*--------------------------------------------------------------------------*/
/*--------------------- End File InvestmentBlock.cpp -----------------------*/
/*--------------------------------------------------------------------------*/
