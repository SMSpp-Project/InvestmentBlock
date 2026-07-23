/*--------------------------------------------------------------------------*/
/*----------------------- File InvestmentBlock.h ---------------------------*/
/*--------------------------------------------------------------------------*/
/** @file
 * Header file for the InvestmentBlock class, which derives from Block and has
 * the following characteristics. It has a vector of ColVariable, each of
 * which represents the investment in a particular asset. The number of
 * ColVariable is thus the number of assets that are subject to
 * investment. The Objective of the InvestmentBlock is an FRealObjective whose
 * Function is a InvestmentFunction. The active Variable of this
 * InvestmentFunction are the ones defined in this InvestmentBlock.
 *
 * The InvestmentBlock can have explicit box constraints and "implicit" linear
 * constraints on its set of ColVariable. The box constraints, if present, are
 * in the form of a vector of BoxConstraint, whose size is the number of
 * assets subject to investment and its i-th element is the box constraint
 * associated with the i-th (asset and) ColVariable of this InvestmentBlock.
 *
 * We refer to the linear constraints as "implicit" because they are not
 * represented by Constraint. These linear constraints are handled by the
 * InvestmentFunction (which is part of the Objective of the
 * InvestmentBlock). They help define the domain of the InvestmentFunction. An
 * investment that does not satisfy all of these linear constraints has an
 * InvestmentFunction value which is +infinity (if the sense of the Objective
 * of the InvestmentBlock is minimization) or -infinity (if the sense of the
 * Objective of the InvestmentBlock is maximization).
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
/*----------------------------- DEFINITIONS --------------------------------*/
/*--------------------------------------------------------------------------*/

#ifndef __InvestmentBlock
#define __InvestmentBlock
                      /* self-identification: #endif at the end of the file */

/*--------------------------------------------------------------------------*/
/*------------------------------ INCLUDES ----------------------------------*/
/*--------------------------------------------------------------------------*/

#include "Block.h"
#include "ColVariable.h"
#include "FRealObjective.h"
#include "InvestmentFunction.h"
#include "OneVarConstraint.h"
#include "Solution.h"

/*--------------------------------------------------------------------------*/
/*----------------------------- NAMESPACE ----------------------------------*/
/*--------------------------------------------------------------------------*/

/// namespace for the Structured Modeling System++ (SMS++)
namespace SMSpp_di_unipi_it
{
/*--------------------------------------------------------------------------*/
/*------------------------------- CLASSES ----------------------------------*/
/*--------------------------------------------------------------------------*/
/** @defgroup InvestmentBlock_CLASSES Classes in InvestmentBlock.h
 *  @{ */

/*--------------------------------------------------------------------------*/
/*------------------------ CLASS InvestmentBlock ---------------------------*/
/*--------------------------------------------------------------------------*/
/*--------------------------- GENERAL NOTES --------------------------------*/
/*--------------------------------------------------------------------------*/
/// a Block whose FRealObjective has a InvestmentFunction
/** An InvestmentBlock is a Block that has the following characteristics. It
 * has a vector of ColVariable, each of which represents the investment in a
 * particular asset. The number of ColVariable is thus the number of assets
 * that are subject to investment. The Objective of the InvestmentBlock is an
 * FRealObjective whose Function is a InvestmentFunction. The active Variable
 * of this InvestmentFunction are the ones defined in this InvestmentBlock.
 *
 * The InvestmentBlock can have explicit box constraints and "implicit" linear
 * constraints on its set of ColVariable. The box constraints, if present, are
 * in the form of a vector of BoxConstraint, whose size is the number of
 * assets subject to investment and its i-th element is the box constraint
 * associated with the i-th (asset and) ColVariable of this InvestmentBlock.
 *
 * We refer to the linear constraints as "implicit" because they are not
 * represented by Constraint. These linear constraints are handled by the
 * InvestmentFunction (which is part of the Objective of the
 * InvestmentBlock). They help define the domain of the InvestmentFunction. An
 * investment that does not satisfy all of these linear constraints has an
 * InvestmentFunction value which is +infinity (if the sense of the Objective
 * of the InvestmentBlock is minimization) or -infinity (if the sense of the
 * Objective of the InvestmentBlock is maximization). */

class InvestmentBlock : public Block
{
/*--------------------------------------------------------------------------*/
/*----------------------- PUBLIC PART OF THE CLASS -------------------------*/
/*--------------------------------------------------------------------------*/

public:

/*--------------------------------------------------------------------------*/
/*-------------- CONSTRUCTING AND DESTRUCTING InvestmentBlock --------------*/
/*--------------------------------------------------------------------------*/
/** @name Constructing and destructing InvestmentBlock
 *  @{ */

