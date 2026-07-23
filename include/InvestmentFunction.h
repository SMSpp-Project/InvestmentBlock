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

 class IntermittentUnitBlock;  // forward declaration of IntermittentUnitBlock

 class SDDPBlock;              // forward declaration of SDDPBlock

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
/// a Function for scaling a set of UnitBlock
/** An InvestmentFunction represents a C05Function that is capable of
 * evaluating the investment on a set of assets. InvestmentFunction derives
 * from *both* C05Function and Block. The main ingredients of an
 * InvestmentFunction are the following:
 *
 * - The active ColVariable of the InvestmentFunction represent investments
 *   that can be made in certain assets. The i-th active ColVariable
 *   represents the investment to be made in the i-th asset. An asset can be
 *   one of the following: a ThermalUnitBlock, an IntermittentUnitBlock, a
 *   BatteryUnitBlock, or a transmission line. The meaning of an investment
 *   depends on the type of asset it is associated with.
 *
 *   - Investing an amount x in a ThermalUnitBlock means that the unit is
 *     scaled (see ThermalUnitBlock::scale()) by x.
 *
 *   - For BatteryUnitBlock and IntermittentUnitBlock, two types of
 *     investments can be made. Considering an investment of an amount x, the
 *     unit may either be scaled by x (according to UnitBlock::scale()) or
 *     have its minimum and maximum power and storage levels scaled by x.
 *
 *   - Investing an amount x in a transmission line means that its minimum and
 *     maximum power flow are scaled by x.
 *
 * - The value of the InvestmentFunction is given by a fixed investment cost
 *   (CAPEX) and an [expected] operational cost (OPEX). The fixed investment
 *   cost is a linear function of the variables of the InvestmentFunction.
 *   The [expected] operational cost is given by the solution value of the
 *   inner Block (which depends on the investment being made, i.e., the values
 *   of the variables of the InvestmentFunction).
 *
 * - The InvestmentFunction has an inner Block which contains the assets to
 *   invest in. This inner Block can be either a UCBlock or an SDDPBlock. In
 *   the former case the OPEX is deterministic. In the latter case it is the
 *   expected cost of the sub-Block of the SDDPBlock, some of which (the ones
 *   where the investments take place) must themselves  be UCBlock. Note that
 *   the same amount of investment in the assets is made for all the stages
 *   of the SDDPBlock. */

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

 enum AssetType { eUnitBlock = 0 , eLine = 1 };

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
 /// virtualized concrete iterator, templated over const-ness
 /** A single concrete implementation of both ThinVarDepInterface::v_iterator
  * and ThinVarDepInterface::v_const_iterator, parameterized over the abstract
  * base \p Base (the v_iterator / v_const_iterator to derive from) and the
  * underlying \p UnderIt (VarVector::iterator / ::const_iterator). It sifts
  * through the "active" Variable of an InvestmentFunction; the two flavours
  * are the #v_iterator and #v_const_iterator aliases below. */

 template< class Base , class UnderIt >
 class v_iterator_tmpl : public Base
 {
  public:

  using reference = typename Base::reference;
  using pointer   = typename Base::pointer;

  explicit v_iterator_tmpl( UnderIt & itr ) : itr_( itr ) {}
  explicit v_iterator_tmpl( UnderIt && itr ) : itr_( std::move( itr ) ) {}

  Base * clone( void ) override final {
   return( new v_iterator_tmpl( itr_ ) );
   }

  void operator++( void ) override final { ++( itr_ ); }

  reference operator*( void ) const override final { return( *( *itr_ ) ); }
  pointer operator->( void ) const override final { return( *itr_ ); }

  bool operator==( const Base & rhs ) const override final {
   auto tmp = cast( rhs );
   return( tmp ? itr_ == tmp->itr_ : false );
   }
  bool operator!=( const Base & rhs ) const override final {
   auto tmp = cast( rhs );
   return( tmp ? itr_ != tmp->itr_ : true );
   }

  private:

  // debug builds check the dynamic type; release builds trust the caller
  static const v_iterator_tmpl * cast( const Base & rhs ) {
   #ifdef NDEBUG
    return( static_cast< const v_iterator_tmpl * >( & rhs ) );
   #else
    return( dynamic_cast< const v_iterator_tmpl * >( & rhs ) );
   #endif
   }

  UnderIt itr_;
  };

