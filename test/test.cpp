/*--------------------------------------------------------------------------*/
/*---------------------------- File test.cpp -------------------------------*/
/*--------------------------------------------------------------------------*/
/** @file
 * Unit test of InvestmentBlock and InvestmentFunction on instances built in
 * memory.
 *
 * The curvature an InvestmentFunction declares is checked first: without an
 * inner Block it is neither convex nor concave, and (if a :MILPSolver is in
 * the build, which the inner Block needs) with a minimizing inner Block it
 * is convex only if the cost of investing plus the cost of disinvesting is
 * nonnegative for every asset.
 *
 * With UCBlock in the build, what an InvestmentBlock reads is checked
 * without solving anything. A file is refused when an asset names methods
 * that the methods factory does not have, or of another class than its
 * piece of the inner Block, or a setter without its getter, or a type that
 * does not exist; when a field has left the format, is longer than the
 * assets or is out of range; when the components have a gap in their
 * numbering, or the root carries linear constraints or a maximization;
 * when a unit has a second size and no first, or sizes naming different
 * methods; when components limit the change of one design Variable
 * differently; and when the inner Block is in design mode. A baseline
 * mapping of the wrong size or past the design Variable is refused too. A
 * file with components, written and read again, keeps its components and
 * their baselines; each component gets as active Variable the design
 * Variable its assets and their baselines name, with its mappings pointing
 * into them; and the feasibility of a design, against its bounds and the
 * limits on its change, is the same read off the ColVariable as off the
 * Constraint.
 *
 * Also, an inner AbstractBlock made of two UCBlock is split into two
 * components, and the InvestmentBlockSolution writes the :Solution of the
 * AbstractBlock as the one "InnerSolution" group, as for an inner Block that
 * is not split, which it then writes back.
 *
 * Then, again if a :MILPSolver is in the build, the linearization computed
 * by the InvestmentFunction is compared with the finite differences of its
 * value, or checked against it, on small UCBlock whose continuous
 * relaxation is a Linear Program:
 *
 * - an investment in a line of a meshed network, i.e., one whose lines have
 *   a nonzero susceptance, so that the flow divides among the paths: the
 *   derivative of the bound of the line is the same as for an HVDC line;
 *
 * - an investment in a ThermalUnitBlock represented by its scale factor, in
 *   a single bus with a demand of reactive power that only the commitment of
 *   that unit can cover: the value does not change with the scale, since
 *   the copies cover the same demand with a proportionally smaller
 *   commitment, while the objective of one copy does, and only the dual
 *   value of the reactive balance makes up for it;
 *
 * - an investment in a ThermalUnitBlock represented by its scale factor
 *   with no copies, where building it pays: the unit weighs nothing in the
 *   inner Block, and the coefficient is read from a copy of it solved alone
 *   against the dual values; the cutting plane method that starts at 0 has
 *   to leave 0 and reach the optimum;
 *
 * - an investment in a ConversionUnitBlock represented by its scale factor,
 *   with its two generators on two nodes, at one copy and at no copy;
 *
 * - an investment in the storage and in the converter of a BatteryUnitBlock
 *   as two assets (types 0 and 2), whose optimum has a storage twice the
 *   converter: the value at the optimum, the coefficients where one of the
 *   two binds and where all the fences meet at ( 0 , 0 ), and the single
 *   kappa of the battery, which resizes both and does worse.
 *
 * With the :MILPSolver, what compute() does besides the value is checked
 * too. A file written and read again gives the same value, and writes back
 * the names of the methods it was given. An inner Block that is infeasible
 * is said so, with a feasibility cut where the asset can give one, and an
 * unbounded one gives kUnbounded and -Inf. compute() fails rather than give
 * a wrong answer where a unit does not register the setter it is sized by,
 * and throws where the certificate would carry the Objective, leaving the
 * inner Block unlocked, and where the inner Block has no Solver. Removing an
 * active Variable takes its asset and the data of the asset with it; a
 * weight of the inner Block changed after a compute() weights the next; and
 * the global pool, shrunk, says removed only the names past its new size,
 * while a State of a smaller pool leaves those names empty.
 *
 * The test needs nothing but the module, its dependencies and, for the
 * parts with an inner Block, UCBlock and, for those that solve it, a
 * :MILPSolver; the netCDF groups are written in a dataset in memory, and the
 * Configuration files are read from the working directory.
 *
 * \author Donato Meoli \n
 *         Dipartimento di Informatica \n
 *         Universita' di Pisa \n
 */
/*--------------------------------------------------------------------------*/
/*------------------------------ INCLUDES ----------------------------------*/
/*--------------------------------------------------------------------------*/

#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

#include <netcdf>

#include "AbstractBlock.h"
#include "BlockSolverConfig.h"
#include "FRealObjective.h"
#include "InvestmentBlock.h"
#include "InvestmentFunction.h"

/*--------------------------------------------------------------------------*/
/*-------------------------------- USING -----------------------------------*/
/*--------------------------------------------------------------------------*/

using namespace SMSpp_di_unipi_it;

using Index = Block::Index;

/*--------------------------------------------------------------------------*/
/*-------------------------------- GLOBALS ---------------------------------*/
/*--------------------------------------------------------------------------*/

static int failures = 0;  // number of failed checks

static int NCID = -1;  // the netCDF dataset in memory the groups go in

static unsigned int n_groups = 0;  // groups written so far

/*--------------------------------------------------------------------------*/
/*------------------------------ FUNCTIONS ---------------------------------*/
/*--------------------------------------------------------------------------*/

static void check( bool ok , const std::string & what )
{
 if( ! ok ) {
  ++failures;
  std::cout << "FAILED: " << what << std::endl;
  }
 }

/*--------------------------------------------------------------------------*/

static bool close( double a , double b , double eps = 1e-6 )
{
 return( std::abs( a - b ) <= eps * std::max( 1.0 , std::abs( b ) ) );
 }

/*--------------------------------------------------------------------------*/
/// a new group, with a name never used before, in the netCDF dataset in
/// memory

static netCDF::NcGroup new_group( const std::string & prefix )
{
 if( NCID < 0 )
  if( nc_create( "ib_unit_test.nc4" , NC_DISKLESS | NC_NETCDF4 | NC_CLOBBER ,
		 & NCID ) != NC_NOERR )
   throw( std::runtime_error( "new_group: cannot create a netCDF dataset "
			      "in memory" ) );

 return( netCDF::NcGroup( NCID ).addGroup( prefix + "_" +
					   std::to_string( n_groups++ ) ) );
 }

/*--------------------------------------------------------------------------*/

static void put( netCDF::NcGroup & g , const std::string & n , double v )
{
 g.addVar( n , netCDF::NcDouble() ).putVar( & v );
 }

static void put_int( netCDF::NcGroup & g , const std::string & n , int v )
{
 g.addVar( n , netCDF::NcInt() ).putVar( & v );
 }

static void put( netCDF::NcGroup & g , const std::string & n ,
		 const netCDF::NcDim & d , const std::vector< double > & v )
{
 g.addVar( n , netCDF::NcDouble() , d ).putVar( v.data() );
 }

static void put( netCDF::NcGroup & g , const std::string & n ,
		 const std::vector< netCDF::NcDim > & d ,
		 const std::vector< double > & v )
{
 g.addVar( n , netCDF::NcDouble() , d ).putVar( v.data() );
 }

static void put_uint( netCDF::NcGroup & g , const std::string & n ,
		      const netCDF::NcDim & d ,
		      const std::vector< unsigned int > & v )
{
 g.addVar( n , netCDF::NcUint() , d ).putVar( v.data() );
 }

/// the names v of the methods of the assets of g, one per asset

static void put_names( netCDF::NcGroup & g , const std::string & n ,
		       const std::vector< std::string > & v )
{
 auto var = g.addVar( n , netCDF::NcString() , g.getDim( "NumAssets" ) );
 for( std::size_t i = 0 ; i < v.size() ; ++i )
  var.putVar( { i } , v[ i ] );
 }

/*--------------------------------------------------------------------------*/
/// that reading the InvestmentBlock of g is refused with an exception whose
/// message holds message

static void check_refused( const netCDF::NcGroup & g ,
			   const std::string & message ,
			   const std::string & what )
{
 try {
  InvestmentBlock ib;
  ib.deserialize( g );
  }
 catch( const std::exception & e ) {
  check( std::string( e.what() ).find( message ) != std::string::npos ,
	 what + ": refused, but saying \"" + e.what() + "\"" );
  return;
  }
 check( false , what + ": not refused" );
 }

/*--------------------------------------------------------------------------*/
/// a SlackUnitBlock with the given power, cost and reactive range

static void slack( netCDF::NcGroup g , double maxp , double cost ,
		   double maxq = 0 )
{
 g.putAtt( "type" , "SlackUnitBlock" );
 put( g , "MaxPower" , maxp );
 put( g , "ActivePowerCost" , cost );
 if( maxq > 0 ) {
  put( g , "MaxReactivePower" , maxq );
  put( g , "MinReactivePower" , - maxq );
  }
 }