 /// constructor
 /** Constructs a InvestmentBlock whose father Block is \p father and that has
  * \p num_variables ColVariable.
  *
  * @param father A pointer to the father of this InvestmentBlock.
  *
  * @param num_variables The number of Variable of this InvestmentBlock. */

 InvestmentBlock( Block * father = nullptr , Index num_variables = 0 ) :
  Block( father ) {
  v_variables.resize( num_variables );
  }

/*--------------------------------------------------------------------------*/
 /// adds one weighted component to the disaggregated (1:K) structure
 /** Wraps \p f in a bare AbstractBlock sub-Block holding an FRealObjective
  * over it (one BundleSolver component per sub-Block). Call it once per
  * component to assemble the disaggregated (1:K) structure, be it
  * programmatically or from deserialize() (one call per Component_<k>
  * group).
  *
  * If \p f has no active Variable yet, it is bound to ALL the design
  * ColVariable of this InvestmentBlock, in their natural order (the common
  * case: every component sees the whole x). A caller that wants a proper
  * subset (or a different order) of active Variable must set them on \p f
  * BEFORE calling this method; they are then left untouched.
  *
  * \p weight is validated ( > 0 ) and applied to the component (value and
  * diagonal linearization) via InvestmentFunction::set_weight(). This must
  * be called before the abstract representation is generated.
  *
  * @param f      the InvestmentFunction of the component; ownership is taken
  *               (handed to an FRealObjective, or deleted on failure).
  *
  * @param weight the component weight w ( > 0 ). */

 void add_component( InvestmentFunction * f , double weight );

/*--------------------------------------------------------------------------*/
 /// adds a component bound to a SUBSET of the design Variable
 /** As add_component( f , weight ), but wires the active Variable of \p f to
  * the given \p actives subset (by index, in the given order) of the design
  * ColVariable of this InvestmentBlock. This is the per-period binding of
  * the multi-period (1:K) scheme: the component of period t declares only
  * its own design block plus the baseline variables of period t-1, and the
  * bundle master goes from dense to block-banded.
  *
  * The component's AssetVarIndex / AssetBaselineVarIndex mappings must then
  * use LOCAL positions, i.e., indices into \p actives. NOTE, BundleSolver rule
  * for sparse active sets: each component must present its actives in
  * increasing union order — with the per-period chain layout, passing
  * \p actives sorted by increasing design index is sufficient.
  *
  * @param f       the InvestmentFunction of the component (ownership taken);
  *                it must not have active Variable yet.
  *
  * @param weight  the component weight w ( > 0 ).
  *
  * @param actives the indices of the design Variable this component sees. */

 void add_component( InvestmentFunction * f , double weight ,
                     const std::vector< Index > & actives ) {
  if( ! f )
   throw( std::invalid_argument( "InvestmentBlock::add_component: "
                                 "null component" ) );
  if( f->get_num_active_var() ) {
   delete f;
   throw( std::logic_error( "InvestmentBlock::add_component: the component "
                            "already has active Variable" ) );
   }
  if( actives.empty() ) {
   delete f;
   throw( std::invalid_argument( "InvestmentBlock::add_component: empty "
                                 "active subset (use the two-parameter "
                                 "overload for the dense wiring)" ) );
   }
  std::vector< ColVariable * > p;
  p.reserve( actives.size() );
  for( Index i = 0 ; i < actives.size() ; ++i ) {
   if( actives[ i ] >= v_variables.size() ) {
    delete f;
    throw( std::invalid_argument( "InvestmentBlock::add_component: active "
                                  "index " + std::to_string( actives[ i ] ) +
                                  " out of range" ) );
    }
   if( i && ( actives[ i ] <= actives[ i - 1 ] ) ) {
    delete f;
    throw( std::invalid_argument( "InvestmentBlock::add_component: the "
                                  "active subset must be strictly "
                                  "increasing (BundleSolver union-order "
                                  "rule, and no duplicates)" ) );
    }
   p.push_back( & v_variables[ actives[ i ] ] );
   }
  try {
   f->set_variables( std::move( p ) );
   }
  catch( ... ) {
   delete f;
   throw;
   }
  add_component( f , weight );
  }

/*--------------------------------------------------------------------------*/
 /// destructor