/*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/
 /// concrete iterator over the "active" Variable (non-const flavour)

 using v_iterator =
  v_iterator_tmpl< ThinVarDepInterface::v_iterator , VarVector::iterator >;

 /// concrete iterator over the "active" Variable (const flavour)

 using v_const_iterator =
  v_iterator_tmpl< ThinVarDepInterface::v_const_iterator ,
                   VarVector::const_iterator >;

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
  * If a key that is not any of the above is provided, an exception is
  * thrown. No pointer is kept by the InvestmentFunction, so the caller can
  * (and is responsible to) delete any pointer provided.
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
 /// sets the multiplicative weight w ( > 0 ) of this component
 /** Makes the InvestmentFunction behave as w * F, with w > 0 (BundleSolver
  * convex components require a strictly positive weight that lives inside the
  * Function). The weight scales the value and the diagonal linearization; the
  * feasibility (vertical) linearizations are weight-invariant and not scaled.
  *
  * Must be set ONCE before the function is first computed / attached to a
  * Solver: it is a plain setter that issues no Modification and does not
  * rescale linearizations already stored in the global pool.
  *
  * @param w the component weight ( > 0 ). */

 void set_weight( double w ) {
  if( w <= 0 )
   throw( std::invalid_argument( "InvestmentFunction::set_weight: "
                                 "weight must be > 0" ) );
  f_weight = w;
  }

/*--------------------------------------------------------------------------*/
 /// returns the multiplicative weight w of this component

 double get_weight( void ) const { return( f_weight ); }

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
  * @return The value of the parameter. */

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
  * @return The value of the parameter. */

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
  * @return The value of the parameter. */

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

/**@} ----------------------------------------------------------------------*/
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
  * active variable x, there may be l and u such that x must satisfy
  *
  *     l <= x <= u.
  *
  * These natural bounds, however, may have been reformulated such that x
  * becomes a nonnegative variable satisfying
  *
  *     0 <= x <= u - l
  *
  * if the lower bound l is finite. This function informs the
  * InvestmentFunction whether the natural bounds on the active variables have
  * been reformulated.
  *
  * @param reformulated If true, this means that the bounds on the active
  *        variables have been reformulated. Otherwise, the bounds are the
  *        "natural" ones. */

 void reformulated_bounds( bool reformulated ) {
  if( reformulated && ( ( ! v_asset_var_index.empty() ) ||
                        ( ! v_asset_baseline_var_index.empty() ) ) )
   throw( std::logic_error( "InvestmentFunction::reformulated_bounds: bound "
                            "reformulation is not supported together with a "
                            "non-identity asset-to-variable mapping or a "
                            "variable baseline" ) );
  f_reformulated_bounds = reformulated;
  }

/*--------------------------------------------------------------------------*/
 /// sets the mapping from assets to active variables
 /** Sets which active variable each asset invests in: \p avi has one entry per
  * asset, and avi[ i ] is the index of asset i's variable among the active
  * ones.
  *
  * This is needed when several InvestmentFunction share the same active
  * variables (as the components of a disaggregated InvestmentBlock do) but each
  * invests only in some of them, so there are more variables than assets and
  * the position alone no longer says which variable an asset uses.
  *
  * If \p avi is empty (the default) the mapping is the identity: asset i uses
  * the i-th active variable.
  *
  * @param avi one entry per asset, each the index of its variable among the
  *        active ones. */

 void set_asset_variable_indices( std::vector< Index > avi ) {
  if( ! avi.empty() ) {
   if( avi.size() != v_asset_indices.size() )
    throw( std::logic_error( "InvestmentFunction::set_asset_variable_indices: "
                             "the mapping has " + std::to_string( avi.size() ) +
                             " entries, but there are " +
                             std::to_string( v_asset_indices.size() ) +
                             " assets" ) );
   for( const auto j : avi )
    if( j >= v_x.size() )
     throw( std::logic_error( "InvestmentFunction::set_asset_variable_indices: "
                              "variable index " + std::to_string( j ) +
                              " is out of range (there are " +
                              std::to_string( v_x.size() ) +
                              " active variables)" ) );
   if( f_reformulated_bounds )
    throw( std::logic_error( "InvestmentFunction::set_asset_variable_indices: "
                             "a non-identity mapping is not supported when the "
                             "bounds have been reformulated" ) );
   }
  v_asset_var_index = std::move( avi );
  }

