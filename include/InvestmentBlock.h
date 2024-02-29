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
 * \copyright &copy; by Rafael Durbano Lobato
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

/*--------------------------------------------------------------------------*/
/*----------------------------- NAMESPACE ----------------------------------*/
/*--------------------------------------------------------------------------*/

/// namespace for the Structured Modeling System++ (SMS++)
namespace SMSpp_di_unipi_it
{

 class BoxConstraint;   // forward declaration of BoxConstraint

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

class InvestmentBlock : public Block {

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

/**@} ----------------------------------------------------------------------*/
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

/**@} ----------------------------------------------------------------------*/
/*----------- METHODS FOR READING THE DATA OF THE InvestmentBlock ----------*/
/*--------------------------------------------------------------------------*/
/** @name Reading the data of the InvestmentBlock
    @{ */

 /// Return the number of Variable of this InvestmentBlock
 /** This function returns the number of Variable of this InvestmentBlock.
  *
  * @return The number of Variable of this InvestmentBlock. */

 Index get_number_variables() const {
  return v_variables.size();
 }

/*--------------------------------------------------------------------------*/

 /// returns the vector of ColVariable of this InvestmentBlock
 /** This function returns a const reference to the vector of ColVariable of
  * this InvestmentBlock.
  *
  * @return A const reference to the vector of ColVariable of
  *         this InvestmentBlock. */

 const std::vector< ColVariable > & get_variables() const {
  return v_variables;
 }

/*--------------------------------------------------------------------------*/

 /// returns a vector with the values of the ColVariable of this InvestmentBlock
 /** This function return a vector containing the values of the of ColVariable
  * of this InvestmentBlock.
  *
  * @return A vector containing the values of the of ColVariable
  *         of this InvestmentBlock. */

 std::vector< double > get_variable_values() const {
  std::vector< double > variable_values;
  variable_values.reserve( v_variables.size() );
  for( const auto & variable : v_variables )
   variable_values.push_back( variable.get_value() );
  return variable_values;
 }

/*--------------------------------------------------------------------------*/

 Function * get_function() const {
  return objective.get_function();
 }

/*--------------------------------------------------------------------------*/

 /// returns the lower bound on each Variable
 const std::vector< double > & get_variable_lower_bound() const {
  return v_lower_bound;
 }

/*--------------------------------------------------------------------------*/

 /// returns the box constraints on the Variable
 const std::vector< BoxConstraint > & get_constraints() const {
  return v_constraints;
 }

/**@} ----------------------------------------------------------------------*/
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
  * BoxConstraint (if generated) are relaxed, then this function will return \c
  * true if \p useabstract is \c true, and \c false if \p useabstract is \c
  * false.
  *
  * @param useabstract It indicates whether the abstract representation of the
  *        constraints (if it has been generated) must be used to determine if
  *        the current solution is feasible.
  *
  * @param fsbc If it is a pointer to a SimpleConfiguration< double >, then the
  *        value stored in that SimpleConfiguration will be the tolerance that
  *        determines if a solution is feasible. */

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
  throw( std::logic_error( "InvestmentBlock::load(): not implemented yet." ) );
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

/*--------------------------------------------------------------------------*/
/*---------------------------- PROTECTED FIELDS ----------------------------*/
/*--------------------------------------------------------------------------*/

 /// The sense of the Objective
 int f_objective_sense = Objective::eMin;

 /// It indicates whether the bound constraints must be reformulated
 int f_reformulate_bounds = 0;

 Index f_num_sub_blocks_per_stage = 1;

 /// The Objective of this InvestmentBlock
 FRealObjective objective;

 /// The Variable that are the active ones in the InvestmentFunction
 std::vector< ColVariable > v_variables;

 /// Lower bound on the Variable
 std::vector< double > v_lower_bound;

 /// Upper bound on the Variable
 std::vector< double > v_upper_bound;

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

/** @} end( group( InvestmentBlock_CLASSES ) ) */

}  // end( namespace SMSpp_di_unipi_it )

/*--------------------------------------------------------------------------*/
/*--------------------------------------------------------------------------*/

#endif  /* InvestmentBlock.h included */

/*--------------------------------------------------------------------------*/
/*--------------------- End File InvestmentBlock.h -------------------------*/
/*--------------------------------------------------------------------------*/
