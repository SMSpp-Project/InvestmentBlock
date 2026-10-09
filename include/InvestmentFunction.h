/*--------------------------------------------------------------------------*/
/*----------------------- File InvestmentFunction.h ------------------------*/
/*--------------------------------------------------------------------------*/
/** @file
 * Header file for the class InvestmentFunction, which derives from both
 * C05Function and Block. It represents a function that can compute the value
 * of an investment in a set of assets.
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

#ifndef __InvestmentFunction
#define __InvestmentFunction
                      /* self-identification: #endif at the end of the file */

/*--------------------------------------------------------------------------*/
/*------------------------------ INCLUDES ----------------------------------*/
/*--------------------------------------------------------------------------*/

#include <functional>

#include "Block.h"

#include "C05Function.h"

#include "CDASolver.h"

/*--------------------------------------------------------------------------*/
/*--------------------------- NAMESPACE ------------------------------------*/
/*--------------------------------------------------------------------------*/

/// namespace for the Structured Modeling System++ (SMS++)
namespace SMSpp_di_unipi_it
{
 class BatteryUnitBlock;       // forward declaration of BatteryUnitBlock

 class BendersBFunction;       // forward declaration of BendersBFunction

 class BlockSolverConfig;      // forward declaration of BlockSolverConfig

 class IntermittentUnitBlock;  // forward declaration of IntermittentUnitBlock

 class SDDPBlock;              // forward declaration of SDDPBlock

 class TwoStageStochasticBlock;   // forward declaration of
                                  // TwoStageStochasticBlock

 class SDDPSolver;             // forward declaration of SDDPSolver

 class UCBlock;                // forward declaration of UClock

 class UnitBlock;              // forward declaration of UnitBlock

/*--------------------------------------------------------------------------*/
/*------------------------------- CLASSES ----------------------------------*/
/*--------------------------------------------------------------------------*/
/** @defgroup InvestmentFun_CLASSES Classes in InvestmentFunction.h
 *  @{ */

/*--------------------------------------------------------------------------*/
/*----------------------- CLASS InvestmentFunction -------------------------*/
/*--------------------------------------------------------------------------*/
/*--------------------------- GENERAL NOTES --------------------------------*/
/*--------------------------------------------------------------------------*/
/// a C05Function giving the cost of an investment in a set of assets
/** An InvestmentFunction is a C05Function that gives the cost of an
 * investment in a set of assets of an energy system, i.e., the cost of the
 * investment plus the operational cost of the system with the capacities
 * that result from it. InvestmentFunction derives from *both* C05Function
 * and Block, and the operational problem is its inner Block. Its main
 * ingredients are the following.
 *
 * - Its active ColVariable are the investments that can be made in certain
 *   assets, where the i-th one is the investment in the i-th asset. An asset
 *   is either a UnitBlock of the UCBlock where the investment takes place (a
 *   ThermalUnitBlock, a BatteryUnitBlock or an IntermittentUnitBlock),
 *   identified by its index there, or the converter of a BatteryUnitBlock,
 *   identified by the index of the unit, or a line of its network,
 *   identified by its index in the NetworkData [see serialize()].
 *
 * - The inner Block is the operational problem. It is either a UCBlock, or
 *   a TwoStageStochasticBlock whose scenarios (more precisely, the leaves
 *   of its tree of scenarios) are UCBlock, or an SDDPBlock whose stages are
 *   UCBlock inside a BendersBFunction; several identical SDDPBlock can also
 *   be given, in which case the scenarios are divided among them. The same
 *   investment is written into each of these UCBlock, hence it is the same
 *   for all the scenarios and all the stages.
 *
 * - Its value is the sum of the cost of the investment, a piecewise-linear
 *   function of the active ColVariable, and of the value of the operational
 *   problem, which the Solver of the inner Block gives once the investment
 *   has been written into it.
 *
 * Formally, let \f$ x \in \mathbb{R}^m \f$ be the vector of the investments,
 * one per asset (if the bounds of the active ColVariable have been
 * reformulated, \f$ x_i \f$ is the value of the i-th of them plus its lower
 * bound, cf. reformulated_bounds()). Let also \f$ \bar x_i \f$ be the
 * quantity of the i-th asset that is already installed, and \f$ c_i \f$ and
 * \f$ d_i \f$ the costs of investing and of disinvesting one unit of it
 * ("InstalledQuantity", "Cost" and "DisinvestmentCost" in serialize()). With
 * these data, the InvestmentFunction is
 * \f[
 *   \mathcal{F}( x ) = \sum_{ i = 1 }^m \bigl( \, c_i \, ( x_i - \bar x_i
 *     )^+ + d_i \, ( \bar x_i - x_i )^+ \, \bigr) + Q( x ) \; , \tag{1}
 * \f]
 * where \f$ ( a )^+ = \max \{ a , 0 \} \f$ and \f$ Q( x ) \f$ is the value of
 * the operational problem with the investment \f$ x \f$, as follows.
 * According to the asset, the investment \f$ x_i \f$ is written into the
 * UCBlock in one of two ways:
 *
 * - as the scale factor of the UnitBlock [see UnitBlock::scale()], i.e.,
 *   \f$ x_i \f$ is the number of identical copies of the unit, which share
 *   its Variable. The contributions of the unit to the linking constraints
 *   of the UCBlock and to the Objective are then those of one copy times
 *   \f$ x_i \f$. This is the case of a ThermalUnitBlock, and of a
 *   BatteryUnitBlock or an IntermittentUnitBlock if the attribute
 *   "ReplicateBatteryUnits" or "ReplicateIntermittentUnits", respectively,
 *   is nonzero;
 *
 * - as the kappa constant of the asset, i.e., \f$ x_i \f$ multiplies its
 *   capacity data. These are the minimum and maximum power of an
 *   IntermittentUnitBlock, and therefore the reserve it can give [see
 *   IntermittentUnitBlock::set_kappa()], the power, storage, converter and
 *   reserve limits of a BatteryUnitBlock [see BatteryUnitBlock::set_kappa()]
 *   (the converter only if it is not an asset of its own), the converter
 *   limits alone if the asset is the converter of a BatteryUnitBlock [see
 *   BatteryUnitBlock::set_converter_kappa()], in which case storage and
 *   converter are two assets of the unit whose sizes are chosen apart,
 *   and the two flow limits \f$ \kappa_l C^v P^{mn}_l \le F_l \le \kappa_l
 *   C^v P^{mx}_l \f$ of a line \f$ l \f$ in the DCNetworkBlock of each time
 *   instant [see DCNetworkBlock::set_kappa()]. A line can only be an asset
 *   of a DCNetworkBlock: with any other NetworkBlock, writing the investment
 *   fails, and compute() returns kError with the value \f$ +\infty \f$
 *   (\f$ -\infty \f$ for a maximization).
 *
 * In both cases \f$ x_i \f$ multiplies the capacity of the asset as a whole,
 * the installed one included. The capacity data of the UnitBlock or of the
 * line are those of one unit of the asset; for a kappa constant they are its
 * maximum capacity times the profile of its availability (e.g., the maximum
 * power of an intermittent unit is the installable capacity times the load
 * factor at each instant). Only the part \f$ x_i - \bar x_i \f$ is charged in
 * (1). Thus, \f$ x_i \in [ 0 , 1 ] \f$ is the fraction of the maximum
 * capacity that is installed if the upper bound of the active ColVariable is
 * 1, while \f$ x_i \in \{ 0 , 1 , \ldots , U_i \} \f$ is the number of copies
 * of a unit if the ColVariable is integer with upper bound \f$ U_i \f$ (the
 * maximum number of units). The bounds and the integrality are those of the
 * InvestmentBlock ("LowerBound", "UpperBound" and "Integer" there), which by
 * default leaves the ColVariable continuous and unbounded, and therefore the
 * bounds (at least the lower one) usually have to be given in the data. Note
 * that the cost of the investment is the piecewise-linear function in (1) of
 * the new capacity, with a kink at the installed quantity, and an asset can
 * also be reduced, at the price \f$ d_i \f$; with \f$ \bar x_i = 0 \f$ (the
 * default) and \f$ x_i \ge 0 \f$, it is the linear cost \f$ c_i x_i \f$.
 *
 * The operational cost \f$ Q( x ) \f$ is the value that the (first) Solver
 * registered to the inner Block gives once the investment has been written
 * into it, i.e., what the Solver returns as the value of its solution (cf.
 * Solver::get_var_value()), whatever the Solver is. Hence, it is the
 * optimal value of the unit commitment problem if the Solver solves it
 * exactly, and the optimal value of its continuous relaxation if the Solver
 * solves that. If the Solver is a LagrangianDualSolver, \f$ Q( x ) \f$ is
 * the value of the Lagrangian Dual, i.e., a lower bound on the optimal
 * value, which (if the Objective of each unit is linear) is equal to that
 * of the problem in which the feasible set of each unit is replaced by its
 * convex hull. Precisely:
 *
 * - with a UCBlock, \f$ Q( x ) \f$ is the value given by its Solver;
 *
 * - with a TwoStageStochasticBlock, it is the value given by the Solver of
 *   the TwoStageStochasticBlock, i.e., the expected operational cost over
 *   its scenarios (whose probabilities are in the Objective of their
 *   UCBlock);
 *
 * - with an SDDPBlock, the cuts of the future cost functions are first
 *   computed by the SDDPSolver registered to the SDDPBlock, if any (after
 *   removing the existing ones, if the investment has changed). Then the
 *   policy they define is simulated over each of the \f$ N \f$ scenarios of
 *   the SDDPBlock by an SDDPGreedySolver, and \f$ Q( x ) \f$ is the mean
 *   \f$ \frac{1}{N} \sum_{ k = 1 }^N Q^k( x ) \f$ of the costs
 *   \f$ Q^k( x ) \f$ of the simulations. This is an unbiased statistical
 *   estimate of the expected operational cost of that policy, which for a
 *   minimization is not below the optimal one, and it differs from the
 *   lower bound given by the cuts.
 *
 * If intComputeLinearization is nonzero (the default), compute() also gives a
 * linearization of \f$ \mathcal{F} \f$ at \f$ x \f$. Its coefficients are the
 * derivative of the cost of the investment (\f$ c_i \f$ if
 * \f$ x_i > \bar x_i \f$, and \f$ - d_i \f$ otherwise, which is a subgradient
 * at the kink if \f$ c_i + d_i \ge 0 \f$) plus a linearization of \f$ Q \f$.
 * The latter is read from the dual values of the inner Block, i.e., from the
 * multipliers \f$ y \f$ of its constraints that its Solver writes there [see
 * RowConstraint::get_dual()], in a way that again depends on the asset.
 *
 * - for an asset represented by a scale factor, the coefficient is
 *   \f[
 *     f^1_i( \bar z_i ) + \bar y^\top g^1_i( \bar z_i ) \; , \tag{2}
 *   \f]
 *   where \f$ f^1_i \f$ is the Objective of one copy of the UnitBlock and
 *   \f$ \bar z_i \f$ is its current solution if it has copies, and else a
 *   minimizer of (2) over the feasible set of one copy (see (4) below).
 *   The vector \f$ g^1_i \f$ holds the contributions of one copy to the
 *   linking constraints of the UCBlock (the active and reactive node
 *   injection constraints, the primary and secondary reserve, inertia and
 *   pollutant budget ones, cf. UCBlock::generate_abstract_constraints()),
 *   whose dual values are \f$ \bar y \f$; the fixed consumption of a unit
 *   that is off is among the contributions to the active ones;
 *
 * - for a line \f$ l \f$, the coefficient is
 *   \f[
 *     \sum_t C^v \bigl( \, P^{mn}_l \mu^{mn}_{t,l} - P^{mx}_l
 *     \mu^{mx}_{t,l} \, \bigr) \; , \tag{3}
 *   \f]
 *   where \f$ \mu^{mn}_{t,l} , \mu^{mx}_{t,l} \ge 0 \f$ are the multipliers
 *   of the two sides of the flow limits of the line in the DCNetworkBlock
 *   of instant \f$ t \f$ (at most one of the two is nonzero, and which one
 *   is given by the sign of the dual value). Formula (3) holds whatever the
 *   formulation
 *   of the network and the susceptances of the lines, since the flow limits
 *   bound the flow Variable of each line [see
 *   DCNetworkBlock::generate_abstract_constraints()]. A line that has a
 *   design variable \f$ x_l \f$ in the DCNetworkBlock has instead the flow
 *   limits \f$ F_l - \kappa_l C^v P^{mx}_l x_l \le 0 \f$ (and the lower
 *   one), in which \f$ \kappa_l \f$ multiplies a Variable: the optimal
 *   value is then not a convex function of \f$ \kappa_l \f$, and the
 *   linearization is refused (as
 *   IntermittentUnitBlock::get_kappa_linearization() and
 *   BatteryUnitBlock::get_kappa_linearization() refuse it for a unit with a
 *   design variable): compute() then returns the status of the Solver of
 *   the inner Block with neither a value, get_value() giving
 *   \f$ +\infty \f$ (\f$ -\infty \f$ for a maximization), nor a
 *   linearization;
 *
 * - for a BatteryUnitBlock or an IntermittentUnitBlock represented by its
 *   kappa constant, the coefficient is the one given by
 *   BatteryUnitBlock::get_kappa_linearization() and
 *   IntermittentUnitBlock::get_kappa_linearization(), i.e., the sum of the
 *   multipliers of the constraints of the unit whose right-hand sides kappa
 *   multiplies, each times the datum it multiplies there, and for the
 *   converter of a BatteryUnitBlock it is the one given by
 *   BatteryUnitBlock::get_converter_kappa_linearization().
 *
 * With a TwoStageStochasticBlock these coefficients are summed over the
 * UCBlock of its scenarios (since the probabilities are in their dual
 * values), and with an SDDPBlock they are averaged over the simulations as
 * the value is. By the envelope theorem, (2) and (3) are subgradients of
 * \f$ Q \f$ if \f$ Q \f$ is the optimal value of a convex problem and the
 * dual values are optimal ones. Indeed, a kappa constant of an intermittent
 * unit or of a line only enters right-hand sides of linear constraints, and
 * the optimal value of a convex problem is a convex function of its
 * right-hand sides. A BatteryUnitBlock with the binary variables
 * \f$ u^{ch}_t \f$ has instead the rows
 * \f$ p^-_t \le - \kappa P^{mn}_t u^{ch}_t \f$ and \f$ p^+_t \le \kappa
 * P^{mx}_t ( 1 - u^{ch}_t ) \f$, in which \f$ \kappa \f$ multiplies a
 * Variable; in their continuous relaxation, however, the change of variable
 * \f$ w_t = \kappa u^{ch}_t \f$ turns them into
 * \f$ p^-_t \le - P^{mn}_t w_t \f$,
 * \f$ p^+_t \le P^{mx}_t ( \kappa - w_t ) \f$ and
 * \f$ 0 \le w_t \le \kappa \f$, which are linear in
 * \f$ ( p , w , \kappa ) \f$ jointly, and therefore the optimal value is
 * again convex in \f$ \kappa \f$ (see
 * BatteryUnitBlock::get_kappa_linearization() for the terms of these rows).
 * The kappa of a converter that is an asset of its own enters the
 * right-hand sides of the converter rows alone, and its coefficient is
 * BatteryUnitBlock::get_converter_kappa_linearization(). A scale factor,
 * instead, multiplies all the contributions of a unit, and the convexity of
 * \f$ Q \f$ in \f$ x_i \f$ is that of a perspective: with \f$ Z_i \f$ the
 * feasible set of one copy, convex if the unit is relaxed, \f$ x_i \f$
 * copies contribute \f$ x_i f^1_i( z_i ) \f$ and \f$ x_i g^1_i( z_i ) \f$
 * for some \f$ z_i \in Z_i \f$, i.e., with \f$ w_i = x_i z_i \f$, the
 * perspective \f$ x_i f^1_i( w_i / x_i ) \f$ over the cone
 * \f$ \{ ( w_i , x_i ) : x_i > 0 , \, w_i \in x_i Z_i \} \f$ (closed by
 * \f$ w_i = 0 \f$ at \f$ x_i = 0 \f$ if \f$ Z_i \f$ is bounded), which is
 * jointly convex in \f$ ( w_i , x_i ) \f$, while \f$ g^1_i \f$ is linear,
 * as it is in the rows of the UCBlock. The optimal value of the inner Block
 * is therefore the minimum over \f$ w \f$ of a function jointly convex in
 * \f$ ( w , x ) \f$, a convex function of \f$ x \f$. In the Lagrangian Dual of
 * the linking constraints, the contribution of the unit is
 * \f[
 *   x_i \, \varphi_i( y ) \; , \quad \varphi_i( y ) = \min \{ \,
 *   f^1_i( z_i ) + y^\top g^1_i( z_i ) \; : \; z_i \in Z_i \, \} \; ,
 *   \tag{4}
 * \f]
 * which is affine in \f$ x_i \f$ for each \f$ y \f$, and the rest of the dual
 * function does not depend on \f$ x_i \f$. Hence, if the inner Block is
 * solved as a convex problem (its continuous relaxation, or its Lagrangian
 * Dual by a LagrangianDualSolver), then \f$ Q \f$ is the supremum over
 * \f$ y \f$ of functions affine in \f$ x \f$, the one of an optimal
 * \f$ \bar y \f$ at \f$ \bar x \f$ is below \f$ Q \f$ everywhere and equal to
 * it at \f$ \bar x \f$, and \f$ \varphi_i( \bar y ) \f$ is the coefficient of
 * \f$ x_i \f$ in a subgradient. If \f$ \bar x_i > 0 \f$, the solution
 * \f$ \bar z_i \f$ of the unit is a minimizer in (4) (otherwise the copies
 * could do better against \f$ \bar y \f$, and neither the primal nor the
 * dual solution would be optimal), and (4) is (2). If \f$ \bar x_i = 0 \f$,
 * instead, the unit weighs nothing in the inner Block, and its solution is
 * arbitrary (typically the unit is off, and (2) is 0 even where building
 * the unit pays, which stops a method that starts at 0 there): then
 * \f$ \varphi_i( \bar y ) \f$ is computed by solving a copy of the unit
 * alone with the Objective of (4) [see compute_zero_copy_linearization()],
 * by the Solver of the BlockSolverConfig of the units, which has to solve
 * \f$ Z_i \f$ as the inner Block does [see set_ComputeConfig()]. Since
 * \f$ x_i \ge 0 \f$, a lower estimate of \f$ \varphi_i( \bar y ) \f$ at
 * \f$ \bar x_i = 0 \f$ (e.g., from a relaxation of the copy) still gives a
 * linearization below \f$ Q \f$ over the domain, if a less tight one. The
 * copies of a scaled unit are then exact for the convexified problem, while
 * for the unit commitment problem they are a restriction, since all the
 * copies of a unit take the same decisions. If the inner Block is solved
 * as a mixed-integer problem, \f$ Q \f$ is typically not convex and the
 * linearization may not be a subgradient. With an SDDPBlock the
 * linearization is the mean of those of the simulations, a subgradient of
 * the mean of their values for the current policy rather than of the
 * expected cost. The cost of the investment in (1)
 * is convex if and only if \f$ c_i + d_i \ge 0 \f$ for all \f$ i \f$. Thus,
 * \f$ \mathcal{F} \f$ is convex if this holds and \f$ Q \f$ is convex;
 * is_convex() checks the former, while the latter depends on the Solver of
 * the inner Block and cannot be checked. The domain of \f$ \mathcal{F} \f$ is
 * given by the bounds and the linear constraints of the InvestmentBlock. If
 * \f$ x \f$ violates the latter, compute() gives the value \f$ +\infty \f$
 * (\f$ -\infty \f$ for a maximization) without solving the inner Block; so it
 * does if the inner Block is infeasible at \f$ x \f$. In this second case, if
 * the Solver of the inner Block gives a certificate of the infeasibility (an
 * unbounded dual direction), a vertical linearization is read from it as the
 * diagonal ones are. This is only done if all the assets are represented by
 * kappa constants, whose certificate is valid away from the current
 * investment when they enter right-hand sides only (intermittent units and
 * lines; for a battery with binary variables, the continuous relaxation after
 * the change of variable above): for a scale factor, which multiplies the
 * columns of a unit, it may not be. With the integrality of the active
 * ColVariable relaxed, minimizing \f$ \mathcal{F} \f$ over its domain is
 * therefore a convex nondifferentiable problem, which a Solver for
 * C05Function (e.g., BundleSolver) attached to the InvestmentBlock can solve.
 * Also, one can evaluate the InvestmentFunction at a single investment, or at
 * two of them to compare them, or use it only for its linearization, which
 * gives the sensitivity of the total cost to the capacity of each asset.
 *
 * We do not model some features that a capacity expansion model may have. For
 * instance, one InvestmentFunction has no worst case over a set
 * \f$ \Upsilon \f$ of alternative models of the uncertainty (meta-scenarios),
 * i.e., it does not give
 * \f[
 *   \min_x \; \Bigl\{ \, \sum_{ i = 1 }^m \bigl( c_i ( x_i - \bar x_i )^+ +
 *   d_i ( \bar x_i - x_i )^+ \bigr) + \max_{ \upsilon \in \Upsilon }
 *   Q_\upsilon( x ) \, \Bigr\} \; ,
 * \f]
 * with \f$ Q_\upsilon \f$ the expected operational cost under the model
 * \f$ \upsilon \f$. One can obtain this with one InvestmentFunction
 * \f$ \mathcal{F}_\upsilon \f$ per model, all of them on the same investment,
 * and an outer problem minimizing a variable \f$ v \f$ subject to
 * \f$ v \ge \mathcal{F}_\upsilon( x ) \f$ for all
 * \f$ \upsilon \in \Upsilon \f$. Since the cost of the investment is the same
 * in all of them, \f$ \max_\upsilon \mathcal{F}_\upsilon \f$ is the function
 * above; it is convex if each \f$ \mathcal{F}_\upsilon \f$ is, and any convex
 * combination of the linearizations of the \f$ \mathcal{F}_\upsilon \f$
 * attaining the maximum is a subgradient of it. Also, an investment can only
 * be made in the assets listed above, neither in lines of a network that is
 * not a DCNetworkBlock (e.g., a distribution grid) nor in connections between
 * networks. Finally, the investment is the same for all the stages and all
 * the scenarios, and therefore it cannot depend on the time or on the
 * realization of the uncertainty. */

class InvestmentFunction : public C05Function , public Block {

/*--------------------------------------------------------------------------*/
/*----------------------- PUBLIC PART OF THE CLASS -------------------------*/
/*--------------------------------------------------------------------------*/