 virtual ~InvestmentBlock();

/*--------------------------------------------------------------------------*/
 /// deserialize a InvestmentBlock out of netCDF::NcGroup
 /** The method takes a netCDF::NcGroup supposedly containing all the
  * information required to deserialize the InvestmentBlock. Besides the
  * 'type' attribute common to all :Block, it should contain:
  *
  * - The dimension "NumAssets" containing the number of assets that are
  *   subject to investment. This dimension is optional. If it is not
  *   provided, then it is assumed that NumAssets = 0.
  *
  *   For each asset that is subject to investment, this InvestmentBlock
  *   defines a ColVariable which determines the investment to be made in that
  *   asset. The i-th ColVariable of this InvestmentBlock is associated with
  *   the i-th asset.
  *
  * - The variable "LowerBound", of type netCDF::NcDouble(), which is either a
  *   scalar or indexed over "NumAssets". If it is a scalar, then we assume
  *   that LowerBound[i] = LowerBound[0] for all i in {0, ..., NumAssets -
  *   1}. The i-th element of this vector provides the lower bound on the i-th
  *   ColVariable of this InvestmentBlock. This variable is optional. If it is
  *   not provided, then we assume that LowerBound[i] = 0 for all i in {0,
  *   ..., NumAssets - 1}.
  *
  * - The variable "UpperBound", of type netCDF::NcDouble(), which is either a
  *   scalar or indexed over "NumAssets". If it is a scalar, then we assume
  *   that UpperBound[i] = UpperBound[0] for all i in {0, ..., NumAssets -
  *   1}. The i-th element of this vector provides the upper bound on the i-th
  *   ColVariable of this InvestmentBlock. This variable is optional. If it is
  *   not provided, then we assume that UpperBound[i] = +inf for all i in {0,
  *   ..., NumAssets - 1}.
  *
  * - The dimension "ObjectiveSense", which indicates the sense of the
  *   Objective of this InvestmentBlock. If it is zero, then the Objective is
  *   a "maximization" one. Otherwise, it is a "minimization" one. This
  *   variable is optional. If it is not provided, then we assume that the
  *   Objective is a "minimization" one. Recall that the Objective of this
  *   InvestmentBlock is an FRealObjective whose Function is an
  *   InvestmentFunction.
  *
  * - A description of the InvestmentFunction.
  *
  * The linear constraints are assumed to have the following form:
  *
  *     l_i <= a_i ' x <= u_i, for i in {0, ..., NumConstraints - 1},
  *
  * where x denotes the variables of this InvestmentBlock, a_i is the vector
  * of coefficients of the i-th constraint, a_i'x is the inner product between
  * a_i and x, and l_i and u_i are the lower and upper bounds determining the
  * i-th linear constraint. We denote by A the matrix of coefficients, whose
  * i-th row is a_i. The following dimension and variables describe the
  * constraints:
  *
  * - The dimension "NumConstraints", containing the number of linear
  *   constraints. This variable is optional. If it is not provided, then it
  *   is assumed that NumConstraints = 0, i.e., there is no linear constraint
  *   (except, of course, the box constraints possibly defined by the
  *   variables LowerBound and UpperBound).
  *
  * - The variable "Constraints_A", of type netCDF::NcDouble() and indexed
  *   over the dimensions "NumConstraints" and "NumAssets" (in this order),
  *   containing the coefficients of the linear constraints. This variable is
  *   mandatory if NumConstraints > 0. If NumConstraints = 0, this variable is
  *   ignored.
  *
  * - The variable "Constraints_LowerBound", of type netCDF::NcDouble() and
  *   indexed over the dimension "NumConstraints", containing the lower bound
  *   of the linear constraints. This variable is optional. If it is not
  *   provided, then it is assumed that Constraints_LowerBound[i] = -inf for
  *   each i in {0, ..., NumConstraints - 1}.
  *
  * - The variable "Constraints_UpperBound", of type netCDF::NcDouble() and
  *   indexed over the dimension "NumConstraints", containing the upper bound
  *   of the linear constraints. This variable is optional. If it is not
  *   provided, then it is assumed that Constraints_UpperBound[i] = +inf for
  *   each i in {0, ..., NumConstraints - 1}.
  *
  * @param group A netCDF::NcGroup holding the required data. */

 void deserialize( const netCDF::NcGroup & group ) override;

/**@} ----------------------------------------------------------------------*/
/*-------------------------- OTHER INITIALIZATIONS -------------------------*/
/*--------------------------------------------------------------------------*/
/** @name Other initializations
 *  @{ */

 /// sets the InvestmentFunction of the FRealObjective of this InvestmentBlock
 /** This function sets the given \p function as the Function of the
  * FRealObjective of this InvestmentBlocks.
  *
  * @param function A pointer to the InvestmentFunction which will be the
  *        function of the FRealObjective of this InvestmentBlock.
  *
  * @param issueMod This parameter indicates if and how the FRealObjectiveMod
  *        is issued, as described in Observer::make_par().
  *
  * @param deleteold This parameter indicates whether the previous Function of
  *        the FRealObjective of this InvestmentBlock must be deleted. */

 void set_function( InvestmentFunction * function ,
		    c_ModParam issueMod = eModBlck ,
                    bool deleteold = true ) {
  function->reformulated_bounds( f_reformulate_bounds );
  objective.set_function( function , issueMod , deleteold );
  }

/*--------------------------------------------------------------------------*/