/*--------------------------------------------------------------------------*/
/// the group of an InvestmentBlock with one asset of the given type and
/// index, and costs c and d; its InnerBlock group is returned in inner

static netCDF::NcGroup investment( unsigned int type , unsigned int asset ,
				   double c , double d ,
				   netCDF::NcGroup & inner )
{
 auto g = new_group( "InvestmentBlock" );
 g.putAtt( "type" , "InvestmentBlock" );
 auto na = g.addDim( "NumAssets" , 1 );
 put_uint( g , "Assets" , na , { asset } );
 put_uint( g , "AssetType" , na , { type } );
 put( g , "Cost" , na , { c } );
 put( g , "DisinvestmentCost" , na , { d } );
 put( g , "LowerBound" , 0 );
 put( g , "UpperBound" , 10 );
 inner = g.addGroup( "InnerBlock" );
 inner.putAtt( "type" , "UCBlock" );
 return( g );
 }

/*--------------------------------------------------------------------------*/
/// a triangle of lines with unit susceptance, 0 -> 1, 1 -> 2 and 0 -> 2, the
/// last one with flow limit 20; a SlackUnitBlock with cost 10 at node 0, one
/// with cost 50 at node 2, and a demand of 60 at node 2. The flow from node 0
/// divides into 2/3 on the direct line and 1/3 through node 1, hence with
/// the limit 20 kappa of the direct line the cost is 3000 - 1200 kappa; the
/// asset is that line

static netCDF::NcGroup meshed_network( double c , double d )
{
 netCDF::NcGroup u;
 auto g = investment( 1 , 2 , c , d , u );

 u.addDim( "TimeHorizon" , 1 );
 u.addDim( "NumberUnits" , 2 );
 auto ng = u.addDim( "NumberElectricalGenerators" , 2 );
 auto nn = u.addDim( "NumberNodes" , 3 );
 auto nl = u.addDim( "NumberLines" , 3 );
 auto th = u.getDim( "TimeHorizon" );

 put_uint( u , "GeneratorNode" , ng , { 0 , 2 } );
 put( u , "ActivePowerDemand" , { nn , th } , { 0 , 0 , 60 } );
 put_uint( u , "StartLine" , nl , { 0 , 1 , 0 } );
 put_uint( u , "EndLine" , nl , { 1 , 2 , 2 } );
 put( u , "MinPowerFlow" , nl , { -100 , -100 , -20 } );
 put( u , "MaxPowerFlow" , nl , { 100 , 100 , 20 } );
 put( u , "LineSusceptance" , nl , { 1 , 1 , 1 } );

 slack( u.addGroup( "UnitBlock_0" ) , 100 , 10 );
 slack( u.addGroup( "UnitBlock_1" ) , 100 , 50 );
 return( g );
 }

/*--------------------------------------------------------------------------*/
/// a single bus with active demand 10 and reactive demand 40; a
/// ThermalUnitBlock with fixed cost 100, linear cost 30 and reactive power
/// within [ -50 u , 50 u ], and a SlackUnitBlock with cost 10 and reactive
/// power within [ -1 , 1 ], which costs 10 per unit as the active one does.
/// At the optimum of the continuous relaxation the slack gives the active
/// power, and the thermal unit, scaled by sigma, the whole reactive power
/// with sigma u = 0.8 (a unit of reactive power costing 2 there), for the
/// cost 180 whatever sigma >= 0.8 is; the asset is the thermal unit,
/// represented by its scale

static netCDF::NcGroup reactive_bus( double c , double d )
{
 netCDF::NcGroup u;
 auto g = investment( 0 , 0 , c , d , u );

 u.addDim( "TimeHorizon" , 1 );
 u.addDim( "NumberUnits" , 2 );
 auto ng = u.addDim( "NumberElectricalGenerators" , 2 );
 auto nn = u.addDim( "NumberNodes" , 1 );
 auto th = u.getDim( "TimeHorizon" );

 put_uint( u , "GeneratorNode" , ng , { 0 , 0 } );
 put( u , "ActivePowerDemand" , { nn , th } , { 10 } );
 put( u , "ReactivePowerDemand" , { nn , th } , { 40 } );

 auto t = u.addGroup( "UnitBlock_0" );
 t.putAtt( "type" , "ThermalUnitBlock" );
 put( t , "MinPower" , 0 );
 put( t , "MaxPower" , 100 );
 put( t , "DeltaRampUp" , 100 );
 put( t , "DeltaRampDown" , 100 );
 put( t , "QuadTerm" , 0 );
 put( t , "LinearTerm" , 30 );
 put( t , "ConstTerm" , 100 );
 put( t , "StartUpCost" , 0 );
 put( t , "InitialPower" , 0 );
 put_int( t , "InitUpDownTime" , 1 );
 put_int( t , "MinUpTime" , 1 );
 put_int( t , "MinDownTime" , 1 );
 put( t , "MaxReactivePowerOn" , 50 );
 put( t , "MinReactivePowerOn" , -50 );

 slack( u.addGroup( "UnitBlock_1" ) , 100 , 10 , 1 );
 return( g );
 }

/*--------------------------------------------------------------------------*/
/// a single bus with demand 50; a ThermalUnitBlock with power within
/// [ 0 , 10 u ], linear cost 10 and no fixed cost, and a SlackUnitBlock with
/// cost 50. The asset is the thermal unit, represented by its scale, at the
/// price c per copy: each copy saves 40 * 10 = 400, hence for c = 100 the
/// value is 2500 - 300 x up to x = 5 and 500 + 100 x beyond; with a nonzero
/// invest the unit has an investment cost of its own, i.e., it is in design
/// mode

static void thermal_uc( netCDF::NcGroup & u , double invest = 0 ,
			bool line = false );

static netCDF::NcGroup thermal_bus( double c , double invest = 0 )
{
 netCDF::NcGroup u;
 auto g = investment( 0 , 0 , c , 0 , u );
 thermal_uc( u , invest );
 return( g );
 }

/*--------------------------------------------------------------------------*/
/// the UCBlock of thermal_bus() in the group u; with line, also a node 1
/// with no demand and no unit behind a line from node 0, which changes
/// nothing

static void thermal_uc( netCDF::NcGroup & u , double invest , bool line )
{
 u.addDim( "TimeHorizon" , 1 );
 u.addDim( "NumberUnits" , 2 );
 auto ng = u.addDim( "NumberElectricalGenerators" , 2 );
 auto nn = u.addDim( "NumberNodes" , line ? 2 : 1 );
 auto th = u.getDim( "TimeHorizon" );

 put_uint( u , "GeneratorNode" , ng , { 0 , 0 } );
 if( line ) {
  auto nl = u.addDim( "NumberLines" , 1 );
  put( u , "ActivePowerDemand" , { nn , th } , { 50 , 0 } );
  put_uint( u , "StartLine" , nl , { 0 } );
  put_uint( u , "EndLine" , nl , { 1 } );
  put( u , "MinPowerFlow" , nl , { -10 } );
  put( u , "MaxPowerFlow" , nl , { 10 } );
  }
 else
  put( u , "ActivePowerDemand" , { nn , th } , { 50 } );

 auto t = u.addGroup( "UnitBlock_0" );
 t.putAtt( "type" , "ThermalUnitBlock" );
 put( t , "MinPower" , 0 );
 put( t , "MaxPower" , 10 );
 put( t , "DeltaRampUp" , 100 );
 put( t , "DeltaRampDown" , 100 );
 put( t , "QuadTerm" , 0 );
 put( t , "LinearTerm" , 10 );
 put( t , "ConstTerm" , 0 );
 put( t , "StartUpCost" , 0 );
 put( t , "InitialPower" , 0 );
 put_int( t , "InitUpDownTime" , invest != 0 ? -1 : 1 );
 if( invest != 0 )
  put( t , "InvestmentCost" , invest );
 put_int( t , "MinUpTime" , 1 );
 put_int( t , "MinDownTime" , 1 );

 slack( u.addGroup( "UnitBlock_1" ) , 100 , 50 );
 }

/*--------------------------------------------------------------------------*/
/// the UCBlock of thermal_bus() with its line, whose assets are the line, at
/// the price 100 with 0.3 of it installed, and the thermal unit, at the
/// price 251 with 0.7 installed