 public:

/*--------------------------------------------------------------------------*/
/*-------------------------------- FRIENDS ---------------------------------*/
/*--------------------------------------------------------------------------*/

 friend class InvestmentFunctionState;

/*--------------------------------------------------------------------------*/
/*---------------------- PUBLIC TYPES OF THE CLASS -------------------------*/
/*--------------------------------------------------------------------------*/
/** @name Public Types
 *  @{ */

 /// public enum representing the types of assets
 /** Public enum representing the types of assets. */

 enum AssetType { eUnitBlock = 0 , eLine = 1 , eConverter = 2 };

 /// public enum representing the sides of the linear constraints
 /** Public enum representing the sides of the linear constraints. */

 enum ConstraintSide { eLHS = 0 , eRHS = 1 };

 /* Since InvestmentFunction is both a ThinVarDepInterface and a Block, it
  * "sees" two definitions of "Index", "Range", and "Subset". These are
  * actually the same, but compilers still don't like it. Disambiguate by
  * declaring we use the ThinVarDepInterface versions (but it could have been
  * the Block versions, as they are the same). */

 using Index    = ThinVarDepInterface::Index;
 using c_Index  = ThinVarDepInterface::c_Index;

 using Range    = ThinVarDepInterface::Range;
 using c_Range  = ThinVarDepInterface::c_Range;

 using Subset   = ThinVarDepInterface::Subset;
 using c_Subset = ThinVarDepInterface::c_Subset;

 using IndexVector = std::vector< Index >;
 using RealVector = std::vector< double >;
 using MultiVector = std::vector< RealVector >;
 using AssetTypeVector = std::vector< AssetType >;

 using VarVector = std::vector< ColVariable * >;
 ///< representing the x variables upon which the function depends

 using ViolatedConstraint = std::pair< Index , ConstraintSide >;

/*--------------------------------------------------------------------------*/
 /// virtualized concrete iterator
 /** A concrete class deriving from ThinVarDepInterface::v_iterator and
  * implementing the concrete iterator for sifting through the "active"
  * Variable of an InvestmentFunction. */

 class v_iterator : public ThinVarDepInterface::v_iterator
 {
  public:

  explicit v_iterator( VarVector::iterator & itr ) : itr_( itr ) {}
  explicit v_iterator( VarVector::iterator && itr )
   : itr_( std::move( itr ) ) {}

  v_iterator * clone( void ) override final {
   return( new v_iterator( itr_ ) );
   }

  void operator++( void ) override final { ++(itr_); }

  reference operator*( void ) const override final {
   return( *( ( *itr_ ) ) );
   }
  pointer operator->( void ) const override final {
   return( ( *itr_ ) );
   }

  bool operator==( const ThinVarDepInterface::v_iterator & rhs )
   const override final {
   #ifdef NDEBUG
    auto tmp = static_cast< const InvestmentFunction::v_iterator * >( & rhs );
    return( itr_ == tmp->itr_ );
   #else
    auto tmp = dynamic_cast< const InvestmentFunction::v_iterator * >( & rhs );
    return( tmp ? itr_ == tmp->itr_ : false );
   #endif
   }
  bool operator!=( const ThinVarDepInterface::v_iterator & rhs )
   const override final {
   #ifdef NDEBUG
    auto tmp = static_cast< const InvestmentFunction::v_iterator * >( & rhs );
    return( itr_ != tmp->itr_ );
   #else
    auto tmp = dynamic_cast< const InvestmentFunction::v_iterator * >( & rhs );
    return( tmp ? itr_ != tmp->itr_ : true );
   #endif
   }

  private:

  VarVector::iterator itr_;
  };

/*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/
 /// virtualized concrete const_iterator
 /** A concrete class deriving from ThinVarDepInterface::v_const_iterator and
  * implementing the concrete iterator for sifting through the "active"
  * Variable of an InvestmentFunction. */

 class v_const_iterator : public ThinVarDepInterface::v_const_iterator
 {
  public:

  explicit v_const_iterator( VarVector::const_iterator & itr ) : itr_( itr ) {}
  explicit v_const_iterator( VarVector::const_iterator && itr )
   : itr_( std::move( itr ) ) {}

  v_const_iterator * clone( void ) override final {
   return( new v_const_iterator( itr_ ) );
   }

  void operator++( void ) override final { ++( itr_ ); }

  reference operator*( void ) const override final {
   return( *( ( *itr_ ) ) );
   }
  pointer operator->( void ) const override final { return( ( *itr_ ) ); }

  bool operator==( const ThinVarDepInterface::v_const_iterator & rhs )
   const override final {
   #ifdef NDEBUG
    auto tmp = static_cast< const InvestmentFunction::v_const_iterator * >(
								      & rhs );
    return( itr_ == tmp->itr_ );
   #else
    auto tmp = dynamic_cast< const InvestmentFunction::v_const_iterator * >(
								      & rhs );
    return( tmp ? itr_ == tmp->itr_ : false );
   #endif
   }
  bool operator!=( const ThinVarDepInterface::v_const_iterator & rhs )
   const override final {
   #ifdef NDEBUG
    auto tmp = static_cast< const InvestmentFunction::v_const_iterator * >(
								      & rhs );
    return( itr_ != tmp->itr_ );
   #else
    auto tmp = dynamic_cast< const InvestmentFunction::v_const_iterator * >(
								      & rhs );
    return( tmp ? itr_ != tmp->itr_ : true );
   #endif
   }

  private:

  VarVector::const_iterator itr_;
  };

/*--------------------------------------------------------------------------*/
 /// public enum for the int algorithmic parameters
 /** Public enum describing the different algorithmic parameters of int type
  * that InvestmentFunction has in addition to those of C05Function. The value
  * intLastParInvestmentF is provided so that the list can be easily further
  * extended by derived classes. */

 enum int_par_type_InvestmentF {

  intComputeLinearization = intLastParC05F ,
  ///< determines whether linearizations must be computed

  intOutputSolution ,
  ///< indicates whether the solution should be output
  /**< If the value for this parameter is nonzero, then part of the primal and
   * dual solutions obtained for each UCBlock for each scenario is output
   * while this InvestmentFunction is being compute()-ed. The default value of
   * this parameter is 0, which means that no solution is output. */

  intLastParInvestmentF
  ///< first allowed new int parameter for derived classes
  /**< Convenience value for easily allow derived classes to extend the set of
   * int algorithmic parameters. */

 };  // end( int_par_type_InvestmentF )

/*--------------------------------------------------------------------------*/
 /// public enum for the possible types of events
 /** This enum defines additional types of events that InvestmentFunction can
  * manage. */

 enum event_type_invest_func {
  eAtTheBeginning = e_last_event_type ,
  ///< event to be called right after compute() begins
  /**< Type of events that will be called right after compute()
   * begins. */

  e_last_event_type = eAtTheBeginning + 1
  ///< convenience value to define new events
  /**< convenience value to allow derived classes to "extend"
   * event_type_invest_func and define new class-specific events that their
   * compute() can support. */
 };  // end( event_type_invest_func )

/*--------------------------------------------------------------------------*/
 /// public enum for the string algorithmic parameters
 /** Public enum describing the different algorithmic parameters of "string"
  * type that InvestmentFunction has in addition to those of C05Function. The
  * value strLastInvestmentFPar is provided so that the list can be easily
  * further extended by derived classes. */

 enum str_par_type_InvestmentF {

  strOutputFilename = str_par_type_C05F::strLastParC05F ,
  ///< name of the file into which the variable and function values are output
  /**< This is the name of the file into which the variable and function
   * values are output (appended) every time this InvestmentFunction is
   * compute()-ed. If it is empty, then the variable and function values are
   * not output. The default value for this parameter is the empty string. */

  strOutputSolutionDirectory ,
  ///< path to the directory where the solution should be output
  /**< If the solution must be output (see #intOutputSolution) then this is
   * the path to the directory where the solution will be output. If it is
   * empty, then the solution is output to the working directory. The default
   * value of this parameter is empty. This parameter is only used by the
   * multi-replica SDDPBlock path. */

  strLastParInvestmentF
  ///< first allowed new string parameter for derived classes
  /**< Convenience value for easily allow derived classes to extend the set of
   * string algorithmic parameters. */

 };  // end( str_par_type_InvestmentF )

/**@} ----------------------------------------------------------------------*/
/*------------- CONSTRUCTING AND DESTRUCTING InvestmentFunction ------------*/
/*--------------------------------------------------------------------------*/
/** @name Constructing and destructing InvestmentFunction
 *  @{ */

