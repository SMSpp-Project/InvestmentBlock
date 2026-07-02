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

#include <algorithm>

#include <iostream>

/*--------------------------------------------------------------------------*/
/*------------------------- NAMESPACE AND USING ----------------------------*/
/*--------------------------------------------------------------------------*/

using namespace SMSpp_di_unipi_it;

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

 // one bare AbstractBlock per component, holding an FRealObjective over f:
 // BundleSolver sees one component per sub-Block
 auto sub = new AbstractBlock( this );
 f->set_f_Block( sub );
 f->set_weight( weight );                     // component behaves as weight * f
 auto obj = new FRealObjective( sub , f );
 obj->set_sense( Objective::eMin );          // convex component => eMin only
 sub->set_objective( obj , eNoMod );
 add_nested_Block( sub );
 v_weights.push_back( weight );
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

 if( ! v_lower_bound.empty() ) {
  if( v_lower_bound.size() == 1 )
   v_lower_bound.resize( num_assets , v_lower_bound.front() );
  else
   if( v_lower_bound.size() != num_assets )
    throw( std::logic_error( "InvestmentBlock::deserialize: the 'LowerBound' "
			     "netCDF variable, if provided, must have size 0,"
			     " 1, or 'NumAssets'." ) );
  }

 if( ! v_upper_bound.empty() ) {
  if( v_upper_bound.size() == 1 )
   v_upper_bound.resize( num_assets , v_upper_bound.front() );
  else
   if( v_upper_bound.size() != num_assets )
    throw( std::logic_error( "InvestmentBlock::deserialize: the 'UpperBound' "
			     "netCDF variable, if provided, must have size 0,"
			     " 1, or 'NumAssets'." ) );
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
  // NumComponents is an optional guardrail; if present it must match the number
  // of Component_<k> groups actually found
  Index num_components = 0;
  const bool have_count =
   deserialize_dim( group , "NumComponents" , num_components );

  for( Index k = 0 ; ; ++k ) {   // access by numeric suffix => deterministic order
   auto grp_k = group.getGroup( "Component_" + std::to_string( k ) );
   if( grp_k.isNull() ) {
    if( have_count && ( k != num_components ) )
     throw( std::logic_error(
      "InvestmentBlock::deserialize: NumComponents = " +
      std::to_string( num_components ) + " but found " + std::to_string( k ) +
      " Component_<k> groups." ) );
    break;
    }
   if( have_count && ( k >= num_components ) )
    throw( std::logic_error(
     "InvestmentBlock::deserialize: more Component_<k> groups than the declared "
     "NumComponents = " + std::to_string( num_components ) + "." ) );

   auto f_k = new InvestmentFunction();
   f_k->set_variables( master_var_pointers() );
   f_k->deserialize( grp_k );    // reads Weight, AssetVarIndex, Constraints, ...
   add_component( f_k , f_k->get_weight() );  // wraps in a bare AbstractBlock
   }

  // #4 guardrail: K > 1 components all at the default weight 1.0 almost always
  // means the per-component weights were forgotten (a weighted expectation
  // needs w_k = probability x discount). Warn loudly; do not fail.
  if( ( v_weights.size() > 1 ) &&
      std::all_of( v_weights.begin() , v_weights.end() ,
                   []( double w ){ return( w == 1.0 ); } ) )
   std::cerr << "InvestmentBlock::deserialize: WARNING - " << v_weights.size()
             << " components all have weight 1.0; if these are "
                "scenarios/periods of a weighted expectation, set a per-component"
                " 'Weight' (probability x discount)." << std::endl;

  Block::deserialize( group );
  return;
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

 if( v_weights.empty() ) {
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

 if( ! v_weights.empty() ) {
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

 if( v_weights.empty() ) {
  // legacy single-component: write the InvestmentFunction at the root group
  // (byte-identical to before: no Component_<k>, no NumComponents)
  if( auto function = objective.get_function() )
   static_cast< InvestmentFunction * >( function )->serialize( group );
  }
 else {
  // disaggregated (1:K): one Component_<k> group per nested sub-Block, each the
  // existing InvestmentFunction serialize (which writes its own Weight and
  // AssetVarIndex when non-default); NumComponents is the round-trip guardrail
  group.addDim( "NumComponents" , get_number_nested_Blocks() );
  for( Index k = 0 ; k < get_number_nested_Blocks() ; ++k ) {
   auto sub = static_cast< const AbstractBlock * >( get_nested_Block( k ) );
   auto obj = static_cast< const FRealObjective * >( sub->get_objective() );
   auto f_k = static_cast< const InvestmentFunction * >( obj->get_function() );
   auto grp_k = group.addGroup( "Component_" + std::to_string( k ) );
   f_k->serialize( grp_k );
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
/*------------------------- COMPONENT NAVIGATION ---------------------------*/
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
  // "NumInnerSolutions", if present, is a round-trip guardrail (mirrors the
  // "NumComponents" check in InvestmentBlock::deserialize)
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

 std::vector< InvestmentFunction * > funcs;
 if( auto IF = dynamic_cast< InvestmentFunction * >( IB->get_function() ) )
  funcs.push_back( IF );                                    // legacy: 1
 else
  for( Index k = 0 ; k < IB->get_number_nested_Blocks() ; ++k )
   funcs.push_back( component_function( IB , k ) );          // disaggregated: K

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
 std::vector< InvestmentFunction * > funcs;
 if( auto IF = dynamic_cast< InvestmentFunction * >( IB->get_function() ) )
  funcs.push_back( IF );                                    // legacy: 1
 else
  for( Index k = 0 ; k < IB->get_number_nested_Blocks() ; ++k )
   funcs.push_back( component_function( IB , k ) );          // disaggregated: K

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