 void generate_abstract_variables( Configuration *stvv = nullptr ) override;

/*--------------------------------------------------------------------------*/
 /// generate the static constraint of the InvestmentBlock
 /** This function generates the abstract constraints of the InvestmentBlock,
  * which consists in lower and upper bounds on the values of the variables.
  *
  * If no lower bound has been provided, then -inf is the lower bound for each
  * variable. If no upper bound has been provided, then +inf is the upper
  * bound for each variable.
  *
  * If lower and upper bounds have not been provided, then the variables are
  * free and no constraint is generated. Otherwise, for each variable x[ i ],
  * the following constraints are generated
  *
  *     lower_bound[ i ] <= x[ i ] <= upper_bound[ i ].
  *
  * The given Configuration \p stcc or the Configuration for the static
  * constraints present in the BlockConfig of this InvestmentBlock can be used
  * to indicate that the bound constraints must be reformulated as
  * follows. For each i, if lower_bound[ i ] is finite, the above constraint
  * can be replaced by
  *
  *     0 <= x[ i ] <= upper_bound[ i ] - lower_bound[ i ].
  *
  * If \p stcc is not nullptr and it is a SimpleConfiguration< int >, or if
  * f_BlockConfig->f_static_constraints_Configuration is not nullptr and it is
  * a SimpleConfiguration< int >, then the f_value (an int) indicates whether
  * the bounds must be reformulated. If the f_value is nonzero, then the
  * bounds are reformulated as above.
  *
  * @param stcc A pointer to a Configuration determining whether the bounds
  *        must be reformulated. */

 void generate_abstract_constraints( Configuration *stcc = nullptr ) override;

/*--------------------------------------------------------------------------*/

 void generate_objective( Configuration *objc = nullptr ) override;

/*--------------------------------------------------------------------------*/

 void set_num_sub_blocks_per_stage( Index n ) {
  f_num_sub_blocks_per_stage = n;
  }

/*--------------------------------------------------------------------------*/
 /// set the number of (replica) sub-Blocks of the InvestmentFunction
 /** This value is propagated to the underlying InvestmentFunction at
  * deserialize() time. It is the number of identical inner Block replicas
  * to construct when the inner Block is an SDDPBlock and the multi-replica
  * parallel execution path is desired. See
  * InvestmentFunction::set_number_sub_blocks() for the precise meaning. */

 void set_number_sub_blocks( Index n ) {
  f_num_sub_blocks = n;
  }

/*--------------------------------------------------------------------------*/
/*----------------------- Methods for handling Solution --------------------*/
/*--------------------------------------------------------------------------*/
 /// returns a InvestmentBlockSolution with the current solution
 /** Returns a InvestmentBlockSolution representing the current solution
  * status of this InvestmentBlock. What kind of solution is saved depends
  * on the \p solc Configuration and/or on its default value to be found in
  * the f_BlockConfig (if any). That is, we denote by curr_cfg the
  * Configuration * obtained as follows:
  *
  * - if solc != nullptr, then curr_cfg == solc
  *
  * - if solc == nullptr, f_BlockConfig != nullptr,
  *   f_BlockConfig->f_solution_Configuration != nullptr, then
  *   curr_cfg == f_BlockConfig->f_solution_Configuration
  *
  * Now, curr_cfg (if not nullptr) can be of two different types:
  *
  * - a SimpleConfiguration< int >
  *
  * - a SimpleConfiguration< std::pair< int , Configuration * > >
  *
  * Let ws = curr_cfg->f_value in the first case and
  * ws = curr_cfg->f_value.first in the second (1 if curr_cfg == nullptr),
  * and innr_cfg = curr_cfg->f_value.second in the second case (nullptr
  * in the first case or if curr_cfg == nullptr). Then, in all cases the
  * value of the design variables is saved. If ws is nonzero, then the
  * Solution of the inner Block in the InvestmentFunction is saved as well,
  * passing innr_cfg (which may be nullptr) when read()-ing it.
  *
  * Note that InvestmentBlock may not contain some or all of the required
  * solution, if the corresponding Variable/InvestmentFunction have not
  * been constructed yet: this throws an exception, unless emptys = true, in
  * which case the InvestmentBlockSolution object is only prepped for
  * getting a solution, but it is not really getting one now.
  *
  * Note that, although the method clearly returns a InvestmentBlockSolution,
  * formally the return type is Solution *. This is because it is not
  * possible to forward declare InvestmentBlockSolution as a derived class
  * from Solution, nor to define InvestmentBlockSolution before
  * InvestmentBlock because the former uses some type information declared
  * in the latter. */ 

 Solution * get_Solution( Configuration *solc = nullptr ,
			  bool emptys = true ) override;

/** @} ---------------------------------------------------------------------*/
/*------------- METHODS FOR Saving THE DATA OF THE InvestmentBlock ---------*/
/*--------------------------------------------------------------------------*/
/** @name Saving the data of the InvestmentBlock
 *  @{ */