/*--------------------------------------------------------------------------*/
 /// sets the per-asset variable baseline (multi-period transition cost)
 /** Sets, for each asset, which active variable provides its *baseline*, i.e.,
  * "the amount there was before": the (dis)investment cost of asset i is then
  * computed on  d = x - baseline  as  max( c+ d , -c- d )  -- a function of
  * BOTH variables, so the subgradient gets a mirrored entry on the baseline
  * variable. This realizes the multi-period transition cost: the component of
  * period t declares the design variable of period t-1 as its baseline.
  *
  * If \p bvi is empty (the default) the baseline of each asset is the
  * InstalledQuantity datum, as in the single-period (legacy) case.
  *
  * A variable baseline cannot be combined with the InstalledQuantity datum in
  * the same InvestmentFunction (ambiguous: which "before"?), nor with
  * reformulated bounds (get_var_value() would silently shift the read value
  * by a per-asset datum indexed by variable); both combinations throw.
  *
  * @param bvi one entry per asset, each the index of its baseline variable
  *        among the active ones. */

 void set_asset_baseline_variable_indices( std::vector< Index > bvi ) {
  if( ! bvi.empty() ) {
   if( bvi.size() != v_asset_indices.size() )
    throw( std::logic_error(
     "InvestmentFunction::set_asset_baseline_variable_indices: the baseline "
     "mapping has " + std::to_string( bvi.size() ) + " entries, but there are "
     + std::to_string( v_asset_indices.size() ) + " assets" ) );
   for( const auto j : bvi )
    if( j >= v_x.size() )
     throw( std::logic_error(
      "InvestmentFunction::set_asset_baseline_variable_indices: variable "
      "index " + std::to_string( j ) + " is out of range (there are " +
      std::to_string( v_x.size() ) + " active variables)" ) );
   if( ! v_installed_quantity.empty() )
    throw( std::logic_error(
     "InvestmentFunction::set_asset_baseline_variable_indices: a variable "
     "baseline cannot be combined with InstalledQuantity" ) );
   if( f_reformulated_bounds )
    throw( std::logic_error(
     "InvestmentFunction::set_asset_baseline_variable_indices: a variable "
     "baseline is not supported when the bounds have been reformulated" ) );
   }
  v_asset_baseline_var_index = std::move( bvi );
  }

/*--------------------------------------------------------------------------*/
 /// returns the per-asset variable-baseline mapping (empty => legacy baseline)

 const std::vector< Index > & get_asset_baseline_variable_indices() const {
  return( v_asset_baseline_var_index );
  }

/*--------------------------------------------------------------------------*/
 /// returns the asset -> variable mapping (empty => identity)

 const std::vector< Index > & get_asset_variable_indices() const {
  return( v_asset_var_index );
  }

/*--------------------------------------------------------------------------*/
 /// sets the implicit linear constraints on the assets
 /** Sets the linear constraints  lb <= A x <= ub  enforced by the
  * InvestmentFunction via its vertical linearizations (for instance caps or
  * budgets over the assets under investment).
  *
  * It is the programmatic counterpart of reading the Constraints_A,
  * Constraints_LowerBound and Constraints_UpperBound netCDF fields: use it
  * when the function is built in memory rather than read from a file.
  *
  * PER-ASSET semantics, as the netCDF format declares
  * (NumConstraints x NumAssets): each row of \p A has one coefficient per
  * ASSET, and column j multiplies the variable of asset j (asset_var( j )),
  * wherever the mapping puts it. \p lb and \p ub have one entry per
  * constraint (per row of \p A).
  *
  * @param A coefficient matrix: one row per constraint, one column per asset.
  * @param lb lower bounds, one per constraint.
  * @param ub upper bounds, one per constraint. */

 void set_implicit_constraints( MultiVector A , RealVector lb , RealVector ub )
 {
  if( ( lb.size() != A.size() ) || ( ub.size() != A.size() ) )
   throw( std::logic_error( "InvestmentFunction::set_implicit_constraints: "
                            "A, lb and ub need one entry per constraint" ) );
  for( Index i = 0 ; i < A.size() ; ++i ) {
   if( A[ i ].size() != v_asset_indices.size() )
    throw( std::logic_error( "InvestmentFunction::set_implicit_constraints: "
                             "each row of A needs one coefficient per "
                             "asset" ) );
   if( lb[ i ] > ub[ i ] )
    throw( std::logic_error( "InvestmentFunction::set_implicit_constraints: "
                             "lb[ i ] > ub[ i ]" ) );
   }
  v_A = std::move( A );
  v_constraints_lower_bound = std::move( lb );
  v_constraints_upper_bound = std::move( ub );
  }