static netCDF::NcGroup line_and_unit( void )
{
 auto g = new_group( "InvestmentBlock" );
 g.putAtt( "type" , "InvestmentBlock" );
 auto na = g.addDim( "NumAssets" , 2 );
 put_uint( g , "Assets" , na , { 0 , 0 } );
 put_uint( g , "AssetType" , na , { 1 , 0 } );
 put( g , "Cost" , na , { 100 , 251 } );
 put( g , "DisinvestmentCost" , na , { 0 , 0 } );
 put( g , "InstalledQuantity" , na , { 0.3 , 0.7 } );
 put( g , "LowerBound" , 0 );
 put( g , "UpperBound" , 10 );
 auto u = g.addGroup( "InnerBlock" );
 u.putAtt( "type" , "UCBlock" );
 thermal_uc( u , 0 , true );
 return( g );
 }

/*--------------------------------------------------------------------------*/
/// an InvestmentBlock with n design Variable in [ 0 , 10 ] and the
/// components Component_k, for k in which, each investing at the price c in
/// the thermal unit of the UCBlock of thermal_bus() on the design Variable k

static netCDF::NcGroup components( unsigned int n ,
				   const std::vector< unsigned int > & which ,
				   double c = 100 )
{
 auto g = new_group( "InvestmentBlock" );
 g.putAtt( "type" , "InvestmentBlock" );
 g.addDim( "NumAssets" , n );
 put( g , "LowerBound" , 0 );
 put( g , "UpperBound" , 10 );
 for( auto k : which ) {
  auto ck = g.addGroup( "Component_" + std::to_string( k ) );
  auto na = ck.addDim( "NumAssets" , 1 );
  put_uint( ck , "Assets" , na , { 0 } );
  put_uint( ck , "AssetType" , na , { 0 } );
  put( ck , "Cost" , na , { c } );
  put( ck , "DisinvestmentCost" , na , { 0 } );
  put_uint( ck , "AssetVarIndex" , na , { k } );
  auto u = ck.addGroup( "InnerBlock" );
  u.putAtt( "type" , "UCBlock" );
  thermal_uc( u );
  }
 return( g );
 }

/*--------------------------------------------------------------------------*/
/// a demand of 50 that one asset alone cannot cover at its size 1: for "line"
/// a SlackUnitBlock on node 0 behind a line to node 1, where the demand is,
/// with flow within 10 per unit of its kappa (line 0, type 1); for
/// "intermittent" an IntermittentUnitBlock of power 10 per unit of its kappa
/// and for "thermal" a ThermalUnitBlock of power 10 per copy (unit 0, type
/// 0), alone on a single bus

static netCDF::NcGroup short_supply( const std::string & what )
{
 netCDF::NcGroup u;
 const bool line = ( what == "line" );
 auto g = investment( line ? 1 : 0 , 0 , 100 , 0 , u );

 auto th = u.addDim( "TimeHorizon" , 1 );
 u.addDim( "NumberUnits" , 1 );
 auto ng = u.addDim( "NumberElectricalGenerators" , 1 );
 auto nn = u.addDim( "NumberNodes" , line ? 2 : 1 );
 put_uint( u , "GeneratorNode" , ng , { 0 } );

 if( line ) {
  auto nl = u.addDim( "NumberLines" , 1 );
  put( u , "ActivePowerDemand" , { nn , th } , { 0 , 50 } );
  put_uint( u , "StartLine" , nl , { 0 } );
  put_uint( u , "EndLine" , nl , { 1 } );
  put( u , "MinPowerFlow" , nl , { -10 } );
  put( u , "MaxPowerFlow" , nl , { 10 } );
  slack( u.addGroup( "UnitBlock_0" ) , 100 , 10 );
  return( g );
  }

 put( u , "ActivePowerDemand" , { nn , th } , { 50 } );
 auto t = u.addGroup( "UnitBlock_0" );
 if( what == "intermittent" ) {
  t.putAtt( "type" , "IntermittentUnitBlock" );
  put( t , "MaxPower" , 10 );
  return( g );
  }

 t.putAtt( "type" , "ThermalUnitBlock" );
 put( t , "MinPower" , 0 );
 put( t , "MaxPower" , 10 );
 put( t , "DeltaRampUp" , 100 );
 put( t , "DeltaRampDown" , 100 );
 put( t , "QuadTerm" , 0 );
 put( t , "LinearTerm" , 10 );
 put( t , "ConstTerm" , 0 );
 put( t , "StartUpCost" , 0 );
 put( t , "InitialPower" , 0 );
 put_int( t , "InitUpDownTime" , 1 );
 put_int( t , "MinUpTime" , 1 );
 put_int( t , "MinDownTime" , 1 );
 return( g );
 }

/*--------------------------------------------------------------------------*/
/// a single bus over 4 instants with demand 0, 0, 150, 150, a SlackUnitBlock
/// of 100 at cost 10 and one of 1000 at cost 50, and a BatteryUnitBlock of
/// storage 1 and converter 1 per unit, whose kappa is the asset; if two, the
/// converter is a second asset, the storage costing 10 and the converter 25,
/// otherwise the single kappa costs 35. The battery moves energy bought at
/// 10 to the last two instants: q per instant saves 80 q, and it needs a
/// storage of 2 q and a converter of q. The optimum is a storage of 100 and
/// a converter of 50, for 7000 - 4000 + 1000 + 1250 = 5250, while the single
/// kappa does best at 100, for 6500

static netCDF::NcGroup battery_bus( bool two )
{
 auto g = new_group( "InvestmentBlock" );
 g.putAtt( "type" , "InvestmentBlock" );
 auto na = g.addDim( "NumAssets" , two ? 2 : 1 );
 if( two ) {
  put_uint( g , "Assets" , na , { 2 , 2 } );
  put_uint( g , "AssetType" , na , { 0 , 2 } );
  put( g , "Cost" , na , { 10 , 25 } );
  put( g , "DisinvestmentCost" , na , { 0 , 0 } );
  }
 else {
  put_uint( g , "Assets" , na , { 2 } );
  put_uint( g , "AssetType" , na , { 0 } );
  put( g , "Cost" , na , { 35 } );
  put( g , "DisinvestmentCost" , na , { 0 } );
  }
 put( g , "LowerBound" , 0 );
 put( g , "UpperBound" , 1000 );
 auto u = g.addGroup( "InnerBlock" );
 u.putAtt( "type" , "UCBlock" );

 auto th = u.addDim( "TimeHorizon" , 4 );
 u.addDim( "NumberUnits" , 3 );
 auto ng = u.addDim( "NumberElectricalGenerators" , 3 );
 auto nn = u.addDim( "NumberNodes" , 1 );

 put_uint( u , "GeneratorNode" , ng , { 0 , 0 , 0 } );
 put( u , "ActivePowerDemand" , { nn , th } , { 0 , 0 , 150 , 150 } );

 slack( u.addGroup( "UnitBlock_0" ) , 100 , 10 );
 slack( u.addGroup( "UnitBlock_1" ) , 1000 , 50 );

 auto b = u.addGroup( "UnitBlock_2" );
 b.putAtt( "type" , "BatteryUnitBlock" );
 put( b , "MinStorage" , 0 );
 put( b , "MaxStorage" , 1 );
 put( b , "MaxPower" , 10 );
 put( b , "ConverterMaxPower" , 1 );
 put( b , "Kappa" , 0 );
 return( g );
 }

/*--------------------------------------------------------------------------*/
/// an electric node 0 with demand 50 and a heat node 1 with demand 25, not
/// connected (their line has capacity 0), a SlackUnitBlock with cost 50 on
/// node 0 and one with cost 30 on node 1, and a ConversionUnitBlock with its
/// electricity e in [ 0 , 10 u ] on node 0 and its heat h = e / 2 on node
/// 1, at cost 10 per unit of e. The asset is the conversion unit,
/// represented by its scale, at the price c per copy: each copy saves
/// 40 * 10 + 30 * 5 = 550, hence for c = 100 the value is 3250 - 450 x up
/// to x = 5, where both demands are covered, and 1000 + 100 ( x - 5 )
/// beyond

static netCDF::NcGroup conversion_nodes( double c )
{
 netCDF::NcGroup u;
 auto g = investment( 0 , 0 , c , 0 , u );

 u.addDim( "TimeHorizon" , 1 );
 u.addDim( "NumberUnits" , 3 );
 auto ng = u.addDim( "NumberElectricalGenerators" , 4 );
 auto nn = u.addDim( "NumberNodes" , 2 );
 auto nl = u.addDim( "NumberLines" , 1 );
 auto th = u.getDim( "TimeHorizon" );

 put_uint( u , "GeneratorNode" , ng , { 0 , 1 , 0 , 1 } );
 put( u , "ActivePowerDemand" , { nn , th } , { 50 , 25 } );
 put_uint( u , "StartLine" , nl , { 0 } );
 put_uint( u , "EndLine" , nl , { 1 } );
 put( u , "MinPowerFlow" , nl , { 0 } );
 put( u , "MaxPowerFlow" , nl , { 0 } );

 auto t = u.addGroup( "UnitBlock_0" );
 t.putAtt( "type" , "ConversionUnitBlock" );
 auto gg = t.addDim( "NumberGenerators" , 2 );
 auto nr = t.addDim( "NumberOperatingRows" , 1 );
 put( t , "MinPower" , { th , gg } , { 0 , 0 } );
 put( t , "MaxPower" , { th , gg } , { 10 , 5 } );
 put( t , "OperatingMatrix" , { nr , gg } , { -0.5 , 1 } );
 put( t , "OperatingLHS" , { th , nr } , { 0 } );
 put( t , "OperatingRHS" , { th , nr } , { 0 } );
 put( t , "LinearTerm" , { th , gg } , { 10 , 0 } );
 put_int( t , "InitUpDownTime" , 1 );

 slack( u.addGroup( "UnitBlock_1" ) , 100 , 50 );
 slack( u.addGroup( "UnitBlock_2" ) , 100 , 30 );
 return( g );
 }