 /// serialize a InvestmentBlock into a netCDF::NcGroup
 /** Serialize a InvestmentBlock into a netCDF::NcGroup, with the
  * format described in deserialize().
  *
  * @param group The netCDF group into which this InvestmentBlock will be
  *        serialized. */

 void serialize( netCDF::NcGroup & group ) const override;

/** @} ---------------------------------------------------------------------*/
/*----------- METHODS FOR READING THE DATA OF THE InvestmentBlock ----------*/
/*--------------------------------------------------------------------------*/
/** @name Reading the data of the InvestmentBlock
    @{ */

 /// Return the number of Variable of this InvestmentBlock
 /** This function returns the number of Variable of this InvestmentBlock.
  *
  * @return The number of Variable of this InvestmentBlock. */

 Index get_number_variables( void ) const {
  return( v_variables.size() );
  }

/*--------------------------------------------------------------------------*/
 /// returns the vector of ColVariable of this InvestmentBlock
 /** This function returns a const reference to the vector of ColVariable of
  * this InvestmentBlock.
  *
  * @return a const reference to the vector of ColVariable of this
  *         InvestmentBlock. */

 const std::vector< ColVariable > & get_variables( void ) const {
  return( v_variables );
  }

/*--------------------------------------------------------------------------*/
 /// returns a vector with the values of the ColVariable
 /** This function return a std::vector< double >  containing the values of
  * the of ColVariable of this InvestmentBlock.
  *
  * @return A vector containing the values of the of ColVariable
  *         of this InvestmentBlock. */

 std::vector< double > get_variable_values( void ) const {
  std::vector< double > variable_values;
  variable_values.reserve( v_variables.size() );
  for( const auto & variable : v_variables )
   variable_values.push_back( variable.get_value() );
  return( variable_values );
  }

/*--------------------------------------------------------------------------*/

 Function * get_function( void ) const {
  return( objective.get_function() );
  }

/*--------------------------------------------------------------------------*/
 /// returns the lower bound on each Variable

 const std::vector< double > & get_variable_lower_bound() const {
  return v_lower_bound;
  }

/*--------------------------------------------------------------------------*/
 /// returns the upper bound on each Variable

 const std::vector< double > & get_variable_upper_bound() const {
  return v_upper_bound;
  }

/*--------------------------------------------------------------------------*/
 /// returns the box constraints on the Variable

 const std::vector< BoxConstraint > & get_constraints() const {
  return v_constraints;
  }

/*--------------------------------------------------------------------------*/
 /// returns true if the Variable are shifted by their lower bound

 bool get_reformulate_bounds() const { return( f_reformulate_bounds ); }

/** @} ---------------------------------------------------------------------*/
/*----------- METHODS DESCRIBING THE BEHAVIOR OF A InvestmentBlock ---------*/
/*--------------------------------------------------------------------------*/
/** @name Methods describing the behavior of a InvestmentBlock
 *  @{ */

 /// sets the values of the Variable of this InvestmentBlock
 /** This function sets the values of the Variable defined in this
  * InvestmentBlock according to the given \p values. The size of the \p
  * values vector parameter must be at least the number of Variable defined in
  * this InvestmentBlock, so that the value of the i-th Variable will be
  * values[ i ], for each i in {0, ..., get_number_variables() - 1}.
  *
  * @param values The vector containing the values of the Variable. */

 template< class T >
 void set_variable_values( const std::vector< T > & values ) {
  assert( ( values.size() >= 0 ) &&
          ( static_cast<decltype( v_variables.size() )>( values.size() ) ==
            v_variables.size() ) );
  for( Index i = 0 ; i < v_variables.size() ; ++i )
   v_variables[ i ].set_value( values[ i ] );
  }

/*--------------------------------------------------------------------------*/
 /// sets the values of the Variable of this InvestmentBlock
 /** This function sets the values of the Variable defined in this
  * InvestmentBlock according to the given \p values. The size of the \p
  * values array parameter must be at least the number of Variable defined in
  * this InvestmentBlock, so that the value of the i-th Variable will be
  * values( i ), for each i in {0, ..., get_number_variables() - 1}.
  *
  * @param values The Eigen::ArrayXd containing the values of the Variable. */