/*--------------------------------------------------------------------------*/

 void set_num_sub_blocks_per_stage( Index n ) {
  f_num_sub_blocks_per_stage = n;
  }

/*--------------------------------------------------------------------------*/
 /// set the number of (replica) sub-Blocks of the InvestmentFunction
 /** Sets the number of identical sub-Blocks that this InvestmentFunction
  * holds. This is the multi-replica analogue of
  * #set_num_sub_blocks_per_stage(), and is used by the new SDDPBlock
  * parallel execution path: the InvestmentFunction holds \p n identical
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
  *   subject to investment. An asset can be either a UnitBlock or a
  *   transmission line. If it is a UnitBlock, then its index is simply the
  *   index that this UnitBlock has within its UCBlock. If it is a
  *   transmission line, then its index is the index that this transmission
  *   line has within its NetworkBlock.
  *
  * - The one-dimensional variable "AssetType", of type netCDF::Uint(), which
  *   is either a scalar or indexed over "NumAssets", indicating the type of
  *   the i-th asset. For a UnitBlock, the type is 0, and for a transmission
  *   line, the type is 1. If it is a scalar, then we assume that AssetType[i]
  *   = AssetType[0] for all i in {0, ..., NumAssets - 1}. This variable is
  *   optional. If it is not provided, then we assume that AssetType[i] = 0
  *   for each i in {0, ..., NumAssets - 1}.
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
  *   optional. If it is not provided, then we assume that
  *   InstalledQuantity[i] = 1 for all i in {0, ..., NumAssets - 1}.
  *
  * Let x[i] represent the value of the i-th active Variable of this
  * InvestmentFunction. If x[i] is greater than InstalledQuantity[i], then the
  * i-th asset is being subject to investment. If x[i] is less than
  * InstalledQuantity[i], then the i-th asset is being subject to
  * disinvestment. Because the cost of investment is typically different from
  * the cost of disinvestment, the fixed (des)investment cost is composed by
  * two parts: the one associated with an investment and the one associated
  * with a disinvestment. The fixed cost associated with the i-th asset is
  * given by
  *
  *   c[i] * ( x[i] - InstalledQuantity[i] ) if x[i] > InstalledQuantity[i],
  *
  * and
  *
  *   d[i] * ( InstalledQuantity[i] - x[i] ) if x[i] <= InstalledQuantity[i].
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
 /** This method returns an approximation to the value of this
  * InvestmentFunction associated with the most recent call to compute(). The
  * returned value depends on the sense of the Objective of the sub-Block. If
  * the sense of the Objective of the sub-Block is "minimization", then this
  * method returns a valid upper bound on the optimal objective function value
  * of the sub-Block (see Solver::get_ub()). If the sense of the Objective of
  * the sub-Block is "maximization", then this method returns a valid upper
  * bound on the optimal objective function value of the sub-Block (see
  * Solver::get_lb()).
  *
  * Notice that if compute() has never been invoked, then the value returned
  * by this method is meaningless. Moreover, if this InvestmentFunction does
  * not have a sub-Block or its sub-Block does not have a Solver attached to
  * it, then an exception is thrown. */

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
 /** This method returns true only if this InvestmentFunction is convex. If
  * this InvestmentFunction has no sub-Block or the sense of the Objective of
  * its sub-Block is maximization, then this method returns false. Otherwise,
  * it returns true. */

 bool is_convex( void ) override;