/*--------------------------------------------------------------------------*/
/// the InvestmentBlock of the group g, configured by IBOCfg.txt, and its
/// InvestmentFunction in f

static InvestmentBlock * load( const netCDF::NcGroup & g ,
			       InvestmentFunction * & f )
{
 auto ib = dynamic_cast< InvestmentBlock * >( Block::new_Block( g ) );
 if( ! ib )
  throw( std::logic_error( "cannot build the InvestmentBlock" ) );

 ib->generate_abstract_variables();
 ib->generate_objective();

 auto c = Configuration::deserialize( "IBOCfg.txt" );
 auto bc = dynamic_cast< BlockConfig * >( c );
 if( ! bc ) {
  delete c;
  throw( std::invalid_argument( "cannot read IBOCfg.txt" ) );
  }
 bc->apply( ib );
 delete bc;

 f = static_cast< InvestmentFunction * >( ib->get_function() );
 return( ib );
 }

/*--------------------------------------------------------------------------*/
/// the value of f at the investment x, and its linearization in g

static double value( InvestmentFunction * f , double x , double & g )
{
 static_cast< ColVariable * >( f->get_active_var( 0 ) )->set_value( x );
 const auto status = f->compute( true );
 check( status == Solver::kOK , "compute() at " + std::to_string( x ) +
	" returns " + std::to_string( status ) );
 g = 0;
 if( f->has_linearization( true ) )
  f->get_linearization_coefficients( & g );
 else
  check( false , "no linearization at " + std::to_string( x ) );
 return( f->get_value() );
 }

/*--------------------------------------------------------------------------*/
/// the linearization of f at x against the central difference of its value
/// with step h

static void check_linearization( InvestmentFunction * f , double x ,
				 double h , double fx ,
				 const std::string & what )
{
 double g , gp , gm;
 const auto v = value( f , x , g );
 const auto vp = value( f , x + h , gp );
 const auto vm = value( f , x - h , gm );
 check( close( v , fx ) , what + ": value " + std::to_string( v ) +
	" instead of " + std::to_string( fx ) );
 const auto fd = ( vp - vm ) / ( 2 * h );
 check( close( g , fd , 1e-5 ) , what + ": linearization " +
	std::to_string( g ) + " while the value changes at the rate " +
	std::to_string( fd ) );
 }

/*--------------------------------------------------------------------------*/
/// the minimum of the convex function f of one variable over [ lo , hi ] by
/// the cutting plane method from lo, using its linearizations: the point
/// where it stops in x

static double cutting_plane( InvestmentFunction * f , double lo , double hi ,
			     double & x )
{
 std::vector< std::pair< double , double > > cuts;  // ( a , b ): a + b y
 double best = INFINITY;
 double y = lo;
 for( int it = 0 ; it < 50 ; ++it ) {
  double g;
  const auto v = value( f , y , g );
  if( v < best ) {
   best = v;
   x = y;
   }
  cuts.emplace_back( v - g * y , g );

  // the minimum of the model, at an end or where two cuts meet
  std::vector< double > cand = { lo , hi };
  for( Index i = 0 ; i < cuts.size() ; ++i )
   for( Index j = i + 1 ; j < cuts.size() ; ++j )
    if( cuts[ i ].second != cuts[ j ].second ) {
     const auto z = ( cuts[ j ].first - cuts[ i ].first ) /
                    ( cuts[ i ].second - cuts[ j ].second );
     if( ( z > lo ) && ( z < hi ) )
      cand.push_back( z );
     }
  double mmin = INFINITY;
  for( auto z : cand ) {
   double m = - INFINITY;
   for( const auto & [ a , b ] : cuts )
    m = std::max( m , a + b * z );
   if( m < mmin ) {
    mmin = m;
    y = z;
    }
   }
  if( best - mmin <= 1e-7 * std::max( 1.0 , std::abs( best ) ) )
   break;
  }
 return( best );
 }

/*--------------------------------------------------------------------------*/
/// the value of f at the investment ( x0 , x1 ), its linearization in g

static double value2( InvestmentFunction * f , double x0 , double x1 ,
		      std::vector< double > & g )
{
 static_cast< ColVariable * >( f->get_active_var( 0 ) )->set_value( x0 );
 static_cast< ColVariable * >( f->get_active_var( 1 ) )->set_value( x1 );
 const auto status = f->compute( true );
 check( status == Solver::kOK , "compute() at ( " + std::to_string( x0 ) +
	" , " + std::to_string( x1 ) + " ) returns " +
	std::to_string( status ) );
 g.assign( 2 , 0 );
 if( f->has_linearization( true ) )
  f->get_linearization_coefficients( g.data() );
 else
  check( false , "no linearization at ( " + std::to_string( x0 ) + " , " +
	 std::to_string( x1 ) + " )" );
 return( f->get_value() );
 }

/*--------------------------------------------------------------------------*/
/*---------------------------------- TESTS ---------------------------------*/
/*--------------------------------------------------------------------------*/
/// the curvature declared by an InvestmentFunction with no inner Block

static void test_curvature_empty( void )
{
 InvestmentFunction empty;
 check( ! empty.is_convex() , "an InvestmentFunction without inner Block "
	"says it is convex" );
 check( ! empty.is_concave() , "an InvestmentFunction without inner Block "
	"says it is concave" );
 }

/*--------------------------------------------------------------------------*/
/// the curvature declared by an InvestmentFunction with an inner Block

static void test_curvature( void )
{
 // minimizing inner Block, c + d = 100 >= 0: convex
 InvestmentFunction * f;
 auto ib = load( meshed_network( 100 , 0 ) , f );
 check( f->is_convex() && ( ! f->is_concave() ) ,
	"c + d >= 0: the InvestmentFunction is not convex" );
 delete ib;

 // c + d = -50 < 0: the investment term is concave in the asset, and the
 // InvestmentFunction is neither convex nor concave
 ib = load( meshed_network( 100 , -150 ) , f );
 check( ( ! f->is_convex() ) && ( ! f->is_concave() ) ,
	"c + d < 0: the InvestmentFunction says it is convex or concave" );
 delete ib;

 // c + d = 0, the edge: the investment term is linear, hence still convex
 ib = load( meshed_network( 100 , -100 ) , f );
 check( f->is_convex() , "c + d = 0: the InvestmentFunction is not convex" );
 delete ib;
 }

/*--------------------------------------------------------------------------*/
/// the linearization with respect to a line of a meshed network

static void test_meshed_line( void )
{
 InvestmentFunction * f;
 auto ib = load( meshed_network( 100 , 0 ) , f );
 // the operational cost 3000 - 1200 x and the investment cost 100 x
 check_linearization( f , 1 , 0.05 , 3000 - 1200 + 100 ,
		      "a line of a meshed network" );
 delete ib;
 }

/*--------------------------------------------------------------------------*/
/// the linearization with respect to the scale of a ThermalUnitBlock that
/// gives reactive power

static void test_scaled_reactive( void )
{
 InvestmentFunction * f;
 auto ib = load( reactive_bus( 0 , 0 ) , f );
 // the cost 10 * 10 of the slack and 100 * 0.8 of the thermal unit
 check_linearization( f , 1 , 0.05 , 180 ,
		      "the scale of a unit giving reactive power" );
 delete ib;
 }

/*--------------------------------------------------------------------------*/
/// the linearization with respect to the scale of a ThermalUnitBlock with
/// no copies, where building it pays: the unit weighs nothing in the inner
/// Block and its solution says nothing, while the coefficient has to be
/// what a copy gives at the dual values, - 400 (a copy saves 40 on each of
/// its 10 units of power), plus the derivative of the cost of the
/// investment, which at the kink 0 is minus the cost of disinvesting, 0;
/// the cutting plane method from 0 has to reach the optimum 1000 at 5