 /// constructor of InvestmentFunction, possibly inputting the data
 /** Constructor of InvestmentFunction, taking possibly all the data
  * characterising the function:
  *
  * @param inner_block the only sub-Block of this InvestmentFunction,
  *        representing the problem (B) as stated in the general notes of this
  *        class.
  *
  * @param x an n-vector of pointers to ColVariable representing the x
  *        variable vector in the definition of the function. Note that the
  *        order of the variables in x is crucial, since
  *
  *            THE ORDER OF THE x VECTOR WILL DICTATE THE ORDER OF THE
  *            "ACTIVE" [Col]Variable OF THE InvestmentFunction
  *
  *        That is, get_active_var( 0 ) == x[ 0 ], get_active_var( 1 ) == x[ 1
  *        ], ...
  *
  * @param asset_indices a vector containing the indices of the assets that
  *        are subject to investment. An asset can be either a UnitBlock or a
  *        transmission line. If it is a UnitBlock, then its index is simply
  *        the index that this UnitBlock has within its UCBlock. If it is a
  *        transmission line, then its index is the index that this
  *        transmission line has within its NetworkBlock. The correspondence
  *        between \p asset_indices and \p x is positional, i.e., if \p
  *        asset_indices is not empty, \p asset_indices[ i ] is the index of
  *        the asset associated with the i-th active variable of this function
  *        (given by get_active_var( i )).
  *
  * @param asset_type a vector containing the type of each asset.

  * @param cost a vector containing the coefficients of the linear term of
  *        this function associated with investments. The correspondence
  *        between \p cost and \p x is positional, i.e., the coefficient of
  *        the i-th active variable of this function (given by get_active_var(
  *        i )) is given by \p cost[ i ].
  *
  * @param disinvestment_cost a vector containing the coefficients of the
  *        linear term of this function associated with disinvestments. The
  *        correspondence between \p disinvestment_cost and \p x is
  *        positional, i.e., the coefficient of the i-th active variable of
  *        this function (given by get_active_var( i )) is given by \p
  *        disinvestment_cost[ i ].
  *
  * @param observer a pointer to the Observer of this InvestmentFunction.
  *
  * As the && implies, \p x, \p block_indices, \p line_indices, \p cost, and
  * \p disinvestment_cost become property of the InvestmentFunction object.
  *
  * All inputs have a default (nullptr, {}, {}, {}, {}, and nullptr,
  * respectively) so that this can be used as the void constructor. */

 InvestmentFunction( Block * inner_block = nullptr , VarVector && x = {} ,
                     IndexVector && asset_indices = {} ,
                     AssetTypeVector && asset_type = {} ,
                     RealVector && cost = {} ,
                     RealVector && disinvestment_cost = {} ,
                     Observer * const observer = nullptr );

/*--------------------------------------------------------------------------*/
 /// de-serialize an InvestmentFunction out of netCDF::NcGroup
 /** The method takes a netCDF::NcGroup supposedly containing all the
  * *numerical* information required to de-serialize the InvestmentFunction,
  * i.e., the assets to invest (indices of UnitBlocks and transmission lines)
  * and linear coefficients and sets the data of this InvestmentFunction. See
  * the comments to InvestmentFunction::serialize() for the detailed
  * description of the expected format of the netCDF::NcGroup.
  *
  * Note that this method does *not* change the set of active variables, that
  * must be initialized independently either before (like, in the
  * constructor) or after a call to this method (cf. set_variable()).
  *
  * Note, however that there is a significant difference between calling
  * deserialize() before or after set_variables(). More specifically, the
  * difference is between calling the method when the current set of "active"
  * Variable is empty, or not. Indeed, in the former case the number of
  * "active" Variable is dictated by the data found in the netCDF::NcGroup;
  * calling set_variables() afterwards with a vector of different size will
  * fail. Symmetrically, if the set of "active" Variable is not empty when
  * this method is called, finding non-conforming data (number of assets to
  * invest) in the netCDF::NcGroup within this method will cause it to
  * fail. Also, note that in the former case the function is "not completely
  * initialized yet" after deserialize(), and therefore it should not be
  * passed to the Observer quite as yet.
  *
  * Usually [de]serialization is done by Block, but InvestmentFunction is a
  * complex enough object so that having its own ready-made [de]serialization
  * procedure may make sense. Besides, it *is* a Block, and therefore the
  * netCDF::NcGroup will have to contain whatever is managed by the
  * serialize() method of the base Block class in addition to the
  * InvestmentFunction-specific data. However, because this method can then
  * conceivably be called when the InvestmentFunction is attached to an
  * Observer (although it is expected to be used before that), it is also
  * necessary to specify if and how a Modification is issued.
  *
  * @param group a netCDF::NcGroup holding the data in the format described
  *        in the comments to serialize();
  *
  * @param issueMod which decides if and how the FunctionMod (with shift()
  *        == FunctionMod::NaNshift, i.e., "everything changed") is issued,
  *        as described in Observer::make_par(). */

 void deserialize( const netCDF::NcGroup & group , ModParam issueMod );

/*--------------------------------------------------------------------------*/

 /// de-serialize an InvestmentFunction out of netCDF::NcGroup
 /** This method simply calls deserialize( group , eNoMod ). Please refer to
  * the comments to that method for details. The value eNoMod is passed as
  * argument, since this method is mostly thought to be used during
  * initialization when "no one is listening". */

 void deserialize( const netCDF::NcGroup & group ) override {
  deserialize( group , eNoMod );
  }

/*--------------------------------------------------------------------------*/
 /// destructor of InvestmentFunction
 /** Destructor of InvestmentFunction. It destroys the inner Block (if any),
  * releasing its memory. If the inner Block should not be destroyed then,
  * before this InvestmentFunction is destroyed, the pointer to the inner
  * Block must be set to \c nullptr. This can be done by invoking
  * set_inner_block(), passing \c nullptr as a pointer to the new inner Block
  * and \c false to the \c destroy_previous_block parameter. */

 virtual ~InvestmentFunction();

/*--------------------------------------------------------------------------*/
 /// clear method: clears the #v_x vector
 /** Method to "clear" the InvestmentFunction: it clear() the vector
  * #v_x. This destroys the list of "active" Variable without unregistering
  * from them. Not that the InvestmentFunction would have to, but an Observer
  * using it to "implement itself" should. By not having any Variable, the
  * Observer can no longer do that. */

 void clear( void ) override { v_x.clear(); }

/**@} ----------------------------------------------------------------------*/
/*-------------------------- OTHER INITIALIZATIONS -------------------------*/
/*--------------------------------------------------------------------------*/
/** @name Other initializations
 *  @{ */

 /// set the whole set of parameters of this InvestmentFunction
 /** The extra Configuration of the given ComputeConfig (see
  * ComputeConfig::f_extra_Configuration), if not nullptr, is assumed to be of
  * type SimpleConfiguration< std::map< std::string , Configuration * > >. If
  * it is not of this type, an exception is thrown. The map in that
  * SimpleConfiguration is meant to provide pointers to a number of
  * Configuration, that should be used in different situations. The following
  * keys, and their corresponding Configuration, are considered:
  *
  * - "BlockConfig": a pointer to a BlockConfig to be applied to the inner
  *    Block of this BendersBFunction.
  *
  * - "BlockSolverConfig": a pointer to a BlockSolverConfig to be applied to
  *    the inner Block of this BendersBFunction.
  *
  * - "UnitBlockSolverConfig": a pointer to the BlockSolverConfig of the
  *    copy of a UnitBlock with no copies, which the linearization solves
  *    alone [see compute_zero_copy_linearization()]. It is applied to an
  *    AbstractBlock that holds the copy as its sub-Block, hence its Solver
  *    has to handle a Block with sub-Block (e.g., a :MILPSolver), and it
  *    has to solve the copy as the unit is solved in the inner Block (e.g.,
  *    its continuous relaxation, if that is what the inner Block is solved
  *    for). If it is not given and the inner Block is a UCBlock, the
  *    BlockSolverConfig of the inner Block is used, i.e., the unit is solved
  *    by the same Solver, with the same parameters, that solves it in the
  *    inner Block; with any other inner Block it has to be given, if a
  *    scaled UnitBlock can have no copies.
  *
  * If a key that is not any of the above is provided, an exception is
  * thrown. No pointer is kept by the InvestmentFunction, which keeps a clone
  * of what it needs, so the caller can (and is responsible to) delete any
  * pointer provided.
  *
  * If the given pointer to the ComputeConfig is nullptr, then the
  * Configuration of the InvestmentFunction is reset to its default. This
  * means that
  *
  *  (1) all parameters of the InvestmentFunction are reset to their default
  *      values;
  *
  *  (2) the inner Block (if any) is configured to its default configuration;
  *
  *  (3) the Solver of the inner Block (and their sub-Block, recursively) are
  *      unregistered and deleted.
  *
  * If the given pointer to the ComputeConfig is not nullptr but its extra
  * Configuration is nullptr, then (2) and (3) above are performed. If the
  * pointer to the BlockConfig (if provided) in the extra Configuration is
  * nullptr, then (2) above is performed. If the pointer to the
  * BlockSolverConfig (if provided) in the extra Configuration is nullptr,
  * then (3) above is performed.
  *
  * @param scfg a pointer to a ComputeConfig. */

 void set_ComputeConfig( const ComputeConfig *scfg = nullptr ) override;

/*--------------------------------------------------------------------------*/
 /// sets the set of active Variable of the InvestmentFunction
 /** Sets the set of active Variable of the InvestmentFunction. This method is
  * basically provided to work in tandem with the methods which only load the
  * "numerical data" of the InvestmentFunction, for instance,
  * deserialize(). These (if the variables have not been defined prior to
  * calling them, see below) leave the InvestmentFunction in a somewhat
  * inconsistent state whereby one knows the data but not the input Variable,
  * cue this method.
  *
  * Note that there are two distinct patterns of usage:
  *
  * - set_variables() is called *before* deserialize();
  *
  * - set_variables() is called *after* deserialize().
  *
  * In the former case, after the call to set_variables(), the
  * InvestmentFunction has active variables, but data necessary for its
  * computation is not present. In the latter case, the data is there, except
  * that the InvestmentFunction has no input Variable; the object is in a
  * not-fully-consistent defined state. Having an Observer (then, Solver)
  * dealing with a call to set_variables() would be possible by issuing a
  * FunctionModVars( ... , AddVar ), but this is avoided because this method
  * is only thought to be called during initialization where the Observer is
  * not there already, whence no "issueMod" parameter.
  *
  * @param x a n-vector of pointers to ColVariable representing the x variable
  *        vector in the definition of the function. Note that the order of
  *        the variables in x is crucial, since the correspondence with the
  *        indices of the assets and the linear coefficients (whether already
  *        provided, or to be provided later, cf. discussion above) is
  *        positional. The i-th linear coefficient of this InvestmentFunction
  *        is the coefficient of the i-th active variable in the linear term
  *        of this function. Moreover, by letting "NumUnitBlocks" and
  *        "NumLines" the number of UnitBlocks and transmission lines to
  *        invest:
  *
  *          - For each i in {0, ..., NumUnitBlocks-1}, the i-th active
  *            Variable of this InvestmentFunction represents the investment
  *            to be made on the i-th UnitBlock (among the UnitBlock that are
  *            subject to investment).
  *
  *          - For each i in {0, ..., NumLines-1}, the (NumUnitBlocks + i)-th
  *            active Variable of this InvestmentFunction represents the
  *            investment to be made on the i-th line (among the lines that
  *            are subject to investment).
  *
  *        After the call to this method, <tt>x[ 0 ] == get_active_var( 0 ),
  *        x[ 1 ] = get_active_var( 1 )</tt>, ...
  *
  * As the && implies, x become property of the InvestmentFunction object. */

 void set_variables( VarVector && x );

/*--------------------------------------------------------------------------*/
 /// set the (only) sub-Block of the InvestmentFunction
 /** This method sets the only sub-Block of the InvestmentFunction
  * (a.k.a. Block B representing problem (B) in the definition of this
  * InvestmentFunction).
  *
  * @param block the pointer to a Block satisfying the conditions stated in
  *        the definition of this InvestmentFunction.
  *
  * @param destroy_previous_block indicates whether the previous inner Block
  *        must be destroyed. The default value of this parameter is \c true,
  *        which means that the previous inner Block (if any) is destroyed and
  *        its allocated memory is released. */

 void set_inner_block( Block * block , bool destroy_previous_block = true )
 {
  if( ( v_Block.size() == 1 ) && ( block == v_Block.front() ) &&
      ( ! destroy_previous_block ) )
   return; // the given Block is already here; silently return

  // un-do the BlockSolverConfig-uration of the outgoing inner Blocks, i.e.,
  // remove the Solver that this InvestmentFunction has registered there
  unconfigure_inner_Block_Solver();

  if( destroy_previous_block )
   for( auto block : v_Block )
    delete block;

  v_Block.clear();
  v_Block.push_back( block );

  if( block )
   block->set_f_Block( this );

  send_nuclear_modification();
  }

/*--------------------------------------------------------------------------*/
 /// set the sub-Blocks of the InvestmentFunction
 /** This method sets the sub-Blocks of the InvestmentFunction as a vector of
  * (identical) Blocks. This enables the multi-replica execution path: each
  * Block is a self-contained replica of the inner problem, and scenarios
  * are distributed across replicas using OpenMP (and, if compiled with
  * USE_MPI, across MPI processes).
  *
  * @param blocks the pointers to the (identical) Blocks satisfying the
  *        conditions stated in the definition of this InvestmentFunction.
  *
  * @param destroy_previous_blocks indicates whether the previous inner
  *        Blocks must be destroyed. The default value of this parameter is
  *        \c true, which means that the previous inner Blocks (if any) are
  *        destroyed and their allocated memory is released. */

 void set_inner_blocks( std::vector< Block * > & blocks ,
                        bool destroy_previous_blocks = true ) {
  if( ( ! v_Block.empty() ) && ( blocks.size() == v_Block.size() ) &&
      ( ! destroy_previous_blocks ) ) {

   bool blocks_are_here = true;
   for( Index i = 0 ; i < blocks.size() ; ++i )
    if( blocks[ i ] != v_Block[ i ] ) {
     blocks_are_here = false;
     break;
     }

   if( blocks_are_here )
    return; // the given Blocks are already here; silently return
   }

  // un-do the BlockSolverConfig-uration of the outgoing inner Blocks, i.e.,
  // remove the Solver that this InvestmentFunction has registered there
  unconfigure_inner_Block_Solver();

  if( destroy_previous_blocks )
   for( auto block : v_Block )
    delete block;

  v_Block.clear();
  v_Block = blocks;

  for( auto block : v_Block )
   if( block )
    block->set_f_Block( this );

  send_nuclear_modification();
  }

/*--------------------------------------------------------------------------*/
 /// set a given integer (int) numerical parameter
 /** Set a given integer (int) numerical parameter. InvestmentFunction takes
  * care of the following parameters:
  *
  * - #intGPMaxSz: This parameter specifies the maximum number of
  *                linearizations that can be stored in the global pool. The
  *                default value for this parameter is defined by the
  *                C05Function.
  *
  * - #intComputeLinearization: This parameter indicates whether
  *                             linearizations must be computed. The default
  *                             value is 1, which means that linearizations
  *                             are computed.
  *
  * - #intOutputSolution: This parameter indicates whether the solution of
  *                       each UCBlock must be output. If the value for this
  *                       parameter is nonzero, then part of the primal and
  *                       dual solutions obtained for each UCBlock for each
  *                       scenario is output while this InvestmentFunction is
  *                       being compute()-ed. The default value of this
  *                       parameter is 0, which means that no solution is
  *                       output.
  *
  * Any other parameter is handled by the C05Function.
  *
  * @param par The parameter to be set.
  *
  * @param value The value of the parameter. */

 void set_par( idx_type par , int value ) override;

/*--------------------------------------------------------------------------*/
 /// set a given float (double) numerical parameter
 /** Set a given float (double) numerical parameter. InvestmentFunction takes
  * care of the following parameters. Any other parameter is handled by the
  * C05Function.
  *
  * - #dblAAccMlt
  *
  * @param par The parameter to be set.
  *
  * @param value The value of the parameter. */

 void set_par( idx_type par , double value ) override
 {
  if( par == dblAAccMlt ) {
   AAccMlt = value;
   return;
   }

  C05Function::set_par( par , value );
  }

/*--------------------------------------------------------------------------*/
 /// set a given string parameter
 /** Set a given string parameter. InvestmentFunction takes care of the
  * following parameters. Any other parameter is handled by the C05Function.
  *
  * - #strOutputFilename
  *
  * - #strOutputSolutionDirectory
  *
  * @param par The parameter to be set.
  *
  * @param value The value of the parameter. */