/*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/
 /// returns true only if this InvestmentFunction is concave
 /** This method returns true only if this InvestmentFunction is concave. If
  * this InvestmentFunction has no sub-Block or the sense of the Objective of
  * its sub-Block is minimization, then this method returns false. Otherwise,
  * it returns true. */

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
  *    abs( 1 - sum coefficients of diagonal linearizations ) <=
  *                                                         K * #dblAAccMlt
  *
  *    where K is the number of linearizations being combined.
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
   // inner Block (legacy behavior)
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
  * active variable x, there may be l and u such that x must satisfy
  *
  *     l <= x <= u.
  *
  * These natural bounds, however, may have been reformulated such that x
  * becomes a nonnegative variable satisfying
  *
  *     0 <= x <= u - l
  *
  * if the lower bound l is finite. This bool variable thus indicates whether
  * the natural bounds on the active variables have been reformulated. */

 bool f_replicate_battery = false;       ///< replicate battery units

 bool f_replicate_intermittent = false;  ///< replicate intermittent units

 bool f_has_value = false;
 ///< the value of the Function was successfully computed

 bool f_has_diagonal_linearization = false;
 ///< a diagonal linearization is available

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

 double f_weight = 1.0;
 ///< multiplicative weight w ( > 0 ): the function behaves as w * F
 /**< Set once before solving via set_weight(). Applied to the value and the
  * diagonal linearization at the end of compute() (scale_diagonal_by_weight);
  * the vertical/feasibility linearizations from v_A are weight-invariant. */

 std::vector< Index > v_block_indices_map;
 ///< map the index of an UnitBlock to the index of the asset under investment

 std::vector< Index > v_asset_indices;
 ///< indices of the assets that are subject to investment

 std::vector< Index > v_asset_var_index;
 ///< maps each asset to its active variable
 /**< When empty (the default) the mapping is the identity: asset i uses the
  * i-th active variable. When set, v_asset_var_index[ i ] is the index of
  * asset i's variable among the active ones, letting several InvestmentFunction
  * share the same active variables while each invests only in some of them. */

 std::vector< Index > v_asset_baseline_var_index;
 ///< maps each asset to the active variable providing its baseline
 /**< When empty (the default) the baseline of each asset is the
  * InstalledQuantity datum (single-period, legacy). When set,
  * v_asset_baseline_var_index[ i ] is the index (among the active variables)
  * of the variable whose value is asset i's baseline -- the multi-period
  * transition: the component of period t reads the design of period t-1.
  * See set_asset_baseline_variable_indices() and add_linear_term(). */

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
 /// returns the active variable that asset \p i invests in
 /** Index of asset \p i's variable among the active ones. With no mapping set
  * (the common case) it is the identity: asset i uses the i-th active variable.
  * See set_asset_variable_indices(). */

 Index asset_var( Index i ) const {
  return( v_asset_var_index.empty() ? i : v_asset_var_index[ i ] );
  }

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
 /// reset the BlockConfig of the inner Block to the default one

 void set_default_inner_Block_BlockConfig();

/*--------------------------------------------------------------------------*/
 /// reset the BlockSolverConfig of the inner Block to the default one

 void set_default_inner_Block_BlockSolverConfig();

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

 double compute_scale_linearization( Index block_index , Index stage ,
                                     Index sub_block_index );

/*--------------------------------------------------------------------------*/
 /// updates the linearization to reflect the most recent scenario considered
 /** This function updates the linearization to reflect the most recent
  * scenario considered, whose subproblem was solved by the Solver attached to
  * the sub-Block whose index is \p sub_block_index.
  *
  * @param sub_block_index The index of the sub-Block which will be used to
  *        update the linearization. */

 void update_linearization( Index sub_block_index );