 void set_variable_values( const Eigen::ArrayXd & values ) {
  assert( ( values.size() >= 0 ) &&
          ( static_cast<decltype( v_variables.size() )>( values.size() ) ==
            v_variables.size() ) );
  for( Index i = 0 ; i < v_variables.size() ; ++i )
   v_variables[ i ].set_value( values( i ) );
  }

/*--------------------------------------------------------------------------*/
 /// sets the lower and upper bounds of the design Variable
 /** This function sets the lower and upper bounds of the design Variable
  * defined in this InvestmentBlock according to the given \p lb and \p ub.
  * The size of both the \p lb and \p ub array parameters must be equal to the
  * number of Variable defined in this InvestmentBlock, so that the i-th
  * Variable will be bounded in [ lb[ i ] , ub[ i ] ], for each i in
  * {0, ..., get_number_variables() - 1}.
  *
  * This is meant for the programmatic construction path: in the netCDF path
  * the bounds are filled by deserialize(). It must be called before the
  * abstract constraints are generated.
  *
  * @param lb the vector with the lower bound of each design Variable.
  *
  * @param ub the vector with the upper bound of each design Variable. */

 void set_variable_bounds( std::vector< double > lb ,
                           std::vector< double > ub ) {
  if( constraints_generated() )
   throw( std::logic_error( "InvestmentBlock::set_variable_bounds: "
                            "constraints already generated" ) );
  if( ( lb.size() != v_variables.size() ) ||
      ( ub.size() != v_variables.size() ) )
   throw( std::invalid_argument( "InvestmentBlock::set_variable_bounds: "
                                 "lb/ub size != number of variables" ) );
  v_lower_bound = std::move( lb );
  v_upper_bound = std::move( ub );
  }

/*--------------------------------------------------------------------------*/
 /// fixes (or un-fixes) the i-th design Variable to its current value
 /** Semantic accessor for the one mutation the outside world may need on the
  * design ColVariable: fixing one to its current value (e.g., to freeze part
  * of the design). Registered Solver are notified as usual via the Variable
  * Modification machinery; the ColVariable themselves are not exposed.
  *
  * @param i        the index of the design Variable, in
  *                 [ 0 , get_number_variables() ).
  *
  * @param fix      true to fix the Variable, false to un-fix it.
  *
  * @param issueMod if and how the Modification is issued, as described in
  *                 Observer::make_par(). */

 void fix_design_variable( Index i , bool fix = true ,
                           c_ModParam issueMod = eModBlck ) {
  if( i >= v_variables.size() )
   throw( std::invalid_argument( "InvestmentBlock::fix_design_variable: "
                                 "invalid variable index" ) );
  v_variables[ i ].is_fixed( fix , issueMod );
  }

/*--------------------------------------------------------------------------*/
 /// sets the values of the Variable of this InvestmentBlock
 /** This function sets the values of the Variable defined in this
  * InvestmentBlock according to the values given by the iterator \p it. The
  * number of successors (before the past-the-end iterator) of the given
  * iterator must be at least get_number_variables() - 1, so that the value of
  * the i-th Variable will be given by std::next( it , i ), for each i in {0,
  * ..., get_number_variables() - 1}.
  *
  * @param values The vector containing the values of the Variable. */

 template< class Iterator >
 void set_variable_values( Iterator it ) {
  for( Index i = 0 ; i < v_variables.size() ; ++i , std::advance( it , 1 ) )
   v_variables[ i ].set_value( * it );
  }

/** @} ---------------------------------------------------------------------*/
/*---------------- Methods for checking the InvestmentBlock ----------------*/
/*--------------------------------------------------------------------------*/
/** @name Methods for checking solution information in the InvestmentBlock
 *  @{ */

 /// returns true if the current solution is (approximately) feasible
 /** This function returns true if and only if the solution encoded in the
  * current value of the ColVariable of this InvestmentBlock is approximately
  * feasible considering a given tolerance. The tolerance can be provided by
  * either \p fsbc or by #f_BlockConfig->f_is_feasible_Configuration and it is
  * determined as follows:
  *
  *   - If \p fsbc is not a nullptr and it is a pointer to a
  *     SimpleConfiguration< double >, then the tolerance is the value present
  *     in that SimpleConfiguration.
  *
  *   - Otherwise, if both #f_BlockConfig and
  *     #f_BlockConfig->f_is_feasible_Configuration are not nullptr and the
  *     latter is a pointer to a SimpleConfiguration< double >, then the
  *     tolerance is the value present in that SimpleConfiguration.
  *
  *   - Otherwise, the tolerance is considered to be 1e-8 by default.
  *
  * The only constraints in an InvestmentBlock are lower and upper bounds on
  * the variables of the InvestmentBlock. In the abstract representation,
  * these constraints are represented by a set of BoxConstraint (one
  * BoxConstraint for each ColVariable).
  *
  * The feasibility of the current solution can be determined in two ways: by
  * checking whether each BoxConstraint is feasible or directly checking
  * whether the values of the ColVariable satisfy the bound constraints.
  *
  * If \p useabstract is \c true and the abstract constraints of this
  * InvestmentBlock have been generated, the feasibility is determined by
  * checking the set of BoxConstraint. In this case, the solution is
  * considered feasible if and only if the absolute violation of every
  * non-relaxed BoxConstraint is not greater than the given tolerance. See
  * BoxConstraint::abs_viol() for details about the absolute violation of a
  * BoxConstraint.
  *
  * Otherwise, the solutions is considered to be feasible if and only if, for
  * each ColVariable i,
  *
  *     l_i - tolerance <= x_i <= u_i + tolerance
  *
  * where x_i is the value of the i-th ColVariable and l_i and u_i are the
  * lower and upper bounds on that variable.
  *
  * Notice that, if none of the BoxConstraint is relaxed, these two checks are
  * identical. However, if some bounds are violated and the corresponding
  * BoxConstraint (if generated) are relaxed, then this function will return
  * \c true if \p useabstract is \c true, and \c false if \p useabstract is
  * \c false.
  *
  * @param useabstract It indicates whether the abstract representation of
  *        the constraints (if it has been generated) must be used to
  *        determine if the current solution is feasible.
  *
  * @param fsbc If it is a pointer to a SimpleConfiguration< double >, then
  *        the value stored in that SimpleConfiguration will be the
  *        tolerance that determines if a solution is feasible. */