 void set_par( idx_type par , std::string && value ) override
 {
  switch( par ) {
   case( strOutputFilename ):
    f_output_filename = std::move( value );
    return;
   case( strOutputSolutionDirectory ):
    f_output_solution_directory = std::move( value );
    return;
   }

  C05Function::set_par( par , value );
  }

/** @} ---------------------------------------------------------------------*/
/*------------------- METHODS FOR HANDLING THE PARAMETERS ------------------*/
/*--------------------------------------------------------------------------*/
/** @name Handling the parameters of the InvestmentFunction
 *  @{ */

 /// get the number of int parameters
 /** Get the number of int parameters.
  *
  * @return The number of int parameters.
  */

 [[nodiscard]] idx_type get_num_int_par( void ) const override {
  return( intLastParInvestmentF );
  }

/*--------------------------------------------------------------------------*/
 /// get the number of string parameters
 /** Get the number of string parameters.
  *
  * @return The number of string parameters.
  */

 idx_type get_num_str_par( void ) const override {
  return( idx_type( strLastParInvestmentF ) );
  }

/*--------------------------------------------------------------------------*/
 /// get a specific integer (int) numerical parameter
 /** Get a specific integer (int) numerical parameter. InvestmentFunction
  * takes care of the following parameters:
  *
  * - #intGPMaxSz
  *
  * - #intComputeLinearization
  *
  * - #intOutputSolution
  *
  * Any other parameter is handled by the C05Function.
  *
  * @param par The parameter whose value is desired.
  *
  * @return The value of the required parameter. */

 [[nodiscard]] int get_int_par( idx_type par ) const override {
  switch( par ) {
   case( intGPMaxSz ):              return( global_pool.size() );
   case( intComputeLinearization ): return( f_compute_linearization );
   case( intOutputSolution ):       return( f_output_solution );
   }

  return( C05Function::get_int_par( par ) );
  }

/*--------------------------------------------------------------------------*/
 /// get a specific float (double) numerical parameter
 /** Get a specific float (double) numerical parameter. InvestmentFunction
  * takes care of the following parameters:
  *
  * - #dblAAccMlt
  *
  * Any other parameter is handled by the C05Function.
  *
  * @param par The parameter whose value is desired.
  *
  * @return The value of the required parameter. */

 [[nodiscard]] double get_dbl_par( idx_type par ) const override
 {
  if( par == dblAAccMlt )
   return( AAccMlt );

  return( C05Function::get_dbl_par( par ) );
  }

/*--------------------------------------------------------------------------*/
 /// get a specific string numerical parameter
 /** Get a specific string numerical parameter. Please see the
  * #str_par_type_InvestmentF and #str_par_type_C05F enumerations for a
  * detailed explanation of the possible parameters.
  *
  * @param par The parameter whose value is desired.
  *
  * @return The value of the given parameter. */

 const std::string & get_str_par( const idx_type par ) const override
 {
  switch( par ) {
   case( strOutputFilename ):          return( f_output_filename );
   case( strOutputSolutionDirectory ): return( f_output_solution_directory );
   }

  return( C05Function::get_str_par( par ) );
  }

/*--------------------------------------------------------------------------*/

 [[nodiscard]] int get_dflt_int_par( idx_type par ) const override
 {
  if( par == intComputeLinearization )
   return( 1 );

  if( par == intOutputSolution )
   return( 0 );

  return( C05Function::get_dflt_int_par( par ) );
  }

/*--------------------------------------------------------------------------*/
 /// get the default value of a string parameter
 /** Get the default value of the string parameter with given index. Please
  * see #str_par_type_InvestmentF and #str_par_type_C05F enumerations for a
  * detailed explanation of the possible parameters.
  *
  * @param par The parameter whose default value is desired.
  *
  * @return The default value of the given parameter. */

 [[nodiscard]] const std::string & get_dflt_str_par( const idx_type par )
  const override
 {
  static const std::vector< std::string > default_values = { "" };

  if( par >= str_par_type_C05F::strLastParC05F &&
      par < str_par_type_InvestmentF::strLastParInvestmentF )
   return( default_values[ par - str_par_type_C05F::strLastParC05F ] );

  return( C05Function::get_dflt_str_par( par ) );
  }

/*--------------------------------------------------------------------------*/

 [[nodiscard]] idx_type int_par_str2idx( const std::string & name )
  const override
 {
  if( name == "intComputeLinearization" )
   return( intComputeLinearization );

  if( name == "intOutputSolution" )
   return( intOutputSolution );

  return( C05Function::int_par_str2idx( name ) );
  }

/*--------------------------------------------------------------------------*/
 /// returns the index of the string parameter with given string name
 /** This method takes a string, which is assumed to be the name of a string
  * parameter, and returns its index, i.e., the integer value that can be
  * used in [set/get]_par() to set/get it.
  *
  * @param name The name of the parameter.
  *
  * @return The index of the parameter with the given \p name. */

 [[nodiscard]] idx_type str_par_str2idx( const std::string & name )
  const override
 {
  if( name == "strOutputFilename" )
   return( strOutputFilename );

  if( name == "strOutputSolutionDirectory" )
   return( strOutputSolutionDirectory );

  return( C05Function::str_par_str2idx( name ) );
  }

/*--------------------------------------------------------------------------*/

 [[nodiscard]] const std::string & int_par_idx2str( idx_type idx )
  const override
 {
  static const std::vector< std::string > pars = { "intComputeLinearization" ,
                                                   "intOutputSolution" };

  if( ( idx >= intComputeLinearization ) && ( idx < intLastParInvestmentF ) )
   return( pars[ idx - intComputeLinearization ] );

  return( C05Function::int_par_idx2str( idx ) );
  }

/*--------------------------------------------------------------------------*/

 /// returns the string name of the string parameter with given index
 /** This method takes a string parameter index, i.e., the integer value that
  * can be used in [set/get]_par() [see above] to set/get it, and returns its
  * "string name".
  *
  * @param idx The index of the parameter.
  *
  * @return The name of the parameter with the given index \p idx. */

 const std::string & str_par_idx2str( const idx_type idx ) const override
 {
  static const std::vector< std::string > parameter_names = {
                                                "strOutputFilename" ,
                                                "strOutputSolutionDirectory" };
  if( ( idx >= str_par_type_C05F::strLastParC05F ) &&
      ( idx < str_par_type_InvestmentF::strLastParInvestmentF ) )
   return( parameter_names[ idx - str_par_type_C05F::strLastParC05F ] );

  return( C05Function::str_par_idx2str( idx ) );
  }

/** @} ---------------------------------------------------------------------*/
/*-------- METHODS FOR HANDLING THE State OF THE InvestmentFunction --------*/
/*--------------------------------------------------------------------------*/
/** @name Handling the State of the InvestmentFunction
 */

 State * get_State( void ) const override;

/*--------------------------------------------------------------------------*/

 void put_State( const State & state ) override;

/*--------------------------------------------------------------------------*/

 void put_State( State && state ) override;

/*--------------------------------------------------------------------------*/

 void serialize_State( netCDF::NcGroup & group ,
		       const std::string & sub_group_name = "" )
  const override;

/*--------------------------------------------------------------------------*/
/*----------------- METHODS FOR MANAGING THE "IDENTITY" --------------------*/
/*--------------------------------------------------------------------------*/
/** @name Managing the "identity" of the InvestmentFunction
 *
 * Actually implement the methods of ThinComputeInterface relative to
 * temporarily changing the "identity" of the InvestmentFunction.
 *
 *  @{ */

 /// set the "identity" of the InvestmentFunction
 /** Actually implement the ThinComputeInterface::set_id() by setting the f_id
  * member of the InvestmentFunction class, which is initialized with "this"
  * in the constructor. InvestmentFunction always uses f_id to try to "lock
  * and own" the Block. This method allows to change f_id ("lend another
  * identity"); when called with nullptr argument, the id() is reset to the
  * default "this". Also, if the inner Block is set and has registered Solver,
  * their identity is also set (or reset) in anticipation that they also may
  * have to lock() the inner Block during their line of work. */

 void set_id( void * id = nullptr ) override
 {
  if( f_id == id )  // nothing to do
   return;          // silently (and cowardly) return

  f_id = id ? id : this;

  // propagate downwards the id change
  for( auto block : v_Block )
   for( auto s : block->get_registered_solvers() )
    s->set_id( id );
  }

/**@} ----------------------------------------------------------------------*/
/*---------------------- METHODS FOR EVENTS HANDLING -----------------------*/
/*--------------------------------------------------------------------------*/
/** @name Set event handlers
 *
 *  InvestmentFunction manages the following events:
 *
 * - eAtTheBeginning, called just after compute() begins;
 *
 * - eBeforeTermination, called just before compute() terminates;
 *
 * Events have to be set with set_event_handler() for them to be called.
 * @{ */

 /// register a new event handler, returning its id
 /** The new event handler is added at the back of v_events[ type ]. As the &&
  * tells, the event handler becomes property of the InvestmentFunction, which
  * is completely OK if, as one expects, it is defined via a lambda
  * function. The method returns a unique id for the handler, which can (and
  * must) be later used to remove the handler before it becomes invalid. Note
  * that the handler is type-specific, i.e., two event handlers of different
  * types can have the same id; in other words, the "real" id is the pair (
  * type , id ). An exception is thrown if the InvestmentFunction is not
  * capable of handling this type or event for whatever reason, among which
  * that it has exhausted the available maximum number of event handlers slots
  * for the given type. */

 EventID set_event_handler( int type , EventHandler && event ) override
 {
  if( ( type != eBeforeTermination ) && ( type != eAtTheBeginning ) )
   throw( std::invalid_argument( "InvestmentFunction: unsupported event "
                                 "type " + std::to_string( type ) ) );

  if( v_events[ type ].size() > std::numeric_limits< EventID >::max() )
   throw( std::invalid_argument( "InvestmentFunction: too many event handlers"
				 " for type" + std::to_string( type ) ) );

  EventID id = v_events[ type ].size();
  v_events[ type ].push_back( std::move( event ) );

  return( id );
  }

/*--------------------------------------------------------------------------*/
 /// unregister an existing event handler
 /** Removes the event handler with the given id from the list of those
  * registered for the given type. If there is no event handler with the given
  * id for the given type, exception will be thrown. */

 void reset_event_handler( int type , EventID id ) override;

/*--------------------------------------------------------------------------*/
 /// returns the maximum number of event types of the InvestmentFunction

 [[nodiscard]] virtual EventID max_event_number() const {
  return( e_last_event_type );
  }

/**@} ----------------------------------------------------------------------*/
/*---- METHODS FOR HANDLING "ACTIVE" Variable IN THE InvestmentFunction ----*/
/*--------------------------------------------------------------------------*/
/** @name Methods for handling the set of "active" Variable in the
 * InvestmentFunction; this is the actual concrete implementation exploiting
 * the vector v_x of pointers.
 * @{ */

 Index get_num_active_var() const override final {
  return( v_x.size() );
  }

/*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

 Index is_active( const Variable * const var ) const override final
 {
  auto idx = std::find( v_x.begin() , v_x.end() , var );
  if( idx == v_x.end() )
   return( Inf< Index >() );
  else
   return( std::distance( v_x.begin() , idx ) );
  }

/*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

 void map_active( c_Vec_p_Var & vars , Subset & map , bool ordered = false )
  const override final;

/*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

 Variable * get_active_var( Index i ) const override final {
  return( v_x[ i ] );
  }

/*--------------------------------------------------------------------------*/

 v_iterator * v_begin( void ) override final {
  return( new InvestmentFunction::v_iterator( v_x.begin() ) );
  }

/*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

 v_const_iterator * v_begin( void ) const override final {
  return( new InvestmentFunction::v_const_iterator( v_x.begin() ) );
  }

/*--------------------------------------------------------------------------*/

 v_iterator * v_end( void ) override final {
  return( new InvestmentFunction::v_iterator( v_x.end() ) );
  }

/*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

 v_const_iterator * v_end( void ) const override final {
  return( new InvestmentFunction::v_const_iterator( v_x.end() ) );
  }

/**@} ----------------------------------------------------------------------*/
/*------------- METHODS FOR MODIFYING THE InvestmentFunction ---------------*/
/*--------------------------------------------------------------------------*/
/** @name Methods for modifying the InvestmentFunction
 *  @{ */

/*--------------------------------------------------------------------------*/
 /// remove the i-th active Variable
 /** This method removes the active Variable whose index is \p i.
  *
  * @param i the index of the Variable to be removed. It must be an integer
  *        between 0 and get_num_active_var() - 1.
  *
  * @param issueMod decides if and how the C05FunctionModVarsRngd (since a
  *        InvestmentFunction is strongly quasi-additive, and with
  *        shift() == 0 as expected) is issued, as described in
  *        Observer::make_par(). */

 void remove_variable( Index i , ModParam issueMod = eModBlck )
  override final;

/*--------------------------------------------------------------------------*/
 /// remove a range of active Variable
 /** This method removes a range of "active" Variable.
  *
  * @param range contains the indices of the Variable to be deleted
  *        (hence, range.second <= get_num_active_var());
  *
  * @param issueMod decides if and how the C05FunctionModVarsRngd (since a
  *        InvestmentFunction is strongly quasi-additive, and with
  *        shift() == 0 as expected) is issued, as described in
  *        Observer::make_par(). */

 void remove_variables( Range range , ModParam issueMod = eModBlck )
  override final;

/*--------------------------------------------------------------------------*/
 /// remove a subset of Variable
 /** This method removes all the Variable in the given set of indices. If \p
  * indices is empty, all Variable are removed.
  *
  * @param indices a Subset & containing the indices of the Variable to be
  *        removed, i.e., integers between 0 and get_num_active_var() - 1. If
  *        \p indices is empty, all Variable are removed.
  *
  * @param ordered a bool indicating if \p indices is already ordered in
  *        increasing sense (otherwise this is done inside the method, which
  *        is why \p indices is not const).
  *
  * @param issueMod decides if and how the C05FunctionModVars (with f_shift ==
  *        0, since an InvestmentFunction is strongly quasi-additive) is
  *        issued, as described in Observer::make_par(). */

 void remove_variables( Subset && indices , bool ordered = false ,
                        ModParam issueMod = eModBlck ) override final;

/*--------------------------------------------------------------------------*/
 /// informs the InvestmentFunction whether the bounds were reformulated
 /** This functions informs the InvestmentFunction whether the bounds on the
  * active variables have been reformulated.
  *
  * The active variables of this InvestmentFunction may have "natural" lower
  * and upper bounds in the model in which they are defined. That is, for each
  * active variable \f$ x \f$, there may be \f$ l \f$ and \f$ u \f$ such
  * that \f$ l \le x \le u \f$. These natural bounds, however, may have been
  * reformulated such that \f$ x \f$ becomes a nonnegative variable
  * satisfying \f$ 0 \le x \le u - l \f$, if the lower bound \f$ l \f$ is
  * finite. This function informs the
  * InvestmentFunction whether the natural bounds on the active variables have
  * been reformulated.
  *
  * @param reformulated If true, this means that the bounds on the active
  *        variables have been reformulated. Otherwise, the bounds are the
  *        "natural" ones. */

 void reformulated_bounds( bool reformulated ) {
  f_reformulated_bounds = reformulated;
  }

/*--------------------------------------------------------------------------*/

