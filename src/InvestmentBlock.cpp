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

#include "LinearFunction.h"

#include "AbstractBlock.h"


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

// the InvestmentFunction of the k-th component of IB: the k-th sub-Block is
// a bare AbstractBlock whose FRealObjective holds it, as add_component()
// builds it, and nothing else adds a sub-Block to an InvestmentBlock

static InvestmentFunction * component_function( const Block * IB ,
						Block::Index k )
{
 return( static_cast< InvestmentFunction * >( static_cast< FRealObjective * >(
          IB->get_nested_Block( k )->get_objective() )->get_function() ) );
 }

/*--------------------------------------------------------------------------*/

// the group of the component k, for the messages: Inf< Index >() is the root
// [see InvestmentBlock::split_inner()]

static std::string group_name( Block::Index k )
{
 return( k == Inf< Block::Index >() ? "the root"
                                    : "Component_" + std::to_string( k ) );
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
                 VariationLimit >
variation_limits( const InvestmentBlock * IB )
{
 const auto x0 = IB->get_variables().data();
 std::map< std::tuple< Block::Index , Block::Index , double > ,
           VariationLimit > limits;
 const auto funcs = IB->get_investment_functions();
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

// the "type" attribute of g, the empty string if it has none

static std::string type_of( const netCDF::NcGroup & g )
{
 std::string type;
 if( auto att = g.getAtt( "type" ) ; ! att.isNull() )
  att.getValues( type );
 return( type );
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

// K > 1 components of IB all of weight 1, that of the Objective of the inner
// Block of each [see InvestmentBlock::add_component()], almost always mean
// that the weights were forgotten: a weighted expectation needs probability
// x discount

static void warn_unit_weights( const InvestmentBlock * IB )
{
 const auto funcs = IB->get_investment_functions();
 if( ( funcs.size() > 1 ) &&
     std::all_of( funcs.begin() , funcs.end() , []( const auto f ) {
      return( f->get_nested_Block( 0 )->get_objective_weight() == 1.0 ); } ) )
  std::cerr << "InvestmentBlock::deserialize: WARNING - " << funcs.size()
            << " components all have weight 1.0; if these are "
               "scenarios/periods of a weighted expectation, set the "
               "'ObjectiveWeight' of each 'InnerBlock' (probability x "
               "discount)." << std::endl;
 }

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
 // layout. A full-identity component gets the dense wiring.
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
	  std::to_string( g ) + " of " + group_name( k ) +
	  " is out of range (the root has " +
	  std::to_string( v_variables.size() ) + " design variables)." ) );

 if( U.empty() )
  return;   // no assets: add_component() wires ALL the design variables

 const std::vector< Index > sorted_U( U.begin() , U.end() );

 auto local = [ & sorted_U ]( Index g ) {
  return( Index( std::lower_bound( sorted_U.begin() , sorted_U.end() , g )
		 - sorted_U.begin() ) );
  };

 // translate the mappings global -> local position in the subset; a mapping
 // that reduces to the identity stays implicit (empty). Note that an absent
 // AssetVarIndex (global identity, asset a <-> variable a) always translates
 // to the local identity: the identity block 0..na-1 sorts first in U, so
 // local( a ) == a.
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

 f->set_variables( design_vars( sorted_U ) );
 f->set_asset_variable_indices( std::move( lavi ) );
 f->set_asset_baseline_variable_indices( std::move( lbvi ) );

 }  // end( InvestmentBlock::wire_component_actives )

/*--------------------------------------------------------------------------*/

