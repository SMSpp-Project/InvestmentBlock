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

#include "AbstractBlock.h"

#include "StochasticBlock.h"

#include "ScenarioGenerator.h"

#include <algorithm>

#include <cctype>

#include <memory>

#include <iostream>

#include <set>

/*--------------------------------------------------------------------------*/
/*------------------------- NAMESPACE AND USING ----------------------------*/
/*--------------------------------------------------------------------------*/

using namespace SMSpp_di_unipi_it;

/*--------------------------------------------------------------------------*/
/*------------------ FILE-LOCAL COMPONENT NAVIGATION -----------------------*/
/*--------------------------------------------------------------------------*/

// reach the InvestmentFunction of the k-th disaggregated component: each
// component sub-Block is a bare AbstractBlock whose FRealObjective wraps one
// InvestmentFunction (as add_component() builds it). The defensive dynamic_cast
// + throw mirrors how the consumer of these blocks navigates them (see
// BundleSolver: one FRealObjective / C05Function per component sub-Block).

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
// path (one per component sub-Block, since the master objective is then empty).
// This is the single load-bearing "legacy vs 1:K" dispatch; read() and write()
// share it so the two paths cannot drift apart.

static std::vector< InvestmentFunction * > component_functions( const Block * IB )
{
 std::vector< InvestmentFunction * > funcs;
 if( auto IF = dynamic_cast< InvestmentFunction * >(
		static_cast< const InvestmentBlock * >( IB )->get_function() ) )
  funcs.push_back( IF );                                    // legacy: 1
 else
  for( Block::Index k = 0 ; k < IB->get_number_nested_Blocks() ; ++k )
   funcs.push_back( component_function( IB , k ) );          // disaggregated: K
 return( funcs );
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

void InvestmentBlock::wire_component_actives( InvestmentFunction * f , Index k )
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

bool InvestmentBlock::expand_stochastic_template(
                       const netCDF::NcGroup & grp ,
                       const netCDF::NcGroup & root , Index k ,
                       Index & num_components , bool & all_unit_weights )
{
 // a group whose "InnerBlock" is a StochasticBlock is a TEMPLATE: it expands
 // into one component per scenario of its ScenarioGenerator (looked up in grp
 // first, then at root), each carrying the inner materialized on that scenario
 // and weighted by (probability) x (Weight). Shared by the Component_k loop and
 // the legacy path, so a direct StochasticBlock inner expands like a Component_0.
 const auto inner_grp = grp.getGroup( "InnerBlock" );
 std::string inner_type;
 if( ! inner_grp.isNull() ) {
  auto att = inner_grp.getAtt( "type" );
  if( ! att.isNull() )
   att.getValues( inner_type );
  }

 if( inner_type != "StochasticBlock" )
  return( false );   // not a template: let the caller handle grp its own way

 auto gen_grp = grp.getGroup( "ScenarioGenerator" );
 if( gen_grp.isNull() )
  gen_grp = root.getGroup( "ScenarioGenerator" );
 if( gen_grp.isNull() )
  throw( std::logic_error( "InvestmentBlock::deserialize: Component_" +
	 std::to_string( k ) + " has a StochasticBlock inner Block but no "
	 "'ScenarioGenerator' group was found, neither in the component "
	 "nor at the root of the InvestmentBlock." ) );

 std::unique_ptr< ScenarioGenerator >
  generator( ScenarioGenerator::new_ScenarioGenerator( gen_grp ) );
 if( ! generator )
  throw( std::logic_error( "InvestmentBlock::deserialize: it was not "
	 "possible to create the ScenarioGenerator of Component_" +
	 std::to_string( k ) + "." ) );

 generator->init_representative_pool();

 // Walk the pool with next_scenario(), as every other ScenarioGenerator
 // consumer does (SDDPBlock, TwoStageStochasticBlock). NOT get_support_size():
 // that is INFScenario for a continuous/multi-stage generator (not enumerable)
 // -- an infinite-support generator must be reduced upstream to a finite pool.
 Index added = 0;
 do {
  // a fresh StochasticBlock per scenario (set_data() rewrites the inner IN
  // PLACE, so a shared one would leave every scenario with the last one's data)
  std::unique_ptr< Block > sb_block( Block::new_Block( inner_grp , this ) );
  auto sb = dynamic_cast< StochasticBlock * >( sb_block.get() );
  if( ! sb )
   throw( std::logic_error( "InvestmentBlock::deserialize: it was not "
	  "possible to create the StochasticBlock of Component_" +
	  std::to_string( k ) + "." ) );

  const auto scenario = generator->get_current_scenario();
  const auto probability = generator->get_current_scenario_probability();
  const std::vector< double > data( scenario.begin() , scenario.end() );
  sb->set_data( data );

  auto inner_raw = sb->get_inner_block();
  if( ! inner_raw )
   throw( std::logic_error( "InvestmentBlock::deserialize: the "
	  "StochasticBlock of Component_" + std::to_string( k ) +
	  " has no inner Block." ) );
  sb->set_inner_block( nullptr , false );   // detach, do not destroy

  // once detached, inner is owned by nobody until f_s->deserialize() adopts it
  // as its last act; hold it so a throw in between (bad Weight/AssetType/...)
  // frees the subtree instead of leaking it. Release only after adoption.
  std::unique_ptr< Block > inner( inner_raw );

  auto f_s = std::make_unique< InvestmentFunction >();
  f_s->deserialize( grp , inner.get() );  // the template's data + THIS inner
  inner.release();                        // adopted by f_s
  wire_component_actives( f_s.get() , k );

  // weight = Weight (period discount) x scenario probability. add_component()
  // adopts f_s on success and deletes it on failure (weight <= 0), so release
  // BEFORE the call or the unique_ptr would double-free on the throw path.
  const auto weight = f_s->get_weight() * probability;
  add_component( f_s.release() , weight );
  if( weight != 1.0 )
   all_unit_weights = false;
  ++num_components;
  ++added;
  }
 while( generator->next_scenario() );

 // an empty pool would leave 0 components (a degenerate InvestmentBlock with a
 // null objective) -- fail loud, as SDDPBlock does on an empty scenario pool
 if( added == 0 )
  throw( std::logic_error( "InvestmentBlock::deserialize: the ScenarioGenerator"
	 " of Component_" + std::to_string( k ) + " produced no scenarios "
	 "(empty representative pool); the expansion requires a non-empty, "
	 "finite scenario set." ) );

 return( true );

 }  // end( InvestmentBlock::expand_stochastic_template )

/*--------------------------------------------------------------------------*/

void InvestmentBlock::add_component( InvestmentFunction * f , double weight )
{
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

 // f is adopted: on success an FRealObjective deletes it, so on failure it
 // must be freed here or it leaks. weight must be > 0 (BundleSolver eMin)
 if( weight <= 0 ) {
  delete f;
  throw( std::invalid_argument( "InvestmentBlock::add_component: "
                                "weight must be > 0" ) );
  }

 // a component with no active Variable yet is bound to ALL the design
 // ColVariable, in their natural order (the common case); a component that
 // already has actives (the deserialize() path, or a caller picking a proper
 // subset) is left untouched
 if( ! f->get_num_active_var() ) {
  std::vector< ColVariable * > p;
  p.reserve( v_variables.size() );
  for( auto & variable : v_variables )
   p.push_back( & variable );
  f->set_variables( std::move( p ) );
  }

 // one bare AbstractBlock per component, holding an FRealObjective over f:
 // BundleSolver sees one component per sub-Block
 auto sub = new AbstractBlock( this );
 f->set_f_Block( sub );
 f->set_weight( weight );                     // component behaves as weight * f
 auto obj = new FRealObjective( sub , f );
 obj->set_sense( Objective::eMin );          // convex component => eMin only
 sub->set_objective( obj , eNoMod );
 add_nested_Block( sub );
 }

/*--------------------------------------------------------------------------*/

InvestmentBlock::~InvestmentBlock()
{
 for( auto block : v_Block )
  delete block;
 v_Block.clear();

 for( auto & constraint : v_constraints )
  constraint.clear();

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

 ::deserialize( group , "LowerBound" , num_assets , v_lower_bound ,
                true , true );

 ::deserialize( group , "UpperBound" , num_assets , v_upper_bound ,
                true , true );

 f_objective_sense = Objective::eMin;
 if( deserialize_dim( group , "ObjectiveSense" , f_objective_sense ) &&
     ( ! f_objective_sense ) )
  f_objective_sense = Objective::eMax;

 // a bound vector, if provided, must have size 0, 1 (broadcast to NumAssets),
 // or exactly NumAssets; enforce this once for both LowerBound and UpperBound
 auto check_bound = [ num_assets ]( std::vector< double > & bound ,
				    const char * name ) {
  if( bound.empty() )
   return;
  if( bound.size() == 1 )
   bound.resize( num_assets , bound.front() );
  else if( bound.size() != num_assets )
   throw( std::logic_error( std::string( "InvestmentBlock::deserialize: the '" )
			    + name + "' netCDF variable, if provided, must have"
			    " size 0, 1, or 'NumAssets'." ) );
  };

 check_bound( v_lower_bound , "LowerBound" );
 check_bound( v_upper_bound , "UpperBound" );

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
  // access by numeric suffix => deterministic order; the count is implicit
  // (the loop stops at the first missing Component_<k> group)
  Index num_components = 0;
  Index num_stochastic = 0;   // components expanded out of a StochasticBlock
  bool all_unit_weights = true;

  Index k = 0;   // survives the loop: it is the count of consecutive
                 // Component_<k> groups read, checked against the total below
  for( ; ; ++k ) {
   auto grp_k = group.getGroup( "Component_" + std::to_string( k ) );
   if( grp_k.isNull() )
    break;

   // a component whose inner is a StochasticBlock is a TEMPLATE: it expands
   // into one component per scenario (shared helper, see below). The template
   // itself is not a component, so on success skip to the next Component_k.
   if( expand_stochastic_template( grp_k , group , k ,
                                   num_components , all_unit_weights ) ) {
    ++num_stochastic;
    continue;
    }

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

   add_component( f_k , f_k->get_weight() );  // wraps in a bare AbstractBlock
   if( f_k->get_weight() != 1.0 )
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

  // a ScenarioGenerator at the root with no StochasticBlock to feed is a
  // misunderstanding of the format, not a harmless extra: fail loud
  if( ( ! num_stochastic ) && ( ! group.getGroup( "ScenarioGenerator" ).isNull() ) )
   throw( std::logic_error( "InvestmentBlock::deserialize: a "
	  "'ScenarioGenerator' group is present, but no component has a "
	  "StochasticBlock inner Block to expand with it." ) );

  // guardrail: K > 1 components all at the default weight 1.0 almost always
  // means the per-component weights were forgotten (a weighted expectation
  // needs w_k = probability x discount). Warn loudly; do not fail.
  if( ( num_components > 1 ) && all_unit_weights )
   std::cerr << "InvestmentBlock::deserialize: WARNING - " << num_components
             << " components all have weight 1.0; if these are "
                "scenarios/periods of a weighted expectation, set a per-component"
                " 'Weight' (probability x discount)." << std::endl;

  Block::deserialize( group );
  return;
  }

 // ---- legacy single-inner path ----
 // a StochasticBlock as the direct inner is a template too: expand it exactly
 // as a Component_0 one, only without the wrapper.
 {
  Index num_components = 0;
  bool all_unit_weights = true;
  if( expand_stochastic_template( group , group , 0 ,
                                  num_components , all_unit_weights ) ) {
   if( ( num_components > 1 ) && all_unit_weights )
    std::cerr << "InvestmentBlock::deserialize: WARNING - " << num_components
              << " components all have weight 1.0; if these are "
                 "scenarios/periods of a weighted expectation, set a "
                 "per-component 'Weight' (probability x discount)." << std::endl;
   Block::deserialize( group );
   return;
   }

  // nothing was expanded: a root 'ScenarioGenerator' has nothing to feed --
  // reject it, symmetrically with the disaggregated branch above.
  if( ! group.getGroup( "ScenarioGenerator" ).isNull() )
   throw( std::logic_error( "InvestmentBlock::deserialize: a "
	  "'ScenarioGenerator' group is present, but the inner Block is not a "
	  "StochasticBlock to expand with it." ) );
 }

 // ---- legacy single-component path (unchanged behaviour) ----
 auto investment_function = new InvestmentFunction();

 investment_function->set_variables( master_var_pointers() );

 investment_function->set_num_sub_blocks_per_stage(
					       f_num_sub_blocks_per_stage );
 investment_function->set_number_sub_blocks( f_num_sub_blocks );
 investment_function->deserialize( group );
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
 if( constraints_generated() )
  return; // constraints have already been generated

 if( v_lower_bound.empty() && v_upper_bound.empty() )
  return; // there is no bound constraint

 f_reformulate_bounds = 0;
 auto config = dynamic_cast< SimpleConfiguration< int > * >( stcc );
 if( ( ! config ) && f_BlockConfig )
  config = dynamic_cast< SimpleConfiguration< int > * >(
			f_BlockConfig->f_static_constraints_Configuration );
 if( config )
  f_reformulate_bounds = config->f_value;

 if( is_disaggregated() ) {
  // multi-component path: reformulating the bounds would shift the design
  // variables on the master, but the per-component InvestmentFunction are not
  // informed of the shift and would compute with the wrong bounds. Refuse
  // instead of silently returning wrong values.
  if( f_reformulate_bounds )
   throw( std::logic_error(
    "InvestmentBlock::generate_abstract_constraints: bound reformulation not "
    "supported with multiple components" ) );
  }
 else if( auto function =
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

 ::serialize( group , "LowerBound" , netCDF::NcDouble() , NumAssets ,
              v_lower_bound );

 ::serialize( group , "UpperBound" , netCDF::NcDouble() , NumAssets ,
              v_upper_bound );

 if( ! is_disaggregated() ) {
  // legacy single-component: write the InvestmentFunction at the root group
  // (byte-identical to before: no Component_<k>)
  if( auto function = objective.get_function() )
   static_cast< InvestmentFunction * >( function )->serialize( group );
  }
 else {
  // disaggregated (1:K): one Component_<k> group per nested sub-Block, each the
  // existing InvestmentFunction serialize (which writes its own Weight and
  // AssetVarIndex when non-default); the count is implicit in the suffixes
  for( Index k = 0 ; k < get_number_nested_Blocks() ; ++k ) {
   // reach the component's InvestmentFunction through the single navigation
   // helper (checked in debug), so serialize does not re-encode the component
   // structure sub-Block -> FRealObjective -> InvestmentFunction
   const auto f_k = component_function( this , k );
   auto grp_k = group.addGroup( "Component_" + std::to_string( k ) );
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

 if( useabstract && ( ! v_constraints.empty() ) ) {
  // Use the set of Constraint to decide whether the current solution is
  // feasible.
  for( auto & constraint : v_constraints ) {
   if( constraint.is_relaxed() )
    continue;
   constraint.compute();
   if( constraint.abs_viol() > tolerance )
    return( false );
   }

  return( true );
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