 void set_num_sub_blocks_per_stage( Index n ) {
  f_num_sub_blocks_per_stage = n;
  }

/*--------------------------------------------------------------------------*/
 /// set the number of (replica) sub-Blocks of the InvestmentFunction
 /** Sets the number of identical sub-Blocks that this InvestmentFunction
  * holds. This is the multi-replica analogue of
  * #set_num_sub_blocks_per_stage(), and is used by the parallel execution
  * of the SDDPBlock: the InvestmentFunction holds \p n identical
  * SDDPBlocks as inner Blocks (one per OpenMP thread / MPI process), and
  * scenarios are partitioned across replicas. Setting this value when an
  * inner Block is already present is illegal: if non-null Blocks are
  * registered, this asserts and the call has no effect.
  *
  * Note: #set_num_sub_blocks_per_stage() and #set_number_sub_blocks() are
  * NOT aliases; they govern two orthogonal parallelism levels (multi
  * sub-Block per stage within a single inner SDDPBlock vs. multiple
  * inner SDDPBlock replicas). */

 void set_number_sub_blocks( Index n ) {
  assert( v_Block.empty() ||
          std::all_of( v_Block.cbegin() , v_Block.cend() ,
                       []( Block * b ) { return( b == nullptr ); } ) );
  f_num_sub_blocks = n;
  }

/** @} ---------------------------------------------------------------------*/
/*----------- METHODS FOR Saving THE DATA OF THE InvestmentFunction --------*/
/*--------------------------------------------------------------------------*/
/** @name Saving the data of the InvestmentFunction
 *  @{ */

 /// serialize an InvestmentFunction into a netCDF::NcGroup
 /** Serialize an InvestmentFunction into a netCDF::NcGroup. Note that,
  * InvestmentFunction being both a Function and a Block, the netCDF::NcGroup
  * will have to have the "standard format of a :Block", meaning whatever is
  * managed by the serialize() method of the base Block class, plus the
  * InvestmentFunction-specific data with the following format:
  *
  * - The attribute "ReplicateBatteryUnits", of type netCDF::Int(), which
  *   indicates that investment in a battery unit is made by replicating the
  *   unit, i.e., by considering multiple identical units of that unit. This
  *   attribute is optional. If it is not provided or its value is zero, then
  *   investing in a battery unit means scaling its minimum and maximum power
  *   and storage levels. Otherwise, the battery units are replicated.
  *
  * - The attribute "ReplicateIntermittentUnits", of type netCDF::Int(), which
  *   indicates that investment in an intermittent unit is made by replicating
  *   the unit, i.e., by considering multiple identical units of that
  *   unit. This attribute is optional. If it is not provided or its value is
  *   zero, then investing in an intermittent unit means scaling its minimum
  *   and maximum power. Otherwise, the intermittent units are replicated.
  *
  * - The dimension "NumAssets" containing the number of assets that are
  *   subject to investment. This dimension is optional. If it is not
  *   provided, then it is assumed that NumAssets = 0.
  *
  * - The one-dimensional variable "Assets", of type netCDF::Uint() and
  *   indexed over "NumAssets", containing the indices of the assets that are
  *   subject to investment. An asset can be a UnitBlock, the converter of a
  *   BatteryUnitBlock or a transmission line. If it is a UnitBlock or the
  *   converter of a BatteryUnitBlock, then its index is simply the index
  *   that this UnitBlock has within its UCBlock. If it is a transmission
  *   line, then its index is the index that this transmission line has
  *   within its NetworkBlock.
  *
  * - The one-dimensional variable "AssetType", of type netCDF::Uint(), which
  *   is either a scalar or indexed over "NumAssets", indicating the type of
  *   the i-th asset. For a UnitBlock, the type is 0, for a transmission
  *   line, the type is 1, and for the converter of a BatteryUnitBlock, the
  *   type is 2. A BatteryUnitBlock of type 0 whose converter is not of type
  *   2 is resized as a whole by its kappa, converter included, while if the
  *   converter is also an asset (the same index with type 2) the asset of
  *   type 0 is the storage alone and that of type 2 the converter alone,
  *   each with its own cost and bounds [see
  *   BatteryUnitBlock::set_converter_kappa()]; type 2 is refused if
  *   "ReplicateBatteryUnits" is nonzero. If it is a scalar, then we assume
  *   that AssetType[i] = AssetType[0] for all i in {0, ..., NumAssets - 1}.
  *   This variable is optional. If it is not provided, then we assume that
  *   AssetType[i] = 0 for each i in {0, ..., NumAssets - 1}.
  *
  * - The variable "LowerBound", of type netCDF::NcDouble(), which is either a
  *   scalar or indexed over "NumAssets". If it is a scalar, then we assume
  *   that LowerBound[i] = LowerBound[0] for all i in {0, ..., NumAssets -
  *   1}. The i-th element of this vector provides the lower bound on the i-th
  *   ColVariable of this InvestmentBlock. This variable is optional. If it is
  *   not provided, then we assume that LowerBound[i] = 0 for all i in {0,
  *   ..., NumAssets - 1}.
  *
  * - The variable "InstalledQuantity", of type netCDF::NcDouble(), which is
  *   either a scalar or indexed over "NumAssets". If it is a scalar, then we
  *   assume that InstalledQuantity[i] = InstalledQuantity[0] for all i in {0,
  *   ..., NumAssets - 1}. The i-th element of this vector provides the amount
  *   of the i-th asset that is currently installed in the system and,
  *   therefore, that is not subject to investment costs. This variable is
  *   optional. If it is not provided, then InstalledQuantity[i] = 0 for all
  *   i in {0, ..., NumAssets - 1}.
  *
  * Let \f$ x_i \f$ be the value of the i-th active Variable of this
  * InvestmentFunction (plus its lower bound, if the bounds are reformulated
  * [see reformulated_bounds()]) and \f$ \bar x_i \f$ =
  * InstalledQuantity[i]. If \f$ x_i > \bar x_i \f$, then the i-th asset is
  * subject to investment, and if \f$ x_i < \bar x_i \f$, to disinvestment.
  * Because the cost of investment is typically different from the cost of
  * disinvestment, the fixed (dis)investment cost of the i-th asset is
  * \f[
  *   \begin{cases}
  *     c_i \, ( x_i - \bar x_i ) & \text{if } x_i > \bar x_i \; , \\
  *     d_i \, ( \bar x_i - x_i ) & \text{if } x_i \le \bar x_i \; ,
  *   \end{cases}
  * \f]
  * with \f$ c_i \f$ = Cost[i] and \f$ d_i \f$ = DisinvestmentCost[i] [see
  * (1) in the class comments].
  *
  * - The one-dimensional variable "Cost", of type netCDF::NcDouble(), which
  *   is either a scalar or indexed over "NumAssets", containing the fixed
  *   (linear) cost of investing in one unit of each asset. This variable thus
  *   defines the coefficients "c" (see above) of the linear term of the
  *   function represented by this InvestmentFunction. If it is a scalar, then
  *   we assume that Cost[i] = Cost[0] for all i in {0, ..., NumAssets -
  *   1}. The i-th element of this vector is associated with the i-th asset
  *   and, therefore, with the i-th active ColVariable of this
  *   InvestmentFunction. This variable is optional. If it is not provided
  *   then all investment costs are considered to be zero.
  *
  * - The one-dimensional variable "DisinvestmentCost", of type
  *   netCDF::NcDouble(), which is either a scalar or indexed over
  *   "NumAssets", containing the fixed (linear) cost of disinvesting in one
  *   unit of each asset. This variable thus defines the coefficients "d" (see
  *   above) of the linear term of the function represented by this
  *   InvestmentFunction. If it is a scalar, then we assume that
  *   DisinvestmentCost[i] = DisinvestmentCost[0] for all i in {0, ...,
  *   NumAssets - 1}. The i-th element of this vector is associated with the
  *   i-th asset and, therefore, with the i-th active ColVariable of this
  *   InvestmentFunction. This variable is optional. If it is not provided
  *   then all disinvestment costs are considered to be zero.
  *
  * - The group "InnerBlock", containing the description of the inner Block.
 */

 void serialize( netCDF::NcGroup & group ) const override;

/**@} ----------------------------------------------------------------------*/
/*------- METHODS DESCRIBING THE BEHAVIOR OF THE InvestmentFunction --------*/
/*--------------------------------------------------------------------------*/
/** @name Methods describing the behavior of the InvestmentFunction
 *  @{ */

 /// compute the InvestmentFunction

 int compute( bool changedvars = true ) override;

/*--------------------------------------------------------------------------*/
 /// returns the value of the InvestmentFunction
 /** Returns the value \f$ \mathcal{F}( x ) \f$ in (1) of the class comments
  * computed by the last call to compute(), i.e., the cost of the investment
  * plus the operational cost \f$ Q( x ) \f$, which is the value that the
  * Solver of the inner Block gives for its solution [see
  * Solver::get_var_value()], or the mean of the values of the simulations with
  * an SDDPBlock; whether it is an optimal value, a lower bound or an estimate
  * depends on that Solver [see the class comments]. If compute() has not been
  * called yet, or the last call found the investment outside the domain (a
  * linear constraint of the InvestmentBlock violated, or the inner Block
  * infeasible), or it found no solution, the value is \f$ +\infty \f$ if the
  * Objective of the inner Block is a minimization one, and \f$ -\infty \f$
  * otherwise. */

 FunctionValue get_value( void ) override;

/*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/
 /// returns a lower estimate of the InvestmentFunction
 /** This method simply returns get_value(). */

 FunctionValue get_lower_estimate( void ) override {
  return( get_value() );
 }

/*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/
 /// returns an upper estimate of the InvestmentFunction
 /** This method simply returns get_value(). */

 FunctionValue get_upper_estimate( void ) override {
  return( get_value() );
  }

/*--------------------------------------------------------------------------*/
 /// returns the "constant term" of the InvestmentFunction

 FunctionValue get_constant_term( void ) const override;

/*--------------------------------------------------------------------------*/
 /// returns true only if this InvestmentFunction is convex
 /** Returns true if this InvestmentFunction has an inner Block, the sense of
  * the Objective of the inner Block is not maximization (an inner Block
  * whose Objective is not there yet counts as a minimization one), and
  * \f$ c_i + d_i \ge 0 \f$ for every asset \f$ i \f$, i.e., the cost of the
  * investment in (1) of the class comments is convex; false otherwise. The
  * convexity of the operational cost \f$ Q \f$ is also needed, which holds
  * if the inner Block is solved as a convex problem (e.g., its continuous
  * relaxation, or its Lagrangian Dual) and cannot be checked here [see the
  * class comments]. */

 bool is_convex( void ) override;

/*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/
 /// returns true only if this InvestmentFunction is concave
 /** Returns true if this InvestmentFunction has an inner Block, the sense of
  * the Objective of the inner Block is maximization, and
  * \f$ c_i + d_i \le 0 \f$ for every asset \f$ i \f$, i.e., the cost of the
  * investment in (1) of the class comments is concave; false otherwise. As
  * for is_convex(), the concavity of the operational cost cannot be checked
  * here. */

 bool is_concave( void ) override;

/*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/
 /// returns true only if this InvestmentFunction is linear
 /** Method that returns true only if this InvestmentFunction is linear. In
  * particular (and probably very rare) cases, this Function could be
  * linear. We do not attempt to find this out and this method simply returns
  * \c false. */

 bool is_linear( void ) override { return( false ); }

/*--------------------------------------------------------------------------*/
 /// tells whether a linearization is available

 bool has_linearization( bool diagonal = true ) override final;

/*--------------------------------------------------------------------------*/
 /// compute a new linearization for this InvestmentFunction

 bool compute_new_linearization( bool diagonal = true ) override;

/*--------------------------------------------------------------------------*/
 /// store a linearization in the global pool

 void store_linearization( Index name , ModParam issueMod = eModBlck )
  override final;

/*--------------------------------------------------------------------------*/

 bool is_linearization_there( Index name ) const override final {
  return( global_pool.is_linearization_there( name ) );
  }

/*--------------------------------------------------------------------------*/

 bool is_linearization_vertical( Index name ) const override final {
  return( global_pool.is_linearization_vertical( name ) );
  }

/*--------------------------------------------------------------------------*/
 /// stores a combination of the given linearizations
 /** This method creates a combination of the given set of linearizations,
  * with the given coefficients, and stores it into the global pool of
  * linearizations with the given name.
  *
  * InvestmentFunction can produce two types of linearizations: diagonal and
  * vertical ones. For a combination of linearizations to be valid, it must
  * satisfy one of the following two conditions:
  *
  * -# It is a combination involving only vertical linearizations, and each
  *    coefficient (multiplier) must be nonnegative (actually, greater than or
  *    equal to - #dblAAccMlt).
  *
  * -# It is a combination involving at least one diagonal linearization, each
  *    coefficient (multiplier) must be nonnegative (actually, greater than or
  *    equal to - #dblAAccMlt), and the sum of the coefficients of the
  *    diagonal linearizations must be approximately equal to 1:
  *
  *    \f$ | 1 - \sum_{ j \in D } \lambda_j | \le K \epsilon \f$, where
  *    \f$ \lambda_j \f$ are the coefficients, \f$ D \f$ is the set of the
  *    diagonal linearizations being combined, \f$ K \f$ is the number of
  *    linearizations being combined and \f$ \epsilon \f$ is #dblAAccMlt.
  *
  * In the first case, the resulting linearization is a vertical one, while in
  * the second case it is a diagonal linearization. If none of the above two
  * conditions are met, an exception is thrown. */

 void store_combination_of_linearizations(
			   c_LinearCombination & coefficients , Index name ,
			   ModParam issueMod = eModBlck ) override final;

/*--------------------------------------------------------------------------*/
 /// specify which linearization is "the important one"

 void set_important_linearization( LinearCombination && coefficients )
  override final {
  global_pool.set_important_linearization( std::move( coefficients ) );
  }

/*--------------------------------------------------------------------------*/
 /// return the combination used to form "the important linearization"

 c_LinearCombination & get_important_linearization_coefficients( void )
  const override final {
  return( global_pool.get_important_linearization_coefficients() );
  }

/*--------------------------------------------------------------------------*/
 /// delete the given linearization from the global pool of linearizations

 void delete_linearization( Index name ,
                            ModParam issueMod = eModBlck ) override final;

/*--------------------------------------------------------------------------*/

 void delete_linearizations( Subset && which , bool ordered = true ,
                             ModParam issueMod = eModBlck ) override final;

/*--------------------------------------------------------------------------*/

 void get_linearization_coefficients( FunctionValue * g ,
			 Range range = std::make_pair( 0 , Inf< Index >() ) ,
				      Index name = Inf< Index >() ) override;

/*--------------------------------------------------------------------------*/

 void get_linearization_coefficients( SparseVector & g ,
		         Range range = std::make_pair( 0 , Inf< Index >() ) ,
				      Index name = Inf< Index >() ) override;

/*--------------------------------------------------------------------------*/

 void get_linearization_coefficients( FunctionValue * g , c_Subset & subset ,
				      bool ordered = false ,
				      Index name = Inf< Index >() ) override;

/*--------------------------------------------------------------------------*/

 void get_linearization_coefficients( SparseVector & g , c_Subset & subset ,
				      bool ordered = false ,
				      Index name = Inf< Index >() ) override;

/*--------------------------------------------------------------------------*/
 /// return the constant term of a linearization

 FunctionValue get_linearization_constant( Index name = Inf< Index >() )
  override final;

/*--------------------------------------------------------------------------*/
 /// returns a pointer to the Solver attached to the i-th sub-Block (if any)
 /** This method returns a pointer to the Solver attached to the i-th
  * sub-Block of this InvestmentFunction. The template parameter \p T
  * indicates the type of Solver whose pointer will be returned; its default
  * value is Solver. If
  *
  * - \p i is not an index corresponding to a sub-Block; or
  *
  * - the i-th sub-Block of this InvestmentFunction does not have a Solver
  *   attached to it; or
  *
  * - the Solver attached to the i-th sub-Block is not or does not derive from
  *   \p T
  *
  * then a nullptr is returned. Otherwise, a pointer of type \p T is
  * returned. */