bool InvestmentBlock::split_inner( const netCDF::NcGroup & grp , Index k )
{
 if( ! f_scenario_decomposition )
  return( false );   // left whole, as the file asks

 // an inner Block of the class of the methods of the assets is its own
 // piece: it is not built here, so that it is built once
 const auto inner_grp = grp.getGroup( "InnerBlock" );
 if( inner_grp.isNull() )
  return( false );
 const auto cls = InvestmentFunction::asset_class( grp );
 if( type_of( inner_grp ) == cls )
  return( false );

 // any other is built, and split if its sub-Blocks are made of the Blocks
 // of that class, each a copy of the assets [see
 // InvestmentFunction::find_pieces()]: one component per piece, as nothing
 // is generated yet. Pieces behind a Function (the stages of a multi-stage
 // problem, inside the BendersBFunction of each) are not separate problems,
 // and the inner Block is not split. What ties the pieces is
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
           "pieces of the split " + parked.root->classname() + " of " +
           group_name( parked.k ) + " share nothing" ) );

  // the root and every Block between it and the pieces must have nothing of
  // their own: what they have ties the pieces, which then are not separable
  parked.root->generate_abstract_variables();
  parked.root->generate_abstract_constraints();
  if( const auto tie = first_with_own( parked.root , parked.pieces ) )
   throw( std::logic_error( "InvestmentBlock::generate_abstract_constraints:"
           " the components " + std::to_string( parked.first ) + " to " +
           std::to_string( parked.first + parked.pieces.size() - 1 ) +
           " are the pieces of the " + parked.root->classname() + " of " +
           group_name( parked.k ) + ", and the " +
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
 // f is adopted: an FRealObjective deletes it on success, owned on failure
 std::unique_ptr< InvestmentFunction > owned( f );

 // the structure must be defined before the abstract representation
 if( objective_generated() || constraints_generated() )
  throw( std::logic_error( "InvestmentBlock::add_component: "
                           "abstract representation already generated" ) );

 if( v_variables.empty() )   // a component has no active Variable to bind to
  throw( std::logic_error( "InvestmentBlock::add_component: "
                           "no design variables" ) );

 if( ! f )
  throw( std::invalid_argument( "InvestmentBlock::add_component: "
                                "null component" ) );

 // a component with no active Variable yet is bound to ALL the design
 // ColVariable, in their natural order (the common case); a component that
 // already has actives (the deserialize() path, or a caller picking a proper
 // subset) is left untouched
 if( ! f->get_num_active_var() )
  f->set_variables( design_vars() );

 // one bare AbstractBlock per component, holding an FRealObjective over f:
 // BundleSolver sees one component per sub-Block
 auto sub = new AbstractBlock( this );
 f->set_f_Block( sub );
 auto obj = new FRealObjective( sub , owned.release() );
 obj->set_sense( Objective::eMin );          // convex component => eMin only
 sub->set_objective( obj , eNoMod );
 add_nested_Block( sub );
 }

/*--------------------------------------------------------------------------*/

void InvestmentBlock::add_component( InvestmentFunction * f ,
                                     const std::vector< Index > & actives )
{
 // f is adopted as by add_component( f ), which it is handed to
 std::unique_ptr< InvestmentFunction > owned( f );

 if( ! f )
  throw( std::invalid_argument( "InvestmentBlock::add_component: "
                                "null component" ) );
 if( f->get_num_active_var() )
  throw( std::logic_error( "InvestmentBlock::add_component: the component "
                           "already has active Variable" ) );
 if( actives.empty() )
  throw( std::invalid_argument( "InvestmentBlock::add_component: empty "
                                "active subset (use the one-parameter "
                                "overload for the dense wiring)" ) );
 for( Index i = 0 ; i < actives.size() ; ++i ) {
  if( actives[ i ] >= v_variables.size() )
   throw( std::invalid_argument( "InvestmentBlock::add_component: active "
                                 "index " + std::to_string( actives[ i ] ) +
                                 " out of range" ) );
  if( i && ( actives[ i ] <= actives[ i - 1 ] ) )
   throw( std::invalid_argument( "InvestmentBlock::add_component: the "
                                 "active subset must be strictly "
                                 "increasing (BundleSolver union-order "
                                 "rule, and no duplicates)" ) );
  }

 f->set_variables( design_vars( actives ) );
 add_component( owned.release() );
 }

/*--------------------------------------------------------------------------*/

std::vector< ColVariable * > InvestmentBlock::design_vars(
                                     const std::vector< Index > & indices )
{
 const Index n = indices.empty() ? v_variables.size() : indices.size();
 std::vector< ColVariable * > p( n );
 for( Index i = 0 ; i < n ; ++i )
  p[ i ] = & v_variables[ indices.empty() ? i : indices[ i ] ];
 return( p );
 }

/*--------------------------------------------------------------------------*/

std::vector< InvestmentFunction * >
InvestmentBlock::get_investment_functions( void ) const
{
 // the one in the Objective, or else one per component sub-Block, the
 // Objective being then empty
 std::vector< InvestmentFunction * > funcs;
 if( auto IF = dynamic_cast< InvestmentFunction * >( get_function() ) )
  funcs.push_back( IF );
 else
  for( Index k = 0 ; k < get_number_nested_Blocks() ; ++k )
   funcs.push_back( component_function( this , k ) );
 return( funcs );
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

 refuse_generator( group , "the root" );

 // the presence of the group "Component_0" selects the format with
 // components, otherwise the one with the inner Block at the root
 if( ! group.getGroup( "Component_0" ).isNull() ) {

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

  // access by numeric suffix => deterministic order; the loop stops at the
  // first missing Component_<k> group, and k is then the number of those
  // read
  Index k = 0;
  for( ; ; ++k ) {
   auto grp_k = group.getGroup( "Component_" + std::to_string( k ) );
   if( grp_k.isNull() )
    break;

   refuse_generator( grp_k , "Component_" + std::to_string( k ) );

   // an inner Block made of Blocks of the class of the methods of the assets
   // is split, one component per such Block [see split_inner()]
   if( split_inner( grp_k , k ) )
    continue;

   // f_k (and its inner Block) must not leak on a rejected file:
   // Block::new_Block swallows the exception and returns nullptr
   auto f_k = std::make_unique< InvestmentFunction >();
   f_k->deserialize( grp_k );   // no actives yet: the mappings stay GLOBAL,
                                // as the file speaks (range-checked below)
   wire_component_actives( f_k.get() , k );
   add_component( f_k.release() );   // wraps in a bare AbstractBlock
   }

  // a Component_<k> group past a gap in the numbering would be dropped
  // silently: all of them must have been read
  Index num_groups = 0;
  for( const auto & named : group.getGroups() )
   if( ( named.first.rfind( "Component_" , 0 ) == 0 ) &&
       ( named.first.size() > 10 ) &&
       std::isdigit( (unsigned char) named.first[ 10 ] ) )
    ++num_groups;
  if( num_groups != k )
   throw( std::logic_error( "InvestmentBlock::deserialize: of the " +
	  std::to_string( num_groups ) + " Component_<k> groups only " +
	  std::to_string( k ) + " are numbered consecutively from 0, and the "
	  "others would be ignored: number the components consecutively "
	  "from 0." ) );

  warn_unit_weights( this );

  variation_limits( this );  // refuses the components that disagree on one

  Block::deserialize( group );
  return;
  }

 // ---- the inner Block at the root, split into components ----
 // as that of a Component_0 is [see split_inner()]: the components are
 // minimized, as those of Component_<k> groups are, and a maximization asked
 // at the root is refused rather than ignored
 if( split_inner( group , Inf< Index >() ) ) {
  if( f_objective_sense == Objective::eMax )
   throw( std::logic_error( "InvestmentBlock::deserialize: the root asks "
	  "for a maximization, but its inner Block is separated into "
	  "components, each of which is minimized." ) );
  warn_unit_weights( this );
  Block::deserialize( group );
  return;
  }

 // ---- the inner Block at the root, a single component ----
 // the function must not leak on a rejected file, as that of a Component_<k>
 auto investment_function = std::make_unique< InvestmentFunction >();

 investment_function->set_variables( design_vars() );
 investment_function->deserialize( group );
 const auto function = investment_function.release();
 set_function( function );
 function->set_f_Block( this );

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

 // with no AssetVarIndex the asset i of a function invests in its active
 // Variable i, which has to be there; the mapping can be given after the
 // Variable [see add_component()], hence the check is made only here
 for( const auto f : get_investment_functions() )
  if( f->get_asset_variable_indices().empty() &&
      ( f->get_asset_indices().size() > f->get_num_active_var() ) )
   throw( std::logic_error( "InvestmentBlock::generate_abstract_constraints:"
          " a component has " + std::to_string( f->get_asset_indices().size() )
          + " assets and " + std::to_string( f->get_num_active_var() ) +
          " active Variable, and no AssetVarIndex: the asset i invests in "
          "the active Variable i" ) );

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
 for( auto function : get_investment_functions() ) {
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
 const auto limits = variation_limits( this );
 v_variation_constraints.resize( limits.size() );
 auto row = v_variation_constraints.begin();
 for( const auto & [ pair , l ] : limits ) {
  LinearFunction::v_coeff_pair vars = { std::make_pair( l.x , 1.0 ) };
  if( l.b )
   vars.push_back( std::make_pair( l.b , -1.0 ) );
  const double s = l.q - design_shift( l.x ) + design_shift( l.b );
  row->set_lhs( l.lo + s );
  row->set_rhs( l.hi + s );
  ( row++ )->set_function( new LinearFunction( std::move( vars ) ) );
  }

 if( ! v_variation_constraints.empty() )
  add_static_constraint( v_variation_constraints , "variation_bounds" );
 }

/*--------------------------------------------------------------------------*/

double InvestmentBlock::design_shift( const ColVariable * x ) const
{
 if( ( ! x ) || ( ! f_reformulate_bounds ) || v_lower_bound.empty() )
  return( 0.0 );
 const auto lb = v_lower_bound[ x - v_variables.data() ];
 return( lb > - Inf< double >() ? lb : 0.0 );
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

 // wsol != 0 means "read the inner Block :Solution(s) when asked to"; how
 // many, one per component, is resolved at read() time
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
  // a single component: write the InvestmentFunction at the root group,
  // which it would leave with its own type
  if( auto function = objective.get_function() ) {
   static_cast< InvestmentFunction * >( function )->serialize( group );
   group.putAtt( "type" , "InvestmentBlock" );
   }
  }
 else {
  // components: one Component_<k> group per nested sub-Block, each the
  // existing InvestmentFunction serialize (which writes its AssetVarIndex
  // when non-default); the count is implicit in the suffixes
  auto parked = v_parked.begin();
  for( Index k = 0 , j = 0 ; k < get_number_nested_Blocks() ; ++k , ++j ) {
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
   // have local == global and are left untouched.
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

 // Check the "physical representation", on the value of each asset: the
 // one of its ColVariable plus its shift [see design_shift()]
 const auto asset = [ this ]( const ColVariable * x ) {
  return( x->get_value() + design_shift( x ) );
  };

 for( Index i = 0 ; i < v_lower_bound.size() ; ++i )
  if( v_lower_bound[ i ] > -Inf< double >() )
   if( asset( & v_variables[ i ] ) < v_lower_bound[ i ] - tolerance )
    return( false );

 for( Index i = 0 ; i < v_upper_bound.size() ; ++i )
  if( v_upper_bound[ i ] < Inf< double >() )
   if( asset( & v_variables[ i ] ) > v_upper_bound[ i ] + tolerance )
    return( false );

 for( const auto & [ pair , l ] : variation_limits( this ) ) {
  const double d = asset( l.x ) - ( l.b ? asset( l.b ) : 0 ) - l.q;
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

 // a single component: a single "InnerSolution" group
 auto sub_group = group.getGroup( "InnerSolution" );
 if( ! sub_group.isNull() )
  v_inner_Solutions.push_back( Solution::new_Solution( sub_group ) );
 else {
  // components: "InnerSolution_<k>" groups, in numeric order;
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

// the values x of the design Variable of IB shifted by sign times their
// finite lower bounds if the bounds are reformulated [see
// InvestmentBlock::generate_abstract_constraints()]: + 1 from the values of
// the ColVariable to the investment, - 1 back

static std::vector< double > shift_by_lower_bound( const InvestmentBlock * IB ,
                                                   std::vector< double > x ,
                                                   double sign )
{
 const auto & lb = IB->get_variable_lower_bound();
 if( ( ! lb.empty() ) && IB->get_reformulate_bounds() )
  for( Block::Index i = 0 ; i < x.size() ; ++i )
   if( lb[ i ] > -Inf< double >() )
    x[ i ] += sign * lb[ i ];
 return( x );
 }

/*--------------------------------------------------------------------------*/

// the inner Block of the InvestmentFunction f, whose :Solution an
// InvestmentBlockSolution holds; who names the method asking for it

static Block * inner_block( const InvestmentFunction * f , const char * who )
{
 if( f->get_nested_Blocks().empty() )
  throw( std::invalid_argument( std::string( "InvestmentBlockSolution::" ) +
                                who + ": empty inner Block" ) );
 return( f->get_nested_Blocks().front() );
 }

/*--------------------------------------------------------------------------*/

void InvestmentBlockSolution::read( const Block * block )
{
 auto IB = dynamic_cast< const InvestmentBlock * >( block );
 if( ! IB )
  throw( std::invalid_argument(
	"InvestmentBlockSolution::read: block is not a InvestmentBlock" ) );

 v_design = shift_by_lower_bound( IB , IB->get_variable_values() , 1 );

 // collect the inner Block :Solution(s) if requested, one per component
 // [see InvestmentBlock::get_investment_functions()]
 for( auto s : v_inner_Solutions )
  delete s;
 v_inner_Solutions.clear();

 if( ! f_inner_wanted )
  return;

 auto funcs = IB->get_investment_functions();

 if( funcs.empty() )
  throw( std::invalid_argument(
	    "InvestmentBlockSolution::read: empty InvestmentFunction" ) );

 v_inner_Solutions.reserve( funcs.size() );
 for( auto fk : funcs ) {
  auto s = inner_block( fk , "read" )->get_Solution( f_inner_Configuration ,
                                                     false );
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

 IB->set_variable_values< double >( shift_by_lower_bound( IB , v_design ,
                                                          -1 ) );

 if( v_inner_Solutions.empty() )
  return;

 // the InvestmentFunction to write into, one per component [see
 // InvestmentBlock::get_investment_functions()]; the inner :Solution count
 // must match.
 auto funcs = IB->get_investment_functions();

 if( funcs.empty() )
  throw( std::invalid_argument(
	   "InvestmentBlockSolution::write: empty InvestmentFunction" ) );

 if( v_inner_Solutions.size() != funcs.size() )
  throw( std::invalid_argument(
    "InvestmentBlockSolution::write: inconsistent inner Solution count" ) );

 for( Index k = 0 ; k < funcs.size() ; ++k ) {
  const auto inner = inner_block( funcs[ k ] , "write" );
  if( v_inner_Solutions[ k ] )
   v_inner_Solutions[ k ]->write( inner );
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
  // a single component: a single "InnerSolution" group
  if( v_inner_Solutions.front() ) {
   auto sub_group = group.addGroup( "InnerSolution" );
   v_inner_Solutions.front()->serialize( sub_group );
   }
  }
 else if( ! v_inner_Solutions.empty() ) {
  // components: one "InnerSolution_<k>" group per component
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