 bool is_feasible( bool useabstract = false ,
                   Configuration * fsbc = nullptr ) override;

/**@} ----------------------------------------------------------------------*/
/*--------------------- PROTECTED PART OF THE CLASS ------------------------*/
/*--------------------------------------------------------------------------*/

protected:

/*--------------------------------------------------------------------------*/
/*---------------------------- PROTECTED METHODS ---------------------------*/
/*--------------------------------------------------------------------------*/

 void load( std::istream &input , char frmt = 0 ) override {
  throw( std::logic_error( "InvestmentBlock::load(): not implemented yet." )
	 );
  }

 /// states that the Variable of the UnitBlock have been generated
 void set_variables_generated() { AR |= HasVar; }

 /// states that the Constraint of the UnitBlock have been generated
 void set_constraints_generated() { AR |= HasCst; }

 /// states that the Objective of the UnitBlock has been generated
 void set_objective_generated() { AR |= HasObj; }

 /// indicates whether the Variable of the UnitBlock have been generated
 bool variables_generated() const { return( AR & HasVar ); }

 /// indicates whether the Constraint of the UnitBlock have been generated
 bool constraints_generated() const { return( AR & HasCst ); }

 /// indicates whether the Objective of the UnitBlock has been generated
 bool objective_generated() const { return( AR & HasObj ); }

 /// indicates whether this is the disaggregated (1:K) path
 /** True iff the Block was built as a disaggregated sum of K component
  * sub-Blocks (each holding an InvestmentFunction with its own weight);
  * false on the legacy single-component path (where the InvestmentFunction
  * lives in this Block's own objective and there are no sub-Blocks). The
  * structure itself is the discriminator: components exist iff nested
  * Blocks exist (sub-Blocks are only ever created by add_component()). */
 bool is_disaggregated() const { return( get_number_nested_Blocks() > 0 ); }

/*--------------------------------------------------------------------------*/
/*---------------------------- PROTECTED FIELDS ----------------------------*/
/*--------------------------------------------------------------------------*/

 int f_objective_sense = Objective::eMin;  ///< the sense of the Objective

 /// indicates whether the bound constraints must be reformulated
 int f_reformulate_bounds = 0;

 Index f_num_sub_blocks_per_stage = 1;

 Index f_num_sub_blocks = 1;
 ///< number of replica sub-Blocks for the multi-replica SDDPBlock path

 FRealObjective objective;  ///< the Objective of this InvestmentBlock

 /// the active Variable in the InvestmentFunction
 std::vector< ColVariable > v_variables;

 std::vector< double > v_lower_bound;  ///< lower bound on the Variable
 std::vector< double > v_upper_bound;  ///< upper bound on the Variable

 /// Box constraints on the Variable
 std::vector< BoxConstraint > v_constraints;

/*--------------------------------------------------------------------------*/
/*--------------------- PRIVATE PART OF THE CLASS --------------------------*/
/*--------------------------------------------------------------------------*/

 private:

/*--------------------------------------------------------------------------*/
/*-------------------------- PRIVATE FIELDS --------------------------------*/
/*--------------------------------------------------------------------------*/

 unsigned char AR{}; ///< bit-wise coded: what abstract is there

 static constexpr unsigned char HasVar = 1;
 ///< first bit of AR == 1 if the Variables have been constructed
 static constexpr unsigned char HasCst = 2;
 ///< second bit of AR == 1 if the Constraints have been constructed
 static constexpr unsigned char HasObj = 4;
 ///< third bit of AR == 1 if the Objective has been constructed