static void test_scaled_zero_copies( void )
{
 InvestmentFunction * f;
 auto ib = load( thermal_bus( 100 ) , f );
 double g;
 const auto v = value( f , 0 , g );
 check( close( v , 2500 ) , "no copies: value " + std::to_string( v ) +
	" instead of 2500" );
 check( close( g , -400 ) , "no copies: linearization " +
	std::to_string( g ) + " instead of -400" );
 double x;
 const auto best = cutting_plane( f , 0 , 10 , x );
 check( close( best , 1000 ) && close( x , 5 ) , "no copies: the cutting "
	"plane method from 0 stops at " + std::to_string( x ) + " with " +
	std::to_string( best ) + " instead of 5 with 1000" );
 delete ib;
 }

/*--------------------------------------------------------------------------*/
/// the linearization with respect to the scale of a ConversionUnitBlock,
/// whose two generators are on two nodes: at one copy against the finite
/// differences, at no copy the value of a copy solved alone, - 550, and the
/// cutting plane method from 0 has to reach the optimum 1000 at 5

static void test_scaled_conversion( void )
{
 InvestmentFunction * f;
 auto ib = load( conversion_nodes( 100 ) , f );
 check_linearization( f , 1 , 0.05 , 2800 , "the scale of a conversion "
		      "unit" );
 double g;
 const auto v = value( f , 0 , g );
 check( close( v , 3250 ) , "conversion, no copies: value " +
	std::to_string( v ) + " instead of 3250" );
 check( close( g , -550 ) , "conversion, no copies: linearization " +
	std::to_string( g ) + " instead of -550" );
 double x;
 const auto best = cutting_plane( f , 0 , 10 , x );
 check( close( best , 1000 ) && close( x , 5 ) , "conversion: the cutting "
	"plane method from 0 stops at " + std::to_string( x ) + " with " +
	std::to_string( best ) + " instead of 5 with 1000" );
 delete ib;
 }

/*--------------------------------------------------------------------------*/
/// the storage and the converter of a battery as two assets: at the optimum
/// ( 100 , 50 ) the value is 5250, the linearizations where the converter
/// binds ( 60 , 20 ) and where the storage does ( 30 , 40 ) are the costs
/// plus ( 0 , -80 ) and ( -40 , 0 ), and the one at ( 0 , 0 ), where all
/// the fences of the battery meet, is below the value elsewhere; with the
/// single kappa the cutting plane method from 0 stops at 100 with 6500

static void test_battery_two_assets( void )
{
 InvestmentFunction * f;
 auto ib = load( battery_bus( true ) , f );
 std::vector< double > g;
 auto v = value2( f , 100 , 50 , g );
 check( close( v , 5250 ) , "two assets: value at ( 100 , 50 ) " +
	std::to_string( v ) + " instead of 5250" );
 value2( f , 60 , 20 , g );
 check( close( g[ 0 ] , 10 ) && close( g[ 1 ] , -55 ) , "two assets: "
	"linearization at ( 60 , 20 ) ( " + std::to_string( g[ 0 ] ) + " , " +
	std::to_string( g[ 1 ] ) + " ) instead of ( 10 , -55 )" );
 value2( f , 30 , 40 , g );
 check( close( g[ 0 ] , -30 ) && close( g[ 1 ] , 25 ) , "two assets: "
	"linearization at ( 30 , 40 ) ( " + std::to_string( g[ 0 ] ) + " , " +
	std::to_string( g[ 1 ] ) + " ) instead of ( -30 , 25 )" );
 const auto v0 = value2( f , 0 , 0 , g );
 const auto g0 = g;
 check( close( v0 , 7000 ) , "two assets: value at ( 0 , 0 ) " +
	std::to_string( v0 ) + " instead of 7000" );
 for( const auto & [ a , b ] : std::vector< std::pair< double , double > >
	{ { 100 , 50 } , { 30 , 40 } , { 60 , 20 } , { 10 , 80 } ,
	  { 0 , 50 } , { 100 , 0 } } ) {
  v = value2( f , a , b , g );
  check( v >= v0 + g0[ 0 ] * a + g0[ 1 ] * b - 1e-6 , "two assets: the "
	 "linearization at ( 0 , 0 ) is above the value " +
	 std::to_string( v ) + " at ( " + std::to_string( a ) + " , " +
	 std::to_string( b ) + " )" );
  }
 delete ib;

 ib = load( battery_bus( false ) , f );
 double x;
 const auto best = cutting_plane( f , 0 , 1000 , x );
 check( close( best , 6500 ) && close( x , 100 ) , "single kappa: the "
	"cutting plane method from 0 stops at " + std::to_string( x ) +
	" with " + std::to_string( best ) + " instead of 100 with 6500" );
 delete ib;
 }

/*--------------------------------------------------------------------------*/
/// the methods an asset names are refused when the methods factory does not
/// have them, when they are of another class than the piece of the inner
/// Block, and when the setter comes without its getter; so is a type of
/// asset that does not exist

static void test_names_refused( void )
{
 auto g = meshed_network( 100 , 0 );
 put_names( g , "AssetSetter" , { "UCBlock::resize_nothing" } );
 put_names( g , "AssetLinearization" ,
	    { "UCBlock::get_resize_line_linearization" } );
 check_refused( g , "which the methods factory does not have as a setter" ,
		"an unknown setter" );

 g = meshed_network( 100 , 0 );
 put_names( g , "AssetSetter" , { "UCBlock::resize_line" } );
 put_names( g , "AssetLinearization" , { "UCBlock::read_nothing" } );
 check_refused( g , "which the methods factory does not have as a getter" ,
		"an unknown getter" );

 g = meshed_network( 100 , 0 );
 put_names( g , "AssetSetter" , { "DCNetworkBlock::resize" } );
 put_names( g , "AssetLinearization" ,
	    { "DCNetworkBlock::get_resize_linearization" } );
 check_refused( g , "is a method of DCNetworkBlock" ,
		"the methods of another class" );

 g = meshed_network( 100 , 0 );
 put_names( g , "AssetSetter" , { "UCBlock::resize_line" } );
 check_refused( g , "'AssetSetter' and 'AssetLinearization' go together" ,
		"a setter without its getter" );

 g = meshed_network( 100 , 0 );
 const unsigned int type = 3;
 g.getVar( "AssetType" ).putVar( { 0 } , type );
 check_refused( g , "invalid type of asset 0" ,
		"a type of asset that does not exist" );
 }

/*--------------------------------------------------------------------------*/
/// the fields of an asset are refused when they have left the format, when
/// they are longer than the assets, and when their values are out of range

static void test_fields_refused( void )
{
 auto g = meshed_network( 100 , 0 );
 put_uint( g , "AssetMethod" , g.getDim( "NumAssets" ) , { 0 } );
 check_refused( g , "'AssetMethod' is no longer supported" ,
		"AssetMethod, which has left the format" );

 g = meshed_network( 100 , 0 );
 g.putAtt( "ReplicateBatteryUnits" , netCDF::NcInt() , 1 );
 check_refused( g , "'ReplicateBatteryUnits' is no longer supported" ,
		"ReplicateBatteryUnits, which has left the format" );

 g = meshed_network( 100 , 0 );
 put( g , "MaxInvestment" , g.addDim( "Two" , 2 ) , { 1 , 2 } );
 check_refused( g , "'MaxInvestment' netCDF variable" ,
		"a MaxInvestment longer than the assets" );

 g = meshed_network( 100 , 0 );
 put( g , "Integer" , g.addDim( "Two" , 2 ) , { 1 , 1 } );
 check_refused( g , "'Integer' netCDF variable" ,
		"an Integer longer than the assets" );

 g = meshed_network( 100 , 0 );
 put_uint( g , "AssetFeasibilityCut" , g.getDim( "NumAssets" ) , { 2 } );
 check_refused( g , "invalid AssetFeasibilityCut: 2" ,
		"an AssetFeasibilityCut neither 0 nor 1" );

 g = meshed_network( 100 , 0 );
 put( g , "MaxDisinvestment" , -1 );
 check_refused( g , "'MaxDisinvestment' netCDF variable" ,
		"a negative MaxDisinvestment" );
 }

/*--------------------------------------------------------------------------*/
/// the structure of a file with components is refused when the numbering of
/// the components has a gap, and when the root carries what only a component
/// can hold, linear constraints or a maximization; a baseline mapping of the
/// wrong size or past the design Variable is refused too

static void test_structure_refused( void )
{
 check_refused( components( 3 , { 0 , 2 } ) ,
		"are numbered consecutively from 0" ,
		"Component_0 and Component_2 with no Component_1" );

 auto g = components( 2 , { 0 , 1 } );
 g.addDim( "NumConstraints" , 1 );
 check_refused( g , "carries 'NumConstraints'" ,
		"linear constraints at the root of the components" );

 g = components( 2 , { 0 , 1 } );
 g.addDim( "ObjectiveSense" , 0 );
 check_refused( g , "asks for a maximization" ,
		"a maximization at the root of the components" );

 auto b = dynamic_cast< InvestmentBlock * >(
		    Block::new_Block( components( 2 , { 0 , 1 } ) ) );
 check( b && ( b->get_investment_functions().size() == 2 ) ,
	"two components: not read as two" );
 if( b ) {
  auto f = b->get_investment_functions()[ 1 ];
  for( const auto & [ bvi , message ] :
	 std::vector< std::pair< std::vector< Index > , std::string > >
	 { { { 0 , 1 } , "the baseline mapping has 2 entries" } ,
	   { { 5 } , "variable index" } } )
   try {
    f->set_asset_baseline_variable_indices( bvi );
    check( false , "a wrong baseline mapping: not refused" );
    }
   catch( const std::exception & e ) {
    check( std::string( e.what() ).find( message ) != std::string::npos ,
	   "a wrong baseline mapping: refused, but saying \"" +
	   std::string( e.what() ) + "\"" );
    }
  delete b;
  }
 }

