/*--------------------------------------------------------------------------*/
/*---------------------------- File test.cpp -------------------------------*/
/*--------------------------------------------------------------------------*/
/** @file
 * Unit test of InvestmentFunction on instances built in memory.
 *
 * The curvature an InvestmentFunction declares is checked first: without an
 * inner Block it is neither convex nor concave, and (if a :MILPSolver is in
 * the build, which the inner Block needs) with a minimizing inner Block it
 * is convex only if the cost of investing plus the cost of disinvesting is
 * nonnegative for every asset.
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
 * - an investment in the storage and in the converter of a BatteryUnitBlock
 *   as two assets (types 0 and 2), whose optimum has a storage twice the
 *   converter: the value at the optimum, the coefficients where one of the
 *   two binds and where all the fences meet at ( 0 , 0 ), and the single
 *   kappa of the battery, which resizes both and does worse.
 *
 * The test needs nothing but the module, its dependencies and, for the
 * second part, a :MILPSolver; the netCDF groups are written in a dataset in
 * memory, and the Configuration files are read from the working directory.
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
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <netcdf>

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
/// value is 2500 - 300 x up to x = 5 and 500 + 100 x beyond

static netCDF::NcGroup thermal_bus( double c )
{
 netCDF::NcGroup u;
 auto g = investment( 0 , 0 , c , 0 , u );

 u.addDim( "TimeHorizon" , 1 );
 u.addDim( "NumberUnits" , 2 );
 auto ng = u.addDim( "NumberElectricalGenerators" , 2 );
 auto nn = u.addDim( "NumberNodes" , 1 );
 auto th = u.getDim( "TimeHorizon" );

 put_uint( u , "GeneratorNode" , ng , { 0 , 0 } );
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
 put_int( t , "InitUpDownTime" , 1 );
 put_int( t , "MinUpTime" , 1 );
 put_int( t , "MinDownTime" , 1 );

 slack( u.addGroup( "UnitBlock_1" ) , 100 , 50 );
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
/*---------------------------------- MAIN ----------------------------------*/
/*--------------------------------------------------------------------------*/

int main( void )
{
 try {
  test_curvature_empty();
  #ifdef HAVE_MILPSOLVER
   test_curvature();
   test_meshed_line();
   test_scaled_reactive();
   test_scaled_zero_copies();
   test_battery_two_assets();
  #else
   std::cout << "no :MILPSolver in the build: the linearizations are not "
             << "checked" << std::endl;
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