/*--------------------------------------------------------------------------*/
 /// updates the linearization with respect to the set of UnitBlock

 void update_linearization_unit_blocks( Index stage , Index sub_block_index ,
	   const std::vector< std::pair< Index , Index > > & block_indices );

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
  * it x, may be subject to lower and upper bounds, so that it must satisfy
  *
  *     l <= x <= u
  *
  * in the model in which it is defined. However, this model may have been
  * reformulated in such a way that, if the lower bound l is finite, this
  * variable becomes a nonnegative variable which must then satisfy
  *
  *     0 <= x <= u - l.
  *
  * Thus, if the model have been reformulated in this way, l is finite
  * (actually, not minus infinity), and \p actual is false, then this function
  * returns x + l. Otherwise, it returns the value of x. */

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
  * @para i The index of an active variable of this InvestmentFunction.
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
 /// scales the diagonal value and linearization by the component weight
 /** Applied at the end of each compute() path, where f_value and
  * v_linearization (the diagonal value/subgradient) are finalized. The
  * vertical (feasibility) linearization built from v_A is weight-invariant
  * (the domain { x : l <= A x <= u } does not change under a positive scale)
  * and is deliberately NOT touched here; see set_weight(). f_value and
  * v_linearization must be scaled together to keep the diagonal constant
  * alpha = f_value - <v_linearization,x> equal to w * alpha. */

 void scale_diagonal_by_weight() {
  if( f_weight == 1.0 )
   return;
  f_value *= f_weight;
  for( auto & c : v_linearization )
   c *= f_weight;
  }

/*--------------------------------------------------------------------------*/
 /// adds the (dis)investment linear term of every asset at the current point
 /** The single home of the transition-cost math, shared by the three
  * compute_*() paths (which only differ in the accumulators they pass in):
  * for each asset i, with  d = x - baseline , adds to \p value the cost
  * max( c+ d , -c- d )  and writes its subgradient into \p linearization.
  * The baseline is the value of the baseline variable when one is declared
  * (multi-period, see set_asset_baseline_variable_indices()), the
  * InstalledQuantity datum otherwise (single-period, legacy).
  *
  * With a variable baseline the cost is a function of BOTH variables, so the
  * subgradient has TWO entries: ( +c+ , -c+ ) for d > 0 and ( -c- , +c- )
  * for d < 0, on (asset variable, baseline variable) respectively. Omitting
  * the mirrored entry would be a SILENT bug: the cut looks valid but is not
  * a subgradient. At the kink d == 0 the else branch yields ( -c- , +c- ),
  * inside the subdifferential [ -c- , c+ ] since the costs are >= 0 (format
  * precondition). */

 void add_linear_term( double & value ,
                       std::vector< double > & linearization ) const
 {
  const bool var_baseline = ! v_asset_baseline_var_index.empty();

  for( Index i = 0 ; i < v_asset_indices.size() ; ++i ) {

   const auto var_index = asset_var( i );
   const auto x = get_var_value( var_index , false );
   const auto baseline = var_baseline ?
    get_var_value( v_asset_baseline_var_index[ i ] , false ) :
    get_installed_quantity( i );

   if( x > baseline ) {
    // an investment is being made in asset i
    const auto cost = get_cost( i );
    value += cost * ( x - baseline );
    linearization[ var_index ] += cost;
    if( var_baseline )
     linearization[ v_asset_baseline_var_index[ i ] ] -= cost;
    }
   else {
    // a disinvestment is being made in asset i (d == 0 falls here)
    const auto disinvestment_cost = get_disinvestment_cost( i );
    value += disinvestment_cost * ( baseline - x );
    linearization[ var_index ] -= disinvestment_cost;
    if( var_baseline )
     linearization[ v_asset_baseline_var_index[ i ] ] += disinvestment_cost;
    }
   }
  }

/*--------------------------------------------------------------------------*/
 /// dense vertical linearization (gradient of the violated constraint)
 /** The single home of the vertical (feasibility) cut, shared by
  * has_linearization() and the four get_linearization_coefficients()
  * flavours. Per-asset semantics of the implicit constraints: column j of
  * v_A belongs to ASSET j (as the netCDF format declares,
  * NumConstraints x NumAssets) and lands on that asset's variable
  * asset_var( j ); every other entry is zero. Under the legacy identity
  * mapping this is bit-identical to the historical per-variable reading --
  * which additionally read out of the row's bounds when the actives
  * outnumbered the assets. */

 void vertical_linearization( std::vector< double > & vec ) const {
  assert( f_violated_constraint.first < v_A.size() );
  const double sign = ( f_violated_constraint.second == eLHS ) ? -1 : 1;
  const auto & row = v_A[ f_violated_constraint.first ];
  vec.assign( v_x.size() , 0.0 );
  for( Index j = 0 ; j < row.size() ; ++j )
   vec[ asset_var( j ) ] += sign * row[ j ];
  }

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