/*--------------------------------------------------------------------------*/
/// the second size of a unit is refused without the first, and so are the
/// two sizes of a unit naming different methods

static void test_second_size_refused( void )
{
 // the first asset becomes the second size of unit 0, which has no first
 auto g = battery_bus( true );
 const unsigned int unit = 0 , second = 2;
 g.getVar( "Assets" ).putVar( { 0 } , unit );
 g.getVar( "AssetType" ).putVar( { 0 } , second );
 check_refused( g , "is the second size of" , "a second size with no first" );

 g = battery_bus( true );
 put_names( g , "AssetSetter" ,
	    { "UCBlock::resize_unit" , "UCBlock::replicate" } );
 put_names( g , "AssetLinearization" ,
	    { "UCBlock::get_resize_unit_linearization" ,
	      "UCBlock::get_replicate_linearization" } );
 check_refused( g , "and name different methods" ,
		"the two sizes of a unit naming different methods" );
 }

/*--------------------------------------------------------------------------*/
/// an InvestmentBlock written by serialize() and read again gives the same
/// value, and the names of the methods written in its file are written back

static void test_round_trip( void )
{
 auto g = meshed_network( 100 , 0 );
 put_names( g , "AssetSetter" , { "UCBlock::resize_line" } );
 put_names( g , "AssetLinearization" ,
	    { "UCBlock::get_resize_line_linearization" } );
 InvestmentFunction * f;
 auto ib = load( g , f );
 double d;
 const auto v = value( f , 1.5 , d );

 auto w = new_group( "InvestmentBlock" );
 ib->serialize( w );
 delete ib;

 std::string setter;
 if( ! w.getVar( "AssetSetter" ).isNull() ) {
  char * name = nullptr;
  w.getVar( "AssetSetter" ).getVar( { 0 } , & name );
  setter = name;
  nc_free_string( 1 , & name );
  }
 check( setter == "UCBlock::resize_line" , "written and read: the setter "
	"written back is \"" + setter + "\" instead of UCBlock::resize_line" );

 ib = load( w , f );
 const auto vw = value( f , 1.5 , d );
 check( close( vw , v ) , "written and read: value " + std::to_string( vw ) +
	" instead of " + std::to_string( v ) );
 delete ib;
 }

/*--------------------------------------------------------------------------*/
/// an InvestmentBlock with two components, the second reading the design
/// Variable of the first as its baseline, written by serialize() and read
/// again has two components, and the same baseline mapping

static void test_round_trip_components( void )
{
 auto g = components( 2 , { 0 , 1 } );
 auto c1 = g.getGroup( "Component_1" );
 put_uint( c1 , "AssetBaselineVarIndex" , c1.getDim( "NumAssets" ) , { 0 } );

 auto b = dynamic_cast< InvestmentBlock * >( Block::new_Block( g ) );
 check( b , "two components with a baseline: not read" );
 if( ! b )
  return;
 auto w = new_group( "InvestmentBlock" );
 b->serialize( w );
 delete b;

 b = dynamic_cast< InvestmentBlock * >( Block::new_Block( w ) );
 check( b && ( b->get_investment_functions().size() == 2 ) ,
	"two components written and read: not read as two" );
 if( b && ( b->get_investment_functions().size() == 2 ) ) {
  const auto & bvi =
   b->get_investment_functions()[ 1 ]->get_asset_baseline_variable_indices();
  check( ( bvi.size() == 1 ) && ( bvi[ 0 ] == 0 ) ,
	 "two components written and read: the baseline of the second is "
	 "no longer the design Variable of the first" );
  }
 delete b;
 }

/*--------------------------------------------------------------------------*/
/// where the inner Block is infeasible, at the size 1 of short_supply(), the
/// InvestmentFunction says so, with a cut for a line and a unit that are
/// resized, with no cut for a unit replicated, whose copies give none, nor
/// for a resized asset with AssetFeasibilityCut 0

static void test_feasibility_cut( void )
{
 for( const auto & [ what , zero , cut ] :
	std::vector< std::tuple< std::string , bool , bool > >
	{ { "line" , false , true } , { "intermittent" , false , true } ,
	  { "thermal" , false , false } , { "line" , true , false } } ) {
  auto g = short_supply( what );
  if( zero )
   put_uint( g , "AssetFeasibilityCut" , g.getDim( "NumAssets" ) , { 0 } );
  const auto name = what + ( zero ? " with AssetFeasibilityCut 0" : "" );
  InvestmentFunction * f;
  auto ib = load( g , f );
  static_cast< ColVariable * >( f->get_active_var( 0 ) )->set_value( 1 );
  const auto status = f->compute( true );
  check( status == Solver::kInfeasible , name + ": compute() returns " +
	 std::to_string( status ) + " instead of kInfeasible" );
  check( f->has_linearization( false ) == cut , name + ( cut ?
	 ": no cut where the inner Block is infeasible" :
	 ": a cut it cannot give" ) );
  delete ib;
  }
 }

/*--------------------------------------------------------------------------*/
/// compute() fails, rather than giving a wrong answer, where an asset is a
/// unit whose class does not register the setter it is sized by; it throws
/// where the certificate of an infeasible inner Block would carry the
/// Objective, leaving the inner Block unlocked and the Solver with its own
/// identity, and where the inner Block has no Solver

static void test_compute_errors( void )
{
 // the slack unit 1 of thermal_bus(), sized by UCBlock::resize_unit, which
 // hands the value on to SlackUnitBlock::resize, a name nobody registers
 auto g = thermal_bus( 100 );
 const unsigned int unit = 1;
 g.getVar( "Assets" ).putVar( { 0 } , unit );
 put_names( g , "AssetSetter" , { "UCBlock::resize_unit" } );
 put_names( g , "AssetLinearization" ,
	    { "UCBlock::get_resize_unit_linearization" } );
 InvestmentFunction * f;
 auto ib = load( g , f );
 static_cast< ColVariable * >( f->get_active_var( 0 ) )->set_value( 1 );
 auto status = f->compute( true );
 check( status == Solver::kError , "a unit whose class has no resize: "
	"compute() returns " + std::to_string( status ) );
 delete ib;

 ib = load( short_supply( "line" ) , f );
 auto inner = f->get_nested_Block( 0 );
 auto solver = inner->get_registered_solvers().front();
 solver->set_par( solver->int_par_str2idx( "intHomogeneousDirection" ) , 0 );
 void * const id = solver->id();
 static_cast< ColVariable * >( f->get_active_var( 0 ) )->set_value( 1 );
 bool threw = false;
 try {
  f->compute( true );
  }
 catch( const std::logic_error & ) {
  threw = true;
  }
 const bool unlocked = inner->is_owned_by( nullptr );
 check( threw && unlocked && ( solver->id() == id ) ,
	std::string( "intHomogeneousDirection 0: compute() " ) +
	( threw ? "throws" : "does not throw" ) +
	( unlocked ? "" : ", the inner Block left locked" ) +
	( solver->id() == id ? "" : ", the Solver left with the identity of "
	  "the function" ) );
 if( unlocked )  // a Block left locked is not destroyed
  delete ib;

 // no IBOCfg.txt, hence no Solver on the inner Block
 ib = dynamic_cast< InvestmentBlock * >(
		   Block::new_Block( meshed_network( 100 , 0 ) ) );
 ib->generate_abstract_variables();
 ib->generate_objective();
 f = static_cast< InvestmentFunction * >( ib->get_function() );
 static_cast< ColVariable * >( f->get_active_var( 0 ) )->set_value( 1 );
 threw = false;
 try {
  f->compute( true );
  }
 catch( const std::logic_error & e ) {
  threw = ( std::string( e.what() ).find( "has no Solver" ) !=
	    std::string::npos );
  }
 check( threw , "an inner Block with no Solver: compute() does not throw "
	"saying so" );
 delete ib;
 }

/*--------------------------------------------------------------------------*/
/// an inner Block that its Solver proves unbounded is an answer: on a
/// single bus an IntermittentUnitBlock can absorb any power and a
/// SlackUnitBlock gives any power at a negative cost, and compute() returns
/// kUnbounded with the value -Inf