 template< class T = Solver >
 inline T * get_solver( Index i ) const {
  if( v_Block.empty() )
   return( nullptr );

  if( v_Block.size() == 1 ) {
   // single-Block mode: get the i-th solver registered on the single
   // inner Block
   if( i >= v_Block.front()->get_registered_solvers().size() )
    return( nullptr );
   return( dynamic_cast< T * >(
              * std::next( v_Block.front()->get_registered_solvers().begin() ,
                           i ) ) );
   }

  // multi-replica mode: get the first solver registered on the i-th
  // replica Block
  if( i >= v_Block.size() )
   return( nullptr );

  if( v_Block[ i ]->get_registered_solvers().empty() )
   return( nullptr );

  return( dynamic_cast< T * >(
            v_Block[ i ]->get_registered_solvers().front() ) );
  }

/**@} ----------------------------------------------------------------------*/
/*-------- METHODS FOR READING THE DATA OF THE InvestmentFunction ----------*/
/*--------------------------------------------------------------------------*/
/** @name Reading the data of the InvestmentFunction
 * @{ */

 /// returns the indices of the assets that are subject to investment
 const std::vector< Index > & get_asset_indices( void ) const {
  return( v_asset_indices );
  }

/*--------------------------------------------------------------------------*/
 /// returns the types of the assets that are subject to investment

 const std::vector< AssetType > & get_asset_type( void ) const {
  return( v_asset_type );
  }

/*--------------------------------------------------------------------------*/
 /// returns the amount of the given asset currently installed in the system
 /** This function returns the amount of the given \p asset that is currently
  * installed in the system and, therefore, that is not subject to investment
  * costs.
  *
  * @param asset The index of an asset subject to investment.
  *
  * @return The amount of the given \p asset currently installed in the
  *         system. */

 double get_installed_quantity( Index asset ) const
 {
  if( v_installed_quantity.empty() )
   return( 0 );

  assert( asset < v_installed_quantity.size() );
  return( v_installed_quantity[ asset ] );
  }

/*--------------------------------------------------------------------------*/
 /// returns a pointer to the i-th SDDPBlock replica (if any)
 /** This function returns a pointer to the i-th SDDPBlock replica of this
  * InvestmentFunction. If the i-th sub-Block is not an SDDPBlock or \p i is
  * out of range, this returns nullptr.
  *
  * @param i The index of a sub-Block of this InvestmentFunction.
  *
  * @return A pointer to the i-th SDDPBlock of this InvestmentFunction, or
  *         nullptr if not available. */

 SDDPBlock * get_sddp_block( Index i ) const;

/** @} ---------------------------------------------------------------------*/
/*-------------------- Methods for handling Modification -------------------*/
/*--------------------------------------------------------------------------*/
/** @name Methods for handling Modification
 *  @{ */

 void add_Modification( sp_Mod mod , Observer::ChnlName chnl = 0 ) override;

/**@} ----------------------------------------------------------------------*/
/*-------------------- PROTECTED PART OF THE CLASS -------------------------*/
/*--------------------------------------------------------------------------*/

 protected:

/*--------------------------------------------------------------------------*/
/*--------------------------- PROTECTED METHODS ----------------------------*/
/*--------------------------------------------------------------------------*/
/** @name Protected methods for printing
    @{ */

 /// print information about the InvestmentFunction on an ostream
 /** Protected method intended to print information about the
  * InvestmentFunction; it is virtual so that derived classes can print their
  * specific information in the format they choose. */

 void print( std::ostream &output ) override {
  output << "InvestmentFunction [" << this << "]"
         << " with " << get_num_active_var() << " active variables";
  }

/*--------------------------------------------------------------------------*/
 /// load the InvestmentFunction out of an input stream
 /** This method loads the InvestmentFunction out of an input stream. */

 void load( std::istream &input , char frmt = 0 ) override final;

/**@} ----------------------------------------------------------------------*/
/*--------------------------- PROTECTED FIELDS  ----------------------------*/
/*--------------------------------------------------------------------------*/

 VarVector v_x;  ///< the pointers to the active variables x

 std::vector< BlockSolverConfig * > v_BSC;
 ///< the clear()-ed BlockSolverConfig that configured each inner Block
 /**< For each (replica) inner Block, the clone of the BlockSolverConfig that
  * has actually been apply()-ed to it, kept clear()-ed: apply()-ing it
  * removes all and only the Solver that it has registered there [see
  * BlockSolverConfig::apply()], which is how the configuration is un-done
  * when the inner Blocks are released, replaced or destroyed. A clone per
  * Block is necessary because the same BlockSolverConfig is apply()-ed to
  * every replica, while the record of the registered Solver that its cleared
  * apply() uses is per-Block. Empty if the inner Blocks have not been
  * BlockSolverConfig-ured. */

 BlockSolverConfig * f_unit_BSC = nullptr;
 ///< the BlockSolverConfig given with the key "UnitBlockSolverConfig"
 /**< A clone, owned by the InvestmentFunction, of the BlockSolverConfig of
  * the copy of a UnitBlock with no copies [see set_ComputeConfig()];
  * nullptr if none has been given. */

 BlockSolverConfig * f_inner_BSC = nullptr;
 ///< the BlockSolverConfig given with the key "BlockSolverConfig"
 /**< A clone, owned by the InvestmentFunction and not clear()-ed, of the
  * BlockSolverConfig of the inner Block, which is that of the copy of a
  * UnitBlock with no copies if the inner Block is a UCBlock and no
  * "UnitBlockSolverConfig" is given; nullptr if none has been given. */

 bool f_blocks_are_updated = false;
 ///< indicates whether the sub-Blocks are updated

 int f_solver_status = 0;
 ///< the most recent status returned by the Solver of the sub-Block

 bool f_diagonal_linearization_required = false;
 ///< indicates whether a diagonal linearization is required

 bool f_compute_linearization = true;
 ///< indicates whether a linearization must be computed

 bool f_output_solution = false;
 ///< indicates whether the solution of each UCBlock must be output

 FunctionValue AAccMlt;
 ///< maximum absolute error in the multipliers of a linear combination

 bool f_ignore_modifications = false; ///< ignore any Modification

 bool f_reformulated_bounds = false;
 ///< indicates whether the bounds on the active variables were reformulated
 /**< The active variables of this InvestmentFunction may have natural lower
  * and upper bounds in the model in which they are defined. That is, for each
  * active variable \f$ x \f$, there may be \f$ l \f$ and \f$ u \f$ such
  * that \f$ l \le x \le u \f$. These natural bounds, however, may have been
  * reformulated such that \f$ x \f$ becomes a nonnegative variable
  * satisfying \f$ 0 \le x \le u - l \f$, if the lower bound \f$ l \f$ is
  * finite. This bool variable thus indicates whether
  * the natural bounds on the active variables have been reformulated. */

 bool f_replicate_battery = false;       ///< replicate battery units

 bool f_replicate_intermittent = false;  ///< replicate intermittent units

 bool f_has_value = false;
 ///< the value of the Function was successfully computed

 bool f_has_diagonal_linearization = false;
 ///< a diagonal linearization is available

 bool f_has_farkas_linearization = false;
 ///< a vertical linearization out of an infeasibility certificate is available
 /**< Set when the inner Block is proved infeasible and its Solver hands out
  * the Farkas certificate of it. The coefficients of the cut are read off the
  * Block exactly as the diagonal ones are, the certificate being written in
  * the very place the optimal duals are; what tells the two apart is only
  * this, and the constant, which for a vertical linearization is not built
  * out of the value of the Function (there is none, the point being outside
  * the domain) but out of f_farkas_value below. */

 FunctionValue f_farkas_value = 0;
 ///< the value of the infeasibility certificate at the current point
 /**< The certificate reads \f$ \Phi( x ) = w^\top d + \sum_j r_j b_j( x )
  * \f$, with \f$ w \f$ the dual ray, \f$ r \f$ the Farkas-consistent reduced
  * costs and \f$ b \f$ the bounds; the inner Block is infeasible at \f$ x
  * \f$ exactly when \f$ \Phi( x ) > 0 \f$, and \f$ \Phi( x ) \le 0 \f$ is
  * the cut. \f$ \Phi \f$ is affine in the design, so the cut is written as
  * usual as \f$ \alpha + g^\top x \le 0 \f$, with \f$ g \f$ the
  * coefficients and \f$ \alpha = \Phi( x ) - g^\top x \f$. */

 Index f_num_sub_blocks_per_stage = 1;
 ///< number of sub-Blocks per stage in SDDPBlock (single-Block mode)

 Index f_num_sub_blocks = 1;
 ///< number of (identical) replica sub-Blocks (multi-replica mode)
 /**< Number of identical inner Blocks held by this InvestmentFunction in
  * the multi-replica execution path. When >1, deserialize() will create
  * this many copies of the inner Block, and compute() dispatches to the
  * multi-replica SDDP code path. */

 double f_constraints_tolerance = 1.0e-6;
 ///< relative tolerance to declare feasible a linear constraint
 /**< Tolerance used in the relative-violation feasibility check performed
  * by constraints_are_satisfied(). The violation of a constraint is
  * divided by max(1, abs(rhs)) and compared against this tolerance. */

 void * f_id;  ///< the "identity" of the InvestmentFunction

 FunctionValue f_value;
 ///< the value of this InvestmentFunction after compute() is called

 std::vector< Index > v_block_indices_map;
 ///< map the index of an UnitBlock to the index of the asset under investment

 std::vector< Index > v_asset_indices;
 ///< indices of the assets that are subject to investment

 std::vector< Index > v_greedy_solvers;
 ///< indices of the SDDPGreedySolver

 SDDPSolver * f_sddp_solver = nullptr;
 ///< pointer to the SDDPSolver

 std::vector< AssetType > v_asset_type;
 ///< the type of each asset that is subject to investment

 std::vector< double > v_linearization;
 ///< linearization associated with the most recent call to compute()

 std::vector< double > v_cost;
 ///< the cost of investing in one unit of each asset

 std::vector< double > v_disinvestment_cost;
 ///< the cost of disinvesting in one unit of each asset

 std::vector< double > v_installed_quantity;
 ///< amount of each asset currently installed in the system

 std::vector< std::vector< std::vector< Index > > > generator_node_map;
 ///< maps the index of a generator to the node it belongs to
 /**< This vector maps a generator to the node it belongs to. The generator is
  * identified by a triplet (stage, unit_block_index, generator_index), where
  * stage indicates the stage (between 0 and SDDPBlock::get_time_horizon() -
  * 1), unit_block_index identifies the UnitBlock to which the generator
  * belongs (and index between 0 and v_block_indices.size() - 1) and
  * generator_index is the index of the generator within its UnitBlock. */

 std::vector< double > v_lower_bound;
 ///< lower bound on the value of the active variables

 MultiVector v_A;
 ///< the coefficient matrix of the linear constraints

 RealVector v_constraints_lower_bound;
 ///< the lower bound of the linear constraints

 RealVector v_constraints_upper_bound;
 ///< the upper bound of the linear constraints

 ViolatedConstraint f_violated_constraint;
 ///< it indicates which linear constraint has been violated

 std::vector< std::vector< EventHandler > > v_events;
 ///< container of event handlers
 /**< v_events[ h ][ i ] contains the event handler of ID i for the event type
  * h. */

 std::string f_output_filename;
 ///< name of the file into which the variable and function values are output

 std::string f_output_solution_directory;
 ///< path to the directory where the solution will be output
 /**< Used by the multi-replica SDDPBlock path; see
  * #strOutputSolutionDirectory. */

/*--------------------------------------------------------------------------*/
/*--------------------- PRIVATE PART OF THE CLASS --------------------------*/
/*--------------------------------------------------------------------------*/

 private:

/*--------------------------------------------------------------------------*/
/*-------------------------- PRIVATE CLASSES -------------------------------*/
/*--------------------------------------------------------------------------*/

 /// A convenience class for representing the global pool of linearizations
 class GlobalPool {

 public:

  static constexpr auto NaN =
                           std::numeric_limits< FunctionValue >::quiet_NaN();

/*--------------------------------------------------------------------------*/

  GlobalPool() = default;

/*--------------------------------------------------------------------------*/

  virtual ~GlobalPool() {}

/*--------------------------------------------------------------------------*/
  /// de-serialize a GlobalPool out of the given netCDF::NcGroup
  /** De-serialize a GlobalPool out of the given netCDF::NcGroup; see
   * GlobalPool::serialize() for a description of the format.
   *
   * @param group a netCDF::NcGroup out of which the GlobalPool will be
   *        de-serialized. */

  void deserialize( const netCDF::NcGroup & group );

/*--------------------------------------------------------------------------*/
  /// serialize this GlobalPool into the given netCDF::NcGroup
  /** This function serializes this GlobalPool into the given
   * netCDF::NcGroup. The netCDF::NcGroup \p group will contain the following
   * data:
   *
   * - The dimension "InvestmentFunction_MaxGlob" containing 1 + the maximum
   *   active name in the global pool; this means that there can be only
   *   InvestmentFunction_MaxGlob nonempty entries in the global pool, and the
   *   largest possible name of an active entry is InvestmentFunction_MaxGlob
   *   - 1. This variable is optional. If it is not present then 0 (empty
   *   global pool) is assumed.
   *
   * - The variable "InvestmentFunction_Type", of type netCDF::NcByte and
   *   indexed over the dimension InvestmentFunction_MaxGlob, which contains
   *   the vector of booleans specifying the type (diagonal/vertical) of each
   *   linearization in the global pool. The i-th element of this vector
   *   indicates the type of the i-th linearization: if it is zero, then the
   *   linearization is vertical; otherwise, the linearization is
   *   diagonal. This variable is optional only if InvestmentFunction_MaxGlob
   *   == 0.
   *
   * - The variable "InvestmentFunction_Constants", of type netCDF::NcDouble
   *   and indexed over the dimension InvestmentFunction_MaxGlob, which
   *   contains the constants of the linearizations. This variable is optional
   *   only if InvestmentFunction_MaxGlob == 0.
   *
   * - The variable "InvestmentFunction_Coefficients", of type
   *   netCDF::NcDouble and indexed over the dimension
   *   "InvestmentFunction_Coefficients_Dim", which contains the coefficients
   *   of the linearizations whose constants (see the netCDF
   *   "InvestmentFunction_Constants" variable) are not NaN. This variable is
   *   optional only if InvestmentFunction_MaxGlob == 0 or there is no i such
   *   that InvestmentFunction_Constants[ i ] != NaN. Let I = {i_0, i_1, ...,
   *   i_k} be the set of indices such that InvestmentFunction_Constants[ i ]
   *   != NaN if and only if i in I and i_j < i_{j+1} for all j in {0, ...,
   *   k-1}. Then, the linearization coefficients associated with the
   *   linearization i_j in I is given by
   *
   *   ( InvestmentFunction_Coefficients[j*NumVar], ...,
   *     InvestmentFunction_Coefficients[(j+1)*NumVar - 1] ).
   *
   * - The dimension "InvestmentFunction_ImpCoeffNum" containing the number of
   *   coefficients of the important combination of linearizations. This
   *   dimension is optional. If it is not provided then 0 (no important
   *   linearization) is assumed.
   *
   * - The variable "InvestmentFunction_ImpCoeffInd", of type netCDF::NcInt
   *   and indexed over the dimension InvestmentFunction_ImpCoeffNum, which
   *   contains indices of the linearizations that are part of the important
   *   combination. This variable is optional only if
   *   InvestmentFunction_ImpCoeffNum == 0.
   *
   * - The variable "InvestmentFunction_ImpCoeffVal", of type netCDF::NcDouble
   *   and indexed over the dimension InvestmentFunction_ImpCoeffNum, which
   *   contains the coefficients of the important combination of
   *   linearizations. The variable is optional only if
   *   InvestmentFunction_ImpCoeffNum == 0.
   *
   * @param group a netCDF::NcGroup into which this GlobalPool will be
   *        serialized. */

