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

 auto investment_function = new InvestmentFunction();

 std::vector< ColVariable * > p_variables;
 p_variables.reserve( v_variables.size() );
 for( auto & variable : v_variables )
  p_variables.push_back( & variable );
 investment_function->set_variables( std::move( p_variables ) );

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

 // if wsol != 0 allocate a placeholder Solution object meant to say "do
 // read the Solution of the inner Block when you are asked to"
 sol->f_inner_Solution = wsol ? new Solution() : nullptr;
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

 if( auto function = objective.get_function() )
  static_cast< InvestmentFunction * >( function )->serialize( group );
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

 // deserialize the InnerSolution - - - - - - - - - - - - - - - - - - - - - -
 delete f_inner_Solution;  // just to be on the safe side
 auto sub_group = group.getGroup( "InnerSolution" );
 if( sub_group.isNull() )
  f_inner_Solution = nullptr;
 else
  f_inner_Solution = Solution::new_Solution( sub_group );
 
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

 if( f_inner_Solution ) {  // if the inner Solution need be saved
  delete f_inner_Solution;
  f_inner_Solution = nullptr;
  auto IF = dynamic_cast< InvestmentFunction * >( IB->get_function() );
  if( ! IF )
   throw( std::invalid_argument(
	      "InvestmentBlockSolution::read: empty InvestmentFunction" ) );
  if( IF->get_nested_Blocks().empty() )
    throw( std::invalid_argument(
		     "InvestmentBlockSolution::read: empty inner Block" ) );
  f_inner_Solution = ( ( IF->get_nested_Blocks() ).front()
			)->get_Solution( f_inner_Configuration , false );
  if( ! f_inner_Solution )
   throw( std::invalid_argument(
	  "InvestmentBlockSolution::read: cannot read desired inner Solution"
				) );
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

 if( f_inner_Solution ) {
  auto IF = dynamic_cast< InvestmentFunction * >( IB->get_function() );
  if( ! IF )
   throw( std::invalid_argument(
	      "InvestmentBlockSolution::write: empty InvestmentFunction" ) );
  if( IF->get_nested_Blocks().empty() )
    throw( std::invalid_argument(
		     "InvestmentBlockSolution::write: empty inner Block" ) );
  f_inner_Solution->write( IF->get_nested_Blocks().front() );
  }
 }  // end( InvestmentBlockSolution::write )

/*--------------------------------------------------------------------------*/

void InvestmentBlockSolution::serialize( netCDF::NcGroup & group ) const
{
 Solution::serialize( group );

 auto ndv = group.addDim( "NumDesignVariables" , v_design.size() );

 ::serialize< double >( group , "DesignVariables" , netCDF::NcDouble() ,
			ndv , v_design );

 if( f_inner_Solution ) {
  auto sub_group = group.addGroup( "InnerSolution" );
  f_inner_Solution->serialize( sub_group );
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

 if( sol->f_inner_Solution )
  sol->f_inner_Solution->scale( factor );
 
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

 if( ( f_inner_Solution && ( ! IBS->f_inner_Solution ) ) ||
     ( ( ! f_inner_Solution ) && IBS->f_inner_Solution )  )
  throw( std::invalid_argument(
	    "InvestmentBlockSolution::sum: inconsistent inner Solution" ) );

 auto dit = IBS->v_design.begin();
 for( auto & i : v_design )
  i += *(dit++) * multiplier;

 if( f_inner_Solution )
  f_inner_Solution->sum( IBS->f_inner_Solution , multiplier );

 }  // end( InvestmentBlockSolution::sum )

/*--------------------------------------------------------------------------*/

InvestmentBlockSolution * InvestmentBlockSolution::clone( bool empty ) const
{
 auto sol = new InvestmentBlockSolution();

 if( ! empty ) {
  sol->v_design = v_design;

  if( f_inner_Solution )
   sol->f_inner_Solution = f_inner_Solution->clone();
  }

 return( sol );

 }  // end( InvestmentBlockSolution::clone )

/*--------------------------------------------------------------------------*/
/*--------------------- End File InvestmentBlock.cpp -----------------------*/
/*--------------------------------------------------------------------------*/