static void test_unbounded( void )
{
 netCDF::NcGroup u;
 auto g = investment( 0 , 0 , 100 , 0 , u );
 auto th = u.addDim( "TimeHorizon" , 1 );
 u.addDim( "NumberUnits" , 2 );
 auto ng = u.addDim( "NumberElectricalGenerators" , 2 );
 auto nn = u.addDim( "NumberNodes" , 1 );
 put_uint( u , "GeneratorNode" , ng , { 0 , 0 } );
 put( u , "ActivePowerDemand" , { nn , th } , { 0 } );
 auto t = u.addGroup( "UnitBlock_0" );
 t.putAtt( "type" , "IntermittentUnitBlock" );
 put( t , "MaxPower" , 10 );
 put( t , "MinPower" , - INFINITY );
 slack( u.addGroup( "UnitBlock_1" ) , INFINITY , -1 );

 InvestmentFunction * f;
 auto ib = load( g , f );
 static_cast< ColVariable * >( f->get_active_var( 0 ) )->set_value( 1 );
 const auto status = f->compute( true );
 check( ( status == Solver::kUnbounded ) && ( f->get_value() == - INFINITY ) ,
	"an unbounded inner Block: compute() returns " +
	std::to_string( status ) + " with the value " +
	std::to_string( f->get_value() ) );
 delete ib;
 }

/*--------------------------------------------------------------------------*/
/// removing the active Variable of the line of line_and_unit(), by each of
/// the methods, takes away the line and its data, and leaves the unit
/// with its own: at 2 copies the operational cost 1700 of thermal_bus() plus
/// 251 ( 2 - 0.7 ), which the data of the line left at the position of the
/// unit would make 100 ( 2 - 0.3 ); and so does removing the line where the
/// mapping puts it on the second Variable

static void test_remove_variables( void )
{
 const double expected = 1700 + 251 * ( 2 - 0.7 );
 for( const auto & [ what , remove ] :
	std::vector< std::pair< std::string ,
	 std::function< void( InvestmentFunction * ) > > >
	{ { "remove_variable( 0 )" , []( InvestmentFunction * f ) {
	     f->remove_variable( 0 , eNoMod ); } } ,
	  { "remove_variables( Range )" , []( InvestmentFunction * f ) {
	     f->remove_variables( Block::Range( 0 , 1 ) , eNoMod ); } } ,
	  { "remove_variables( Subset )" , []( InvestmentFunction * f ) {
	     f->remove_variables( Block::Subset{ 0 } , true , eNoMod ); } } ,
	  { "the line on the second Variable, remove_variable( 1 )" ,
	    []( InvestmentFunction * f ) {
	     f->set_asset_variable_indices( { 1 , 0 } );
	     f->remove_variable( 1 , eNoMod ); } } } ) {
  InvestmentFunction * f;
  auto ib = load( line_and_unit() , f );
  remove( f );
  double g;
  const auto v = value( f , 2 , g );
  check( close( v , expected ) , what + ": value " + std::to_string( v ) +
	 " instead of " + std::to_string( expected ) );
  delete ib;
  }
 }

/*--------------------------------------------------------------------------*/
/// the weight of the Objective of the inner Block, changed after a
/// compute(), weights the next one: at 1 copy of thermal_bus() the value
/// 2200 and the linearization -300 become 4400 and -600 with the weight 2

static void test_weight_changed( void )
{
 InvestmentFunction * f;
 auto ib = load( thermal_bus( 100 ) , f );
 double g;
 auto v = value( f , 1 , g );
 check( close( v , 2200 ) && close( g , -300 ) , "weight 1: value " +
	std::to_string( v ) + " and linearization " + std::to_string( g ) +
	" instead of 2200 and -300" );
 f->get_nested_Block( 0 )->set_objective_weight( 2 );
 v = value( f , 1 , g );
 check( close( v , 4400 ) && close( g , -600 ) , "weight changed to 2: "
	"value " + std::to_string( v ) + " and linearization " +
	std::to_string( g ) + " instead of 4400 and -600" );
 delete ib;
 }

/*--------------------------------------------------------------------------*/
/// a Solver that keeps the names that the last GlobalPoolRemoved
/// Modification of an InvestmentFunction says removed

class PoolSolver : public Solver {
 public:
  Block::Subset removed;
  void add_Modification( sp_Mod & mod ) override {
   if( auto m = std::dynamic_pointer_cast< const C05FunctionMod >( mod ) )
    if( m->type() == C05FunctionMod::GlobalPoolRemoved )
     removed = m->which();
   Solver::add_Modification( mod );
   }
  int compute( bool changedvars = true ) override { return( kOK ); }
  void get_var_solution( Configuration * solc = nullptr ) override {}
 private:
  const std::string & private_name( void ) const override {
   static const std::string name = "PoolSolver";
   return( name );
   }
 };

/// the global pool of the linearizations: shrunk from 5 to 2 it says removed
/// the names 2 to 4, not all of them; a State of the pool of 2 put into the
/// pool of 5 leaves empty the names 3 and 4 stored there

static void test_global_pool( void )
{
 InvestmentFunction * f;
 auto ib = load( thermal_bus( 100 ) , f );
 PoolSolver solver;
 ib->register_Solver( & solver );
 f->set_par( C05Function::intGPMaxSz , 5 );
 f->set_par( C05Function::intGPMaxSz , 2 );
 ib->unregister_Solver( & solver );
 check( solver.removed == Block::Subset{ 2 , 3 , 4 } , "a pool of 5 shrunk "
	"to 2: " + std::to_string( solver.removed.size() ) + " names removed "
	"instead of 2 to 4" );

 const std::unique_ptr< State > small( f->get_State() );
 f->set_par( C05Function::intGPMaxSz , 5 );
 double g;
 value( f , 1 , g );
 f->store_linearization( 3 );
 f->store_linearization( 4 );
 check( f->is_linearization_there( 3 ) && f->is_linearization_there( 4 ) ,
	"the names 3 and 4 not stored" );
 f->put_State( * small );
 check( ! ( f->is_linearization_there( 3 ) ||
	    f->is_linearization_there( 4 ) ) , "a State of a pool of 2 put "
	"into one of 5: the names 3 and 4 still there" );
 delete ib;
 }

/*--------------------------------------------------------------------------*/
/// the design within the bounds [ 5 , 10 ] is feasible at 6 and not at 0
/// and 11, read off the ColVariable as off the Constraint; three assets of a
/// component with no AssetVarIndex over two design Variable are refused
/// when the Constraint are generated

static void test_design_feasibility( void )
{
 auto ib = dynamic_cast< InvestmentBlock * >(
			   Block::new_Block( thermal_bus( 100 ) ) );
 ib->set_variable_bounds( { 5 } , { 10 } );
 ib->generate_abstract_variables();
 ib->generate_abstract_constraints();
 for( const auto & [ y , feasible ] :
	std::vector< std::pair< double , bool > >
	{ { 0 , false } , { 6 , true } , { 11 , false } } ) {
  ib->set_variable_values( std::vector< double >{ y } );
  for( const bool abstract : { false , true } )
   check( ib->is_feasible( abstract ) == feasible , "bounds [ 5 , 10 ] at "
	  + std::to_string( y ) +
	  ( feasible ? ": not feasible" : ": feasible" ) +
	  ( abstract ? " by the Constraint" : " by the ColVariable" ) );
  }
 delete ib;

 InvestmentBlock two( nullptr , 2 );
 two.add_component( new InvestmentFunction( nullptr , {} , { 0 , 1 , 2 } ,
		     { InvestmentFunction::eUnitBlock ,
		       InvestmentFunction::eUnitBlock ,
		       InvestmentFunction::eUnitBlock } , { 1 , 1 , 1 } ) );
 two.generate_abstract_variables();
 try {
  two.generate_abstract_constraints();
  check( false , "three assets over two design Variable: not refused" );
  }
 catch( const std::logic_error & e ) {
  check( std::string( e.what() ).find( "and no AssetVarIndex" ) !=
	 std::string::npos , "three assets over two design Variable: "
	 "refused, but saying \"" + std::string( e.what() ) + "\"" );
  }
 }

/*--------------------------------------------------------------------------*/
/// the active Variable of a component are the design Variable its assets
/// and their baselines name, in increasing order, and its mappings point
/// into them: over 3 design Variable, the unit of a component on the third
/// with the first as baseline has the actives ( x0 , x2 ) and the mappings
/// ( 1 ) and ( 0 ); the line and the unit of line_and_unit() on the
/// second have ( x1 ) and the mapping ( 0 , 0 ); a component with no asset
/// has all of them