  void serialize( netCDF::NcGroup & group ) const;

/*--------------------------------------------------------------------------*/
  /// clone the given GlobalPool into this one

  void clone( const GlobalPool & global_pool );

/*--------------------------------------------------------------------------*/
  /// clone the given GlobalPool into this one

  void clone( GlobalPool && global_pool );

/*--------------------------------------------------------------------------*/
  // resizes the global pool
  /** Resize the global pool to have the given \p size. It is important to
   * notice that
   *
   *         IF THE SIZE OF THE POOL IS BEING DECREASED, ANY LINEARIZATION
   *         WHOSE name IS GREATER THAN OR EQUAL TO THE GIVEN NEW size IS
   *         DESTROYED.
   *
   * @param size The size of the global pool.
   */

  void resize( Index size );

/*--------------------------------------------------------------------------*/
  /// returns the size of the global pool

  Index size( void ) const { return( linearization_constants.size() ); }

/*--------------------------------------------------------------------------*/
  /// returns true if and only if this GlobalPool contains no linearization

  bool empty( void ) const {
   return( std::all_of( linearization_constants.cbegin() ,
                        linearization_constants.cend() ,
                        []( const auto v ) { return( std::isnan( v ) ); } ) );
  }

/*--------------------------------------------------------------------------*/
  /// stores the given linearization constant and solution in the global pool
  /** This function stores the given linearization constant and solution into
   * the global pool under the given \p name. If the given \p name is invalid,
   * an exception is thrown. If a Solution is currently stored under the given
   * \p name, this Solution is destroyed.
   *
   * @param constant the value of the linearization constant.
   *
   * @param coefficients the coefficients of the linearization.
   *
   * @param name the name under which the linearization will be stored.
   *
   * @param diagonal indicates whether the linearization is a diagonal one. */

  void store( FunctionValue constant ,
              std::vector< FunctionValue > coefficients ,
              Index name , bool diagonal );

/*--------------------------------------------------------------------------*/
  /// tells if there is a linearization in this GlobalPool with the given name
  /** This method returns true if \p name is the index (name) of a
   * linearization currently in this GlobalPool. */

  bool is_linearization_there( Index name ) const;

/*--------------------------------------------------------------------------*/
 /// tells if the linearization in this GlobalPool with that name is vertical
 /** This method returns true if \p name is the index (name) of a vertical
  * linearization currently in this GlobalPool. */

  bool is_linearization_vertical( Index name ) const;

/*--------------------------------------------------------------------------*/
  /// returns the linearization constant stored under the given name
  /** This function returns the value of the linearization constant that is
   * stored under the given \p name. If the given \p name is invalid, an
   * exception is thrown.
   *
   * @param name the name of the desired constant.
   *
   * @return the value of the linearization constant that is stored under the
   *         given \p name. */

  FunctionValue get_linearization_constant( Index name ) const
  {
   if( name < size() )
    return( linearization_constants[ name ] );

   throw( std::invalid_argument
          ( "InvestmentFunction::GlobalPool::get_linearization_constant: "
            "linearization with name " + std::to_string( name ) +
            " does not exist." ) );
   }

/*--------------------------------------------------------------------------*/
  /// sets the linearization constant under the given name
  /** This function sets the value of the linearization constant under the
   * given \p name. If the given \p name is invalid, an exception is thrown.
   *
   * @param constant the value of the linearization constant to be stored.
   *
   * @param name the name under which the constant will be stored. */

  void set_linearization_constant( FunctionValue constant , Index name )
  {
   if( name >= size() )
    throw( std::invalid_argument
           ( "InvestmentFunction::GlobalPool::set_linearization_constant: "
             "linearization with name " + std::to_string( name ) +
             " does not exist." ) );

   linearization_constants[ name ] = constant;
   }

/*--------------------------------------------------------------------------*/
  /// invalidates all linearizations
  /** This function invalidates all linearizations, by setting NaN to each
   * linearization constant currently stored. This means that any
   * linearization previously computed may no longer be valid. The
   * linearizations, however, remain stored in this global pool. If they
   * should be destroyed, explicit calls to delete_linearization() must be
   * made. */

  void invalidate( void ) {
   linearization_constants.assign( linearization_constants.size() , NaN );
   }

/*--------------------------------------------------------------------------*/

  void set_important_linearization( LinearCombination && coefficients ) {
   important_linearization_lin_comb = std::move( coefficients );
   }

/*--------------------------------------------------------------------------*/
  /// return the combination used to form "the important linearization"

  c_LinearCombination & get_important_linearization_coefficients() const {
   return( important_linearization_lin_comb );
   }

/*--------------------------------------------------------------------------*/
  /// stores a combination of the linearizations that are already stored
  /** This method creates a linear combination of a given set of
   * linearizations (specified by \p linear_combination) and stores it into
   * the global pool of linearizations with the given \p name (which must be
   * an integer between 0 and size() - 1). If \p linear_combination is empty,
   * an exception is thrown. If any of the names in the given \p
   * linear_combination is invalid, an exception is thrown. If the given \p
   * name is invalid, an exception is thrown.
   *
   * @param linear_combination the LinearCombination containing the names of
   *        the linearizations and their respective coefficients in the
   *        combination.
   *
   * @param name the name under which the combination of linearizations will
   *        be stored.
   *
   * @param AAccMlt the maximum absolute error in the multipliers. */

  void store_combination_of_linearizations(
				  c_LinearCombination & linear_combination ,
				  Index name , FunctionValue AAccMlt );

/*--------------------------------------------------------------------------*/
  /// deletes the linearization with the given name
  /** This function deletes the linearization with the given \p name. If the
   * given \p name is invalid, an exception is thrown.
   *
   * @param name the name of the linearization to be deleted. */

  void delete_linearization( Index name );

/*--------------------------------------------------------------------------*/
  /// deletes the linearizations with the given names
  /** This function deletes the linearizations with the given names given in
   * \p which. If any given name is invalid, an exception is thrown.
   *
   * @param which the names of the linearizations that must be deleted.
   */
  void delete_linearizations( Subset & which , bool ordered );

/*--------------------------------------------------------------------------*/

  void get_linearization_coefficients( FunctionValue * g , Range range ,
                                       Index name ) const;

/*--------------------------------------------------------------------------*/

  void get_linearization_coefficients( SparseVector & g , Range range ,
                                       Index name ) const;

/*--------------------------------------------------------------------------*/

  void get_linearization_coefficients( FunctionValue * g , c_Subset & subset ,
                                       const bool ordered , Index name )
   const;

/*--------------------------------------------------------------------------*/

  void get_linearization_coefficients( SparseVector & g , c_Subset & subset ,
                                       const bool ordered , Index name )
   const;

/*--------------------------------------------------------------------------*/

 private:

  std::vector< FunctionValue > linearization_constants;
  ///< linearization constants

  std::vector< std::vector< FunctionValue > > linearization_coefficients;
  ///< linearization coefficients

  LinearCombination important_linearization_lin_comb;
  ///< the linear combination of the important linearization

  std::vector< bool > is_diagonal;
  ///< indicates whether a linearization is diagonal

 }; // end( class( GlobalPool ) )

/*--------------------------------------------------------------------------*/
/*-------------------------- PRIVATE METHODS -------------------------------*/
/*--------------------------------------------------------------------------*/
 /// Update the given UnitBlock according to the given \p investment
 /** This function updates the given UnitBlock according to the given \p
  * investment.
  *
  * @param block A pointer to a UnitBlock.
  *
  * @param investment The investment to be made in the given UnitBlock. */

 void update_unit_block( UnitBlock * block , double investment );

/*--------------------------------------------------------------------------*/
 /// Update a set of UnitBlock according to the given \p investment
 /** This function updates a set of UnitBlock, given by their indices \p
  * block_indices, according to the given \p investment.
  *
  * @param sub_block_index The index of a sub-Block of this
  *        InvestmentFunction.
  *
  * @param block_indices Indices of the UnitBlock which must be updated.
  *
  * @param investment The investment to be made in each UnitBlock. */

 void update_unit_blocks( Index sub_block_index ,
                          const std::vector< Index > & block_indices ,
                          const std::vector< double > & investment );

/*--------------------------------------------------------------------------*/
 /// Update a set of line according to the given \p investment
 /** This function updates a set of transmission lines, given by their indices
  * \p line_indices, according to the given \p investment.
  *
  * @param sub_block_index The index of a sub-Block of this
  *        InvestmentFunction.
  *
  * @param line_indices Indices of the transmission lines which must be
  *        updated.
  *
  * @param investment The investment to be made in each line. */

 void update_network_blocks( Index sub_block_index ,
                             const std::vector< Index > & line_indices ,
                             const std::vector< double > & investment );

/*--------------------------------------------------------------------------*/
 /// writes the investment in the converters of a set of BatteryUnitBlock
 /** Sets the kappa of the converter [see
  * BatteryUnitBlock::set_converter_kappa()] of the BatteryUnitBlock with the
  * given indices \p block_indices to the given \p investment, in every
  * UCBlock of the \p sub_block_index-th inner Block. */

 void update_converters( Index sub_block_index ,
                         const std::vector< Index > & block_indices ,
                         const std::vector< double > & investment );

/*--------------------------------------------------------------------------*/
 /// gives a kappa of its own to the converters that are assets
 /** For each asset of type eConverter, gives the converter of the
  * BatteryUnitBlock in every UCBlock a kappa of its own (its current one) if
  * it follows the kappa of the battery [see
  * BatteryUnitBlock::converter_follows_kappa()], so that its rows exist when
  * the Constraints are generated. Throws std::invalid_argument if the unit
  * is not a BatteryUnitBlock or the batteries are replicated
  * ("ReplicateBatteryUnits"), since the converter of a battery represented
  * by its scale factor is not a separate asset. */

 void prepare_converter_assets( void );

/*--------------------------------------------------------------------------*/
 /// adds the coefficients of the converters to \p linearization

 void update_linearization_converters( Index stage , Index sub_block_index ,
	   const std::vector< std::pair< Index , Index > > & conv_indices ,
	   std::vector< double > & linearization );

/*--------------------------------------------------------------------------*/
 /// update the sub-Block of the UCBlock
 /** This function updates the sub-Block of the UCBlock to reflect the current
  * values of the x variables. */

 void update_blocks( void );

/*--------------------------------------------------------------------------*/
 /// sends a nuclear modification, invalidates the global pool
 /** Besides sending a "nuclear modification" for Function, it also
  * invalidates the global pool and declares that the Constraint of the
  * sub-Block are not updated.
  *
  * @param chnl the name of the channel to which the Modification should be
  *        sent. */

 void send_nuclear_modification( const Observer::ChnlName chnl = 0 );

/*--------------------------------------------------------------------------*/
 /// return a pointer to the Solver of the UCBlock associated with \p stage
 /** This function returns a pointer to the Solver of the UCBlock associated
  * with the given \p stage in the \p i-th sub-Block..
  *
  * @param stage A stage.
  *
  * @param i The index of a sub-Block.
  *
  * @return A pointer to the Solver of the UCBlock associated with the given
  *         \p stage in the \p i-th sub-Block. */

 CDASolver * get_ucblock_solver( Index stage , Index i ) const;

/*--------------------------------------------------------------------------*/
 /// returns a pointer to the UCBlock associated with the given \p stage
 /** This function returns a pointer to the UCBlock associated with
  * the given \p stage in the \p i-th sub-Block.
  *
  * @param stage A stage.
  *
  * @param i The index of a sub-Block.
  *
  * @return A pointer to the UCBlock associated with the given \p stage in the
  *         \p i-th sub-Block. */

 UCBlock * get_ucblock( Index stage , Index i ) const;

/*--------------------------------------------------------------------------*/
 /// returns a pointer to the UCBlock (if any)
 /** If the inner Block of this function is an UCBlock, it returns a pointer
  * to it. Otherwise, it returns nullptr.
  *
  * @return A pointer to the UCBlock of this InvestmentFunction. If the inner
  *         Block of this InvestmentFunction is not an UCBlock, it return
  *         nullptr. */

 UCBlock * get_ucblock( void ) const;

/*--------------------------------------------------------------------------*/
 /// returns a pointer to the SDDPBlock (if any)
 /** If the inner Block of this function is an SDDPBlock, it returns a pointer
  * to it. Otherwise, it returns nullptr.
  *
  * @return A pointer to the SDDPBlock of this InvestmentFunction. If the
  *         inner Block of this InvestmentFunction is not an SDDPBlock, it
  *         return nullptr. */

 SDDPBlock * get_sddp_block( void ) const;

/*--------------------------------------------------------------------------*/
 /// the inner Block of this function, if it is a TwoStageStochasticBlock
 /** If the inner Block of this function is a TwoStageStochasticBlock, it
  * returns a pointer to it. Otherwise, it returns nullptr.
  *
  * A TwoStageStochasticBlock as the inner Block is the "Benders form" of a
  * two-stage stochastic problem: the here-and-now Variable are the ones of
  * this InvestmentFunction, hence they exist in a single copy and outside
  * the scenarios, rather than being replicated in each of them and tied by
  * the non-anticipativity Constraint of the extensive form.
  *
  * @return A pointer to the TwoStageStochasticBlock of this
  *         InvestmentFunction, or nullptr if the inner Block is not one. */

 TwoStageStochasticBlock * get_tssb_block( void ) const;

/*--------------------------------------------------------------------------*/
 /// the number of sub-Blocks that carry the investment
 /** The investment is here-and-now, hence the same in all the sub-Blocks
  * that carry it: this is how many they are, one for a plain UCBlock, one
  * per scenario for a TwoStageStochasticBlock, the configured number for an
  * SDDPBlock. Writing the investment and reading the linearization back have
  * to run over exactly the same set, or the value and its linearization stop
  * describing the same function. */

 Index get_number_investment_sub_blocks( void ) const;

/*--------------------------------------------------------------------------*/
 /// reset the BlockConfig of the inner Block to the default one

 void set_default_inner_Block_BlockConfig();

/*--------------------------------------------------------------------------*/
 /// reset the BlockSolverConfig of the inner Block to the default one

 void set_default_inner_Block_BlockSolverConfig();

/*--------------------------------------------------------------------------*/
 /// remove the Solver that this InvestmentFunction registered in the Blocks
 /** Applies to each (replica) inner Block the clear()-ed BlockSolverConfig
  * that configured it [see v_BSC], i.e., un-registers and deletes all and
  * only the Solver that this InvestmentFunction has registered there,
  * leaving any other one alone; does nothing if the inner Blocks have not
  * been BlockSolverConfig-ured. */

 void unconfigure_inner_Block_Solver();

/*--------------------------------------------------------------------------*/
 /// reset the configuration of the inner Block to the default one
 /** Reset both the BlockConfig and the BlockSolverConfig of the inner Block
  * to the default ones. */

 void set_default_inner_Block_configuration( void ) {
  set_default_inner_Block_BlockSolverConfig();
  set_default_inner_Block_BlockConfig();
  }

/*--------------------------------------------------------------------------*/

 int get_inner_block_objective_sense() const;

/*--------------------------------------------------------------------------*/
 /// prepares the linearization for a new simulation

 void reset_linearization();

/*--------------------------------------------------------------------------*/
 /// the value of the infeasibility certificate at the current point
 /** Sums the certificate over the whole inner Block, rows and bounds alike,
  * reading each multiplier where the Solver has just written it and each
  * right-hand side and bound where it stands. Nothing here has to know which
  * of them carry the design: the design-dependent terms make the coefficients
  * of the cut, which are read separately, and what is wanted here is the
  * value of the whole, out of which the constant follows.
  *
  * It is the number the solvers already compute, FARKASPROOF in Gurobi and
  * the second output of CPXdualfarkas in CPLEX, and throw away; recomputing
  * it costs a sweep of the model, and is what keeps the cut available on the
  * solvers that do not hand it out. */

 FunctionValue compute_farkas_value( Index stage , Index sub_block_index );

/*--------------------------------------------------------------------------*/