 SMSpp_insert_in_factory_h;

/*--------------------------------------------------------------------------*/

 };   // end( class InvestmentBlock )

/*--------------------------------------------------------------------------*/
/*--------------------- CLASS InvestmentBlockSolution ----------------------*/
/*--------------------------------------------------------------------------*/
/*--------------------------- GENERAL NOTES --------------------------------*/
/*--------------------------------------------------------------------------*/
/// a Solution of a InvestmentBlock
/** The InvestmentBlockSolution class, derived from Solution, represents a
 * solution of a InvestmentBlock, i.e.:
 *
 * - the values of the design variables
 *
 * - optionally, the :Solution to the inner Block in the InvestmentFunction
 *   of the InvestmentBlock
 *
 * Note that the InvestmentBlockSolution can be provided with an inner
 * Configuration that is passed to the inner Block in the InvestmentFunction
 * of the InvestmentBlock when retrieving its :Solution. This Configuration
 * is *not* serialize()-d and deserialize()-d because it is only useful when
 * the InvestmentBlockSolution is first read(); when it is deserialize()-d the
 * exact details of the inner :Solution are already known, and therefore there
 * is no point in serialize()-ing the Configuration. */

class InvestmentBlockSolution : public Solution
{
/*--------------------------------------------------------------------------*/
/*----------------------- PUBLIC PART OF THE CLASS -------------------------*/
/*--------------------------------------------------------------------------*/

 public:

/*------------------------------- FRIENDS ----------------------------------*/

 using Index = Block::Index;  // "import" Index
 
/*------------------------------- FRIENDS ----------------------------------*/

 friend InvestmentBlock;  ///< make InvestmentBlock friend

/*---------- CONSTRUCTING AND DESTRUCTING InvestmentBlockSolution ----------*/

 explicit InvestmentBlockSolution( void ) :
           f_inner_Configuration( nullptr ) { }
 /// constructor, it has nothing to do

/*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

 void deserialize( const netCDF::NcGroup & group ) override final;

/*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

 ~InvestmentBlockSolution() {
  delete f_inner_Configuration;
  for( auto s : v_inner_Solutions )
   delete s;
  }

/*------- METHODS DESCRIBING THE BEHAVIOR OF A InvestmentBlockSolution -----*/

 void read( const Block * block ) override final;

 void write( Block * block ) override final;

/*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/
 /// serialize a InvestmentBlockSolution into a netCDF::NcGroup
 /** Serialize a InvestmentBlockSolution into a netCDF::NcGroup, with the
  * following format:
  *
  * - The mandatory dimension "NumDesignVariables" containing the number of
  *   design variables in the InvestmentBlock
  *
  * - The mandatory variable "DesignVariables", of type netCDF::NcDouble and
  *   indexed over the dimension "NumDesignVariables"; DesignVariables[ i ]
  *   is assumed to contain the vaule of the i-th design variable
  *
  * - The group "InnerSolution" containing the [:Solution] of the inner
  *   Block of the InvestmentBlock corresponding to the value of the design
  *   variables. The group is optional. */

 void serialize( netCDF::NcGroup & group ) const override final;

/*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

 InvestmentBlockSolution * scale( double factor ) const override final;

 void sum( const Solution * solution , double multiplier ) override final;

 InvestmentBlockSolution * clone( bool empty = false ) const override final;

/*-------------------- PROTECTED PART OF THE CLASS -------------------------*/

 protected:

/*-------------------------- PROTECTED METHODS -----------------------------*/

 void print( std::ostream &output ) const override final {
  output << "InvestmentBlockSolution [" << this << "]: " << std::endl;
  }

/*---------------------- PRIVATE PART OF THE CLASS -------------------------*/

 private:

/*---------------------------- PRIVATE FIELDS ------------------------------*/

 std::vector< double > v_design;  ///< the design variables

 std::vector< Solution * > v_inner_Solutions;
 ///< the :Solution(s) of the inner Block(s): 1 (legacy) or K (disaggregated)

 bool f_inner_wanted = false;  ///< whether the inner :Solution(s) must be read

 Configuration * f_inner_Configuration;
             ///< the Configuration for the inner :Solution of the InnerBlock

/*--------------------------------------------------------------------------*/

 SMSpp_insert_in_factory_h;

/*--------------------------------------------------------------------------*/

 };  // end( class( InvestmentBlockSolution ) )

/** @} end( group( InvestmentBlock_CLASSES ) ) -----------------------------*/
/*--------------------------------------------------------------------------*/

}  // end( namespace SMSpp_di_unipi_it )

/*--------------------------------------------------------------------------*/
/*--------------------------------------------------------------------------*/

#endif  /* InvestmentBlock.h included */

/*--------------------------------------------------------------------------*/
/*--------------------- End File InvestmentBlock.h -------------------------*/
/*--------------------------------------------------------------------------*/