static void test_component_mappings( void )
{
 auto g = components( 3 , { 0 } );
 auto c0 = g.getGroup( "Component_0" );
 const unsigned int third = 2;
 c0.getVar( "AssetVarIndex" ).putVar( { 0 } , third );
 put_uint( c0 , "AssetBaselineVarIndex" , c0.getDim( "NumAssets" ) , { 0 } );

 auto c1 = g.addGroup( "Component_1" );
 auto na = c1.addDim( "NumAssets" , 2 );
 put_uint( c1 , "Assets" , na , { 0 , 0 } );
 put_uint( c1 , "AssetType" , na , { 1 , 0 } );
 put( c1 , "Cost" , na , { 100 , 100 } );
 put_uint( c1 , "AssetVarIndex" , na , { 1 , 1 } );
 auto u = c1.addGroup( "InnerBlock" );
 u.putAtt( "type" , "UCBlock" );
 thermal_uc( u , 0 , true );

 u = g.addGroup( "Component_2" ).addGroup( "InnerBlock" );
 u.putAtt( "type" , "UCBlock" );
 thermal_uc( u );

 auto ib = dynamic_cast< InvestmentBlock * >( Block::new_Block( g ) );
 check( ib && ( ib->get_investment_functions().size() == 3 ) ,
	"three components with mappings: not read as three" );
 if( ( ! ib ) || ( ib->get_investment_functions().size() != 3 ) ) {
  delete ib;
  return;
  }

 const auto & x = ib->get_variables();
 const auto & f = ib->get_investment_functions();
 const auto actives = [ & x ]( InvestmentFunction * fk ,
			       const std::vector< Index > & which ) {
  if( fk->get_num_active_var() != which.size() )
   return( false );
  for( Index j = 0 ; j < which.size() ; ++j )
   if( fk->get_active_var( j ) != & x[ which[ j ] ] )
    return( false );
  return( true );
  };
 check( actives( f[ 0 ] , { 0 , 2 } ) &&
	( f[ 0 ]->get_asset_variable_indices() ==
	  std::vector< Index >{ 1 } ) &&
	( f[ 0 ]->get_asset_baseline_variable_indices() ==
	  std::vector< Index >{ 0 } ) , "a unit on the third design Variable "
	"with the first as baseline: not the actives ( x0 , x2 ) with the "
	"mappings ( 1 ) and ( 0 )" );
 check( actives( f[ 1 ] , { 1 } ) &&
	( f[ 1 ]->get_asset_variable_indices() ==
	  std::vector< Index >{ 0 , 0 } ) , "two assets on the second design "
	"Variable: not the actives ( x1 ) with the mapping ( 0 , 0 )" );
 check( actives( f[ 2 ] , { 0 , 1 , 2 } ) , "a component with no asset: not "
	"all the design Variable as actives" );
 delete ib;
 }

/*--------------------------------------------------------------------------*/
/// the limits on the change of a design Variable over its baseline: with 1
/// installed and MaxInvestment 2 the first is at most 3, and over the first
/// with MaxInvestment 3 the second is at most 3 above it, read off the
/// ColVariable as off the Constraint, where a third component gives the
/// second the same limit over the same baseline; one giving it another
/// limit is refused

static void test_variation_limits( void )
{
 const auto limited = []( double third ) {
  auto g = components( 2 , { 0 , 1 , 2 } );
  auto c = g.getGroup( "Component_0" );
  put( c , "InstalledQuantity" , 1 );
  put( c , "MaxInvestment" , 2 );
  for( const std::string name : { "Component_1" , "Component_2" } ) {
   c = g.getGroup( name );
   const unsigned int second = 1;
   c.getVar( "AssetVarIndex" ).putVar( { 0 } , second );
   put_uint( c , "AssetBaselineVarIndex" , c.getDim( "NumAssets" ) , { 0 } );
   put( c , "MaxInvestment" , name == "Component_1" ? 3 : third );
   }
  return( g );
  };

 auto ib = dynamic_cast< InvestmentBlock * >(
			   Block::new_Block( limited( 3 ) ) );
 ib->generate_abstract_variables();
 ib->generate_abstract_constraints();
 for( const auto & [ x0 , x1 , feasible ] :
	std::vector< std::tuple< double , double , bool > >
	{ { 2 , 4 , true } , { 4 , 5 , false } , { 2 , 6 , false } } ) {
  ib->set_variable_values( std::vector< double >{ x0 , x1 } );
  for( const bool abstract : { false , true } )
   check( ib->is_feasible( abstract ) == feasible , "variation limits at ( "
	  + std::to_string( x0 ) + " , " + std::to_string( x1 ) +
	  ( feasible ? " ): not feasible" : " ): feasible" ) +
	  ( abstract ? " by the Constraint" : " by the ColVariable" ) );
  }
 delete ib;

 check_refused( limited( 5 ) , "different MaxInvestment" , "two components "
		"limiting the change of one design Variable differently" );
 }

/*--------------------------------------------------------------------------*/
/*---------------------------------- MAIN ----------------------------------*/
/*--------------------------------------------------------------------------*/
/// an inner Block in design mode, here a UCBlock with a ThermalUnitBlock
/// that has an investment cost of its own, is refused when it is read (and
/// new_Block() gives nullptr, having said why), and the same UCBlock with no
/// investment cost in the unit is not

static void test_design_mode_refused( void )
{
 auto b = Block::new_Block( thermal_bus( 100 , 7 ) );
 check( ! b , "design mode: an inner UCBlock with a unit having an "
        "investment cost is not refused" );
 delete b;

 b = Block::new_Block( thermal_bus( 100 ) );
 check( b , "design mode: the inner UCBlock with no investment cost is "
        "refused" );
 delete b;
 }

/*--------------------------------------------------------------------------*/
/// an inner AbstractBlock of two UCBlock is split, and its :Solution is
/// written and read as that of one inner Block

static void test_split_solution( void )
{
 netCDF::NcGroup inner;
 auto g = investment( 0 , 0 , 100 , 0 , inner );
 inner.putAtt( "type" , "AbstractBlock" );
 inner.addDim( "NumberInnerBlock" , 2 );
 for( int i = 0 ; i < 2 ; ++i ) {
  auto u = inner.addGroup( "Block_" + std::to_string( i ) );
  u.putAtt( "type" , "UCBlock" );
  thermal_uc( u , 0 );
  }

 auto ib = dynamic_cast< InvestmentBlock * >( Block::new_Block( g ) );
 check( ib && ( ib->get_number_nested_Blocks() == 2 ) ,
        "split Solution: the inner AbstractBlock is not split into two "
        "components" );
 if( ! ib )
  return;

 const auto blocks = ib->get_whole_inner_blocks();
 check( ( blocks.size() == 1 ) &&
        dynamic_cast< AbstractBlock * >( blocks.front() ) ,
        "split Solution: the whole inner Blocks are not the AbstractBlock" );

 auto sol = ib->get_Solution( nullptr , false );
 auto sg = new_group( "IBSolution" );
 sol->serialize( sg );
 check( ( ! sg.getGroup( "InnerSolution" ).isNull() ) &&
        sg.getDim( "NumInnerSolutions" ).isNull() ,
        "split Solution: not written as one InnerSolution group" );

 auto back = Solution::new_Solution( sg );
 check( back , "split Solution: not read back" );
 if( back ) {
  try {
   back->write( ib );
   }
  catch( const std::exception & e ) {
   check( false , std::string( "split Solution: not written back, " ) +
          e.what() );
   }
  }

 delete back;
 delete sol;
 delete ib;
 }

/*--------------------------------------------------------------------------*/

int main( void )
{
 try {
  test_curvature_empty();
  #ifdef HAVE_UCBLOCK
   test_design_mode_refused();
   test_split_solution();
   test_names_refused();
   test_fields_refused();
   test_structure_refused();
   test_second_size_refused();
   test_round_trip_components();
   test_design_feasibility();
   test_component_mappings();
   test_variation_limits();
   #ifdef HAVE_MILPSOLVER
    test_curvature();
    test_meshed_line();
    test_scaled_reactive();
    test_scaled_zero_copies();
    test_battery_two_assets();
    test_scaled_conversion();
    test_round_trip();
    test_feasibility_cut();
    test_compute_errors();
    test_unbounded();
    test_remove_variables();
    test_weight_changed();
    test_global_pool();
   #else
    std::cout << "no :MILPSolver in the build: the linearizations are not "
              << "checked" << std::endl;
   #endif
  #else
   std::cout << "no UCBlock in the build: no InvestmentBlock with an inner "
             << "Block is checked" << std::endl;
  #endif
  }
 catch( const std::exception & e ) {
  std::cout << "FAILED: exception " << e.what() << std::endl;
  return( 1 );
  }

 if( NCID >= 0 )
  nc_close( NCID );

 if( failures ) {
  std::cout << failures << " checks FAILED" << std::endl;
  return( 1 );
  }

 std::cout << "all checks passed" << std::endl;
 return( 0 );
 }

/*--------------------------------------------------------------------------*/
/*------------------------- End File test.cpp ------------------------------*/
/*--------------------------------------------------------------------------*/