 /// the linking terms of one copy of a scaled UnitBlock
 /** Gives term() each ColVariable of the UnitBlock with index \p block_index
  * in the UCBlock of \p stage and \p sub_block_index that appears in a
  * linking constraint of the UCBlock, together with its coefficient in the
  * constraint for one copy of the unit times the dual value of the
  * constraint (a ColVariable may be given more than once), and writes in
  * \p constant the terms that multiply no ColVariable, i.e., the sum of
  * \f$ \bar y^\top g^1_i( z_i ) \f$ in (2) is
  * \f$ \sum c \, z + \mbox{constant} \f$ over the pairs given. */

 void scale_linking_terms( Index block_index , Index stage ,
                           Index sub_block_index ,
                           const std::function< void( const ColVariable * ,
                                                      double ) > & term ,
                           double & constant );

/*--------------------------------------------------------------------------*/
 /// the coefficient (2) of a scaled UnitBlock with no copies
 /** Solves a copy of the UnitBlock alone, with the Objective
  * \f$ f^1_i( z_i ) + \bar y^\top g^1_i( z_i ) \f$ at the dual values
  * \f$ \bar y \f$ of the inner Block, by the Solver of the
  * BlockSolverConfig of the units [see set_ComputeConfig()], and returns
  * its optimal value, i.e., the value of (2) at a minimizer [see the class
  * description]. The copy is the mirror of the abstract representation of
  * the unit [see AbstractBlock::mirror()]; if the mirror cannot reproduce
  * something, it is a relaxation of one copy, whose minimum is not larger,
  * and the linearization is still below the value function, since the
  * number of copies cannot go below 0. Throws std::logic_error if the
  * Solver gives no solution. */

 double compute_zero_copy_linearization( Index block_index , Index stage ,
                                         Index sub_block_index );

/*--------------------------------------------------------------------------*/
 /// the BlockSolverConfig of the copy of a UnitBlock with no copies
 /** The one given with the key "UnitBlockSolverConfig", or else, with a
  * UCBlock as inner Block, the one given with the key "BlockSolverConfig";
  * throws std::logic_error if there is none [see set_ComputeConfig()]. */

 const BlockSolverConfig * get_unit_BlockSolverConfig( void ) const;

/*--------------------------------------------------------------------------*/

 double compute_scale_linearization( Index block_index , Index stage ,
                                     Index sub_block_index );

/*--------------------------------------------------------------------------*/
 /// updates the linearization to reflect the most recent scenario considered
 /** This function updates the linearization to reflect the most recent
  * scenario considered, whose subproblem was solved by the Solver attached to
  * the sub-Block whose index is \p sub_block_index.
  *
  * @param sub_block_index The index of the sub-Block which will be used to
  *        update the linearization.
  *
  * @param direction If true, what the sub-Block holds is an unbounded dual
  *        direction rather than an optimal dual solution: it is already in
  *        place, so it is not asked for again, and no primal solution is
  *        asked for either, there being none. Only the coefficients that are
  *        read out of the duals alone are then available, so an asset whose
  *        coefficient needs the primal makes this throw. */

 void update_linearization( Index sub_block_index , bool direction = false );

/*--------------------------------------------------------------------------*/
 /// updates the linearization with respect to the set of UnitBlock

 void update_linearization_unit_blocks( Index stage , Index sub_block_index ,
	   const std::vector< std::pair< Index , Index > > & block_indices ,
	   bool direction = false );

/*--------------------------------------------------------------------------*/
 /// updates the linearization with respect to the set of NetworkBlock

 void update_linearization_network_blocks( Index stage ,
					   Index sub_block_index ,
            const std::vector< std::pair< Index , Index > > & line_indices );

/*--------------------------------------------------------------------------*/
 /// returns the node to which the given generator belongs

 Index get_node( Index stage , Index unit_block_index , Index generator )
  const;

/*--------------------------------------------------------------------------*/
 /// builds the mapping between generator and the node it belongs to

 void build_generator_node_map();

/*--------------------------------------------------------------------------*/
 /// returns a pointer to the BendersBFunction associated with the given stage
 /** This function returns a pointer to the BendersBFunction associated with
  * the given \p stage in the \p i-th sub-Block.
  *
  * @param stage A stage.
  *
  * @param i The index of a sub-Block.
  *
  * @return A pointer to the BendersBFunction associated with the given \p
  *         stage in the \p i-th sub-Block. */

 BendersBFunction * get_benders_function( Index stage , Index i ) const;

/*--------------------------------------------------------------------------*/
 /// returns the value of the i-th active variable
 /** This function returns the value of the i-th active variable, possibly
  * taking into account its lower bound. If \p actual is \c true, then this
  * function returns the variable of the i-th active variable. Otherwise, if
  * the lower bound for this variable is finite, then this function returns
  * the value of this active variable plus its lower bound.
  *
  * This function is useful because the model to which this variable belongs
  * may have been reformulated as follows. This active variable, let us call
  * it \f$ x \f$, may be subject to lower and upper bounds, so that it must
  * satisfy \f$ l \le x \le u \f$ in the model in which it is defined.
  * However, this model may have been reformulated in such a way that, if the
  * lower bound \f$ l \f$ is finite, this variable becomes a nonnegative
  * variable which must then satisfy \f$ 0 \le x \le u - l \f$. Thus, if the
  * model has been reformulated in this way, \f$ l \f$ is finite (actually,
  * not minus infinity), and \p actual is false, then this function returns
  * \f$ x + l \f$; otherwise, it returns the value of \f$ x \f$. */

 double get_var_value( Index i , bool actual = true ) const
 {
  if( f_reformulated_bounds && ( ! actual ) && ( i < v_lower_bound.size() )
      && ( v_lower_bound[ i ] > -Inf< double >() ) )
   return( v_x[ i ]->get_value() + v_lower_bound[ i ] );

  return( v_x[ i ]->get_value() );
  }

/*--------------------------------------------------------------------------*/
 /// returns the lower bound for the given active variable
 /** This function returns the lower bound for the value of the i-th active
  * variable of this InvestmentFunction.
  *
  * @param i The index of an active variable of this InvestmentFunction.
  *
  * @return the lower bound for the value of the i-th active variable of this
  *         InvestmentFunction. */

 double get_var_lower_bound( Index i ) const
 {
  if( i < v_lower_bound.size() )
   return( v_lower_bound[ i ] );

  return( -Inf< double >() );
  }

/*--------------------------------------------------------------------------*/
 /// returns the investment cost of the i-th asset

 double get_cost( Index i ) const
 {
  if( i < v_cost.size() )
   return( v_cost[ i ] );

  return( 0 );
  }

/*--------------------------------------------------------------------------*/
 /// returns the disinvestment cost of the i-th asset

 double get_disinvestment_cost( Index i ) const
 {
  if( i < v_disinvestment_cost.size() )
   return( v_disinvestment_cost[ i ] );

  return( 0 );
  }

/*--------------------------------------------------------------------------*/
 /// returns the number of scenarios

 Index get_number_scenarios( void ) const;

/*--------------------------------------------------------------------------*/
 /// returns the number of stages

 Index get_number_stages( void ) const;

/*--------------------------------------------------------------------------*/
 /// locks a(n unlocked) sub-Block and returns its index

 Index lock_sub_block( void );

/*--------------------------------------------------------------------------*/
 /// unlocks the i-th sub-Block

 void unlock_sub_block( Index i );

/*--------------------------------------------------------------------------*/
 /// identifies the relevant Solver of the inner Block

 void build_solver_map( void );

/*--------------------------------------------------------------------------*/

 int compute_UCBlock( bool changedvars , bool owned );

/*--------------------------------------------------------------------------*/

 int compute_SDDPBlock( bool changedvars , bool owned );

/*--------------------------------------------------------------------------*/
 /// compute the InvestmentFunction in the multi-replica SDDPBlock path
 /** Executes compute() against multiple identical SDDPBlock replicas held
  * in v_Block, with scenarios partitioned across OpenMP threads (and MPI
  * processes if compiled with USE_MPI). Called from compute() when
  * v_Block.size() > 1 (or when a single replica was explicitly registered
  * via set_inner_blocks()). */

 int compute_SDDPBlock_replicas( bool changedvars );

/*--------------------------------------------------------------------------*/
 /// fire all event handlers registered for the given event \p type

 void handle_events( int type ) const;

/*--------------------------------------------------------------------------*/
 /// update the linearization, accumulating into the given vector
 /** Multi-replica variant of update_linearization() that accumulates the
  * linearization terms into the caller-supplied vector \p linearization
  * rather than into v_linearization. This is required for thread-safe
  * accumulation across OpenMP-parallel scenario loops. */

 void update_linearization( Index sub_block_index ,
                            std::vector< double > & linearization );

/*--------------------------------------------------------------------------*/

 void update_linearization_unit_blocks(
   Index stage , Index sub_block_index ,
   const std::vector< std::pair< Index , Index > > & block_indices ,
   std::vector< double > & linearization );

/*--------------------------------------------------------------------------*/

 void update_linearization_network_blocks(
   Index stage , Index sub_block_index ,
   const std::vector< std::pair< Index , Index > > & line_indices ,
   std::vector< double > & linearization );

/*--------------------------------------------------------------------------*/

 SMSpp_insert_in_factory_h;  // insert InvestmentFunction in the Block factory

/*--------------------------------------------------------------------------*/
/*---------------------------- PRIVATE FIELDS  -----------------------------*/
/*--------------------------------------------------------------------------*/

 /// indicates whether each sub-Block is locked
 std::vector< bool > is_locked;

 /// this is the waiting time before trying to acquire the lock again
 double waiting_time = 1e-4;

 /// global pool of linearizations
 GlobalPool global_pool;

 /// mame of the netCDF sub-group containing the inner Block
 inline static const std::string BLOCK_NAME = "InnerBlock";

/*--------------------------------------------------------------------------*/
/*---------------------------- PRIVATE METHODS  ----------------------------*/
/*--------------------------------------------------------------------------*/
 /// returns the value of the i-th linear constraint
 /** This function returns the value of the i-th linear constraints, i.e.,
  * a_i'x.
  *
  * @param i The index of a linear constraint.
  *
  * @return the value of the i-th constraint. */

 double compute_linear_constraint_value( Index i ) const;

/*--------------------------------------------------------------------------*/
 /// returns true if and only if the unverified constraints are satisfied
 /** This function returns true if and only if the linear constraints that
  * have not been verified by a previous call to this function are satisfied,
  * considering the current values of the active Variable of this
  * InvestmentFunction.
  *
  * The linear constraints are verified in order, from the first one to the
  * last one. Whenever a violated constraint is found, this function returns
  * false and the remaining constraints are not verified. If this function is
  * invoked again (before a new call to compute() is made), then only the
  * remaining (unverified) constraints are verified and, again, up until the
  * first violated constraints is found.
  *
  * @return true if and only if the unverified linear constraints are
  *         satisfied. */

 bool constraints_are_satisfied( void );

/*--------------------------------------------------------------------------*/
 /// returns the worst possible value for this InvestmentFunction
 /** Thus function returns the worst possible value for this
  * InvestmentFunction, which is +Inf if the sense of the Objective of the
  * inner Block is minimization (Objective::eMin) and -Inf otherwise.
  *
  * @return +Inf if the sense of the Objective of the inner Block is
  *         Objective::eMin and -Inf otherwise. */

 FunctionValue worst_value( void ) const
 {
  if( get_inner_block_objective_sense() == Objective::eMin )
   return( Inf< FunctionValue >() );

  return( -Inf< FunctionValue >() );
  }

/*--------------------------------------------------------------------------*/
 /// output the values of the active Variable of this InvestmentFunction
 /** This function appends the current values of the active Variable of this
  * InvestmentFunction into the file whose name is given by the parameter
  * #strOutputFile (if this name is not empty). */

 void output_variable_values( void ) const;

/*--------------------------------------------------------------------------*/
 /// output the current value of this InvestmentFunction
 /** This function appends the value of this InvestmentFunction into the file
  * whose name is given by the parameter #strOutputFile (if this name is not
  * empty). */

 void output_function_value( void ) const;

 };  // end( class( InvestmentFunction ) )

/*--------------------------------------------------------------------------*/
/*---------------------- CLASS InvestmentFunctionState ---------------------*/
/*--------------------------------------------------------------------------*/
/// class to describe the "internal state" of an InvestmentFunction
/** Derived class from State to describe the "internal state" of an
 * InvestmentFunction, i.e., its global pool. This means saving the
 * linearization constants, the types of the linearizations, the associated
 * Solution, and the coefficients of the important combination of
 * linearizations. */

class InvestmentFunctionState : public State
{
/*----------------------- PUBLIC PART OF THE CLASS -------------------------*/

public:

/*-------------------------------- FRIENDS ---------------------------------*/

 friend InvestmentFunction;

/*---------- CONSTRUCTING AND DESTRUCTING InvestmentFunctionState ----------*/

 /// constructor, doing everything or nothing.
 /** Constructor of InvestmentFunctionState. If provided with a pointer to a
  * InvestmentFunction, it immediately copies its "internal state", which is
  * the only way in which the InvestmentFunctionState can be initialised out
  * of an existing InvestmentFunction. If nullptr is passed (as by default),
  * then an "empty" InvestmentFunctionState is constructed that can only be
  * filled by calling deserialize(). */

 InvestmentFunctionState( const InvestmentFunction * function = nullptr );

/*--------------------------------------------------------------------------*/
 /// de-serialize a InvestmentFunctionState out of netCDF::NcGroup
 /** De-serialize a InvestmentFunctionState out of netCDF::NcGroup; see
  * InvestmentFunctionState::serialize() for a description of the format.
  *
  * @param group The netCDF::NcGroup containing the
  *        InvestmentFunctionState. */

 void deserialize( const netCDF::NcGroup & group ) override;

/*--------------------------------------------------------------------------*/
 /// destructor

 virtual ~InvestmentFunctionState() {}

/*------ METHODS DESCRIBING THE BEHAVIOR OF A InvestmentFunctionState ------*/

 /// serialize a InvestmentFunctionState into a netCDF::NcGroup
 /** This method serializes this InvestmentFunctionState into the provided
  * netCDF::NcGroup, so that it can later be read back by deserialize(). After
  * the call, \p group will contain the attribute "type", common to all State,
  * and everything necessary to describe a GlobalPool (see
  * InvestmentFunction::GlobalPool::serialize()).
  *
  * @param group The netCDF::NcGroup into which into which this
  *        InvestmentFunctionState will be serialized. */

 void serialize( netCDF::NcGroup & group ) const override;

/*-------------------- PROTECTED PART OF THE CLASS -------------------------*/

protected:

/*-------------------------- PROTECTED METHODS -----------------------------*/

 void print( std::ostream &output ) const override {
  output << "InvestmentFunctionState [" << this
         << "] with max global pool element " << global_pool.size();
  }

/*--------------------------- PROTECTED FIELDS -----------------------------*/
 /// global pool of linearizations

 InvestmentFunction::GlobalPool global_pool;

/*---------------------- PRIVATE PART OF THE CLASS -------------------------*/

private:

/*---------------------------- PRIVATE FIELDS ------------------------------*/

 SMSpp_insert_in_factory_h;

/*--------------------------------------------------------------------------*/

 };  // end( class( InvestmentFunctionState ) )

/*--------------------------------------------------------------------------*/
/*--------------------------------------------------------------------------*/

/** @} end( group( InvestmentFun_CLASSES ) ) -------------------------------*/
/*--------------------------------------------------------------------------*/

}  // end( namespace SMSpp_di_unipi_it )

/*--------------------------------------------------------------------------*/
/*--------------------------------------------------------------------------*/

#endif  /* InvestmentFunction.h included */

/*--------------------------------------------------------------------------*/
/*------------------- End File InvestmentFunction.h ------------------------*/
/*--------------------------------------------------------------------------*/
