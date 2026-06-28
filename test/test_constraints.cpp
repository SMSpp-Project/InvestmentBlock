/*--------------------------------------------------------------------------*/
/* test_constraints.cpp — local dev test (git-excluded).                      */
/*                                                                            */
/* The asset->variable mapping and the implicit linking constraints, the two  */
/* features that need the OSiMP master (two or more components make the       */
/* objective decomposable, and active implicit constraints emit vertical      */
/* (feasibility) linearizations; the default QPPenaltyMP handles neither).    */
/*                                                                            */
/*   [period_mapping] T=3 components share T design variables but each invests */
/*       in only ONE of them (mapping { t }), built programmatically.          */
/*       (A) identical components, no constraint -> the optimum must be        */
/*           x_0==x_1==x_2==the single-period optimum (proves the mapping: if  */
/*           ignored, every component would read variable 0 and variables 1,2  */
/*           would carry no objective term).                                   */
/*       (B) monotonicity x^(t+1)-x^(t) >= 0 (set_implicit_constraints) must   */
/*           pull a decreasing unconstrained optimum up to a non-decreasing    */
/*           one.                                                             */
/*   [vertical_cut] an instance with an ACTIVE implicit bound 0<=x<=5000 (the  */
/*       unconstrained optimum ~6300 binds). set_weight scales the value and   */
/*       the diagonal linearization but must leave the VERTICAL (domain) cut   */
/*       untouched: across weighted decompositions x* must stay at 5000 and    */
/*       the value scale by sum(w). x*~=5000 (not ~6300) also proves the cut   */
/*       was genuinely exercised.                                             */
/*--------------------------------------------------------------------------*/

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <utility>
#include <vector>

#include "investment_test_common.h"

using namespace SMSpp_di_unipi_it;

/*--------------------------------------------------------------------------*/

static bool case_period_mapping( const std::string & base ,
                                 const std::string & bcfg ,
                                 const std::string & scfg ,
                                 const std::string & ucs )
{
 constexpr Index n = 1;            // 1 asset per period
 constexpr Index T = 3;            // 3 periods
 constexpr Index n_design = T * n; // all components share these T variables
 const std::vector< double > weights = { 1.0 , 0.95 , 0.95 * 0.95 };
 const double COST = 251.0;        // the resilient-data line cost
 const double R = 1e-3;            // relative tolerance on x

 netCDF::NcFile f;
 auto g = ib_group( f , base );
 auto inner_group = g.getGroup( "InnerBlock" );
 if( inner_group.isNull() )
  throw std::runtime_error( "no 'InnerBlock' group in " + base );

 // a fresh InvestmentFunction wrapping a fresh inner UCBlock, investing in
 // line 0 (the resilient-data investable asset) at the given cost
 auto make_func = [ & ]( double cost ) {
  auto inner = Block::new_Block( inner_group , nullptr );
  return new InvestmentFunction( inner , {} , { 0 } ,
                                 { InvestmentFunction::eLine } ,
                                 { cost } , { 0.0 } );
  };

 // every component declares all n_design active variables, but is mapped to
 // invest only in its own period; only the design upper bounds differ (\p ub)
 auto build = [ & ]( bool monotonic , const std::vector< double > & ub ) {
  auto ib = new InvestmentBlock( nullptr , n_design );
  std::vector< InvestmentFunction * > comps;
  for( Index t = 0 ; t < T ; ++t ) {
   auto fk = make_func( COST );
   ib->add_component( fk , weights[ t ] );
   comps.push_back( fk );
   }
  for( Index t = 0 ; t < T ; ++t ) {
   comps[ t ]->set_variables( active_variables( ib ) );
   comps[ t ]->set_asset_variable_indices( { t } );   // period t -> variable t
   }
  if( monotonic ) {
   // (T-1) rows, width n_design:  x^(t+1) - x^(t) >= 0
   InvestmentFunction::MultiVector A( T - 1 ,
    InvestmentFunction::RealVector( n_design , 0.0 ) );
   for( Index t = 0 ; t < T - 1 ; ++t ) {
    A[ t ][ t     ] = -1.0;
    A[ t ][ t + 1 ] =  1.0;
    }
   InvestmentFunction::RealVector clb( T - 1 , 0.0 );
   InvestmentFunction::RealVector cub( T - 1 , Inf< double >() );
   for( auto * fk : comps )
    fk->set_implicit_constraints( A , clb , cub );
   }
  ib->set_variable_bounds( std::vector< double >( n_design , 0.0 ) , ub );
  return( std::make_pair( ib , comps ) );
  };

 bool all_ok = true;
 std::cout << "  [period_mapping] K=T=" << T
           << " disjoint periods (mapping + monotonicity)\n";

 // ---- single-period reference optimum (K=1, 1 variable) ----
 double x_ref = 0.0;
 {
  auto ib = new InvestmentBlock( nullptr , n );
  auto fk = make_func( COST );
  ib->add_component( fk , 1.0 );
  fk->set_variables( active_variables( ib ) );
  ib->set_variable_bounds( { 0.0 } , { 1e7 } );
  solve_block( ib , { fk } , bcfg , scfg , ucs );
  x_ref = get_solution( ib )[ 0 ];
  std::cout << "    single-period reference: x_ref=" << x_ref << "\n";
  delete ib;
 }

 // ---- (A) mapping: identical components, no constraint -> x all == x_ref ----
 {
  auto [ ib , comps ] = build( false , std::vector< double >( n_design , 1e7 ) );
  solve_block( ib , comps , bcfg , scfg , ucs );
  const auto x = get_solution( ib );
  bool eq = true , at_ref = true;
  for( Index t = 0 ; t < T ; ++t ) {
   if( std::abs( x[ t ] - x[ 0 ] ) > R * std::max( 1.0 , std::abs( x[ 0 ] ) ) )
    eq = false;
   if( std::abs( x[ t ] - x_ref ) > R * std::max( 1.0 , std::abs( x_ref ) ) )
    at_ref = false;
   }
  const bool ok = eq && at_ref;
  std::cout << "    (A) no constraint: x=[" << x[ 0 ] << "," << x[ 1 ] << ","
            << x[ 2 ] << "]  all-equal=" << ( eq ? "yes" : "NO" )
            << "  ==x_ref=" << ( at_ref ? "yes" : "NO" )
            << "  -> " << ( ok ? "ok" : "FAIL" ) << "\n";
  all_ok = all_ok && ok;
  delete ib;
 }

 // ---- (B) monotonicity, forced to bind via DECREASING upper bounds ----
 // Per-period caps below the natural optimum (6300) and decreasing in t make
 // the unconstrained optimum x=[5000,3000,1000] (each variable pinned at its
 // own cap). The constraints x^(t+1) >= x^(t) then forbid that profile and
 // must pull the solution up to a non-decreasing one.
 {
  const std::vector< double > caps = { 5000.0 , 3000.0 , 1000.0 };

  auto [ ibu , cu ] = build( false , caps );
  solve_block( ibu , cu , bcfg , scfg , ucs );
  const auto xu = get_solution( ibu );
  const bool decreasing = ( xu[ 0 ] > xu[ 1 ] + 1.0 )
                       && ( xu[ 1 ] > xu[ 2 ] + 1.0 );
  delete ibu;

  auto [ ib , comps ] = build( true , caps );
  solve_block( ib , comps , bcfg , scfg , ucs );
  const auto x = get_solution( ib );
  bool mono = true;
  for( Index t = 0 ; t < T - 1 ; ++t )
   if( x[ t + 1 ] < x[ t ] - R * std::max( 1.0 , std::abs( x[ t ] ) ) )
    mono = false;
  // active iff it actually moved the solution off the unconstrained profile
  const bool active = ( std::abs( x[ 0 ] - xu[ 0 ] )
                      + std::abs( x[ 2 ] - xu[ 2 ] ) ) > 1.0;
  const bool ok = decreasing && mono && active;
  std::cout << "    (B) decreasing caps: uncon x=[" << xu[ 0 ] << "," << xu[ 1 ]
            << "," << xu[ 2 ] << "] (" << ( decreasing ? "decreasing"
                                                       : "NOT decreasing" )
            << "), con x=[" << x[ 0 ] << "," << x[ 1 ] << "," << x[ 2 ]
            << "]  monotone=" << ( mono ? "yes" : "NO" )
            << "  constraint " << ( active ? "ACTIVE" : "inactive" )
            << "  -> " << ( ok ? "ok" : "FAIL" ) << "\n";
  all_ok = all_ok && ok;
  delete ib;
 }

 return( all_ok );
 }

/*--------------------------------------------------------------------------*/

static bool case_vertical_cut( const std::string & instance ,
                               const std::string & bcfg ,
                               const std::string & scfg ,
                               const std::string & ucs )
{
 const double CAP = 5000.0;   // the active implicit upper bound on x
 const std::vector< std::vector< double > > configs = {
   { 2.0 , 3.0 } ,        // sum=5 -> 5x legacy, same constrained x*
   { 0.4 , 0.6 }          // sum=1 -> == legacy
   };

 netCDF::NcFile f;
 auto group = ib_group( f , instance );

 // --- constrained legacy reference ---
 auto ib_legacy = dynamic_cast< InvestmentBlock * >(
                    Block::new_Block( group , nullptr ) );
 if( ! ib_legacy )
  throw std::runtime_error( "the instance is not an InvestmentBlock" );
 const Index n = ib_legacy->get_number_variables();
 const auto lb = ib_legacy->get_variable_lower_bound();
 const auto ub = ib_legacy->get_variable_upper_bound();
 auto func_legacy = dynamic_cast< InvestmentFunction * >(
                      ib_legacy->get_function() );
 const double v_legacy = solve_block( ib_legacy , { func_legacy } ,
                                      bcfg , scfg , ucs );
 const auto x_legacy = get_solution( ib_legacy );

 // the constraint must BIND (x* at the cap, not at the unconstrained ~6300),
 // otherwise the vertical path was not exercised and the check is meaningless
 const bool binds = ( ! x_legacy.empty() ) &&
                    ( std::abs( x_legacy[ 0 ] - CAP ) <= 1e-3 * CAP );

 std::cout << "  [vertical_cut] active implicit constraint x <= " << CAP << "\n"
           << "    legacy: v=" << v_legacy << "  x*=" << x_legacy[ 0 ]
           << ( binds ? "  (constraint ACTIVE -> vertical path exercised)\n"
                      : "  (constraint NOT active -> meaningless!)\n" );

 bool all_ok = binds;
 for( const auto & w : configs ) {
  const Index K = w.size();
  double sumw = 0.0;
  for( double wi : w ) sumw += wi;

  auto ib = new InvestmentBlock( nullptr , n );
  std::vector< InvestmentFunction * > funcs;
  for( Index k = 0 ; k < K ; ++k ) {
   auto fk = new InvestmentFunction();
   ib->add_component( fk , w[ k ] );
   funcs.push_back( fk );
   }
  for( auto * fk : funcs ) {
   fk->set_variables( active_variables( ib ) );
   fk->deserialize( group );
   }
  ib->set_variable_bounds( lb , ub );

  const double v = solve_block( ib , funcs , bcfg , scfg , ucs );
  const auto x = get_solution( ib );

  const double expected = sumw * v_legacy;
  const double v_diff = std::abs( v - expected );
  const double v_tol  = 1e-4 * std::max( 1.0 , std::abs( expected ) );
  const double x_diff = std::abs( x[ 0 ] - x_legacy[ 0 ] );
  const bool ok = ( v_diff <= v_tol ) &&
                  ( x_diff <= 1e-3 * std::max( 1.0 , std::abs( x_legacy[ 0 ] ) ) );

  std::cout << "    K=" << K << " sum(w)=" << sumw << ": v=" << v
            << " expected=" << expected << " x*=" << x[ 0 ]
            << " (vdiff=" << v_diff << ", x*diff=" << x_diff << ")  "
            << ( ok ? "ok" : "FAIL" ) << "\n";
  all_ok = all_ok && ok;
  delete ib;
  }
 delete ib_legacy;
 return( all_ok );
 }

/*--------------------------------------------------------------------------*/

int main( int argc , char ** argv )
{
 const std::string base = ( argc > 1 ) ? argv[ 1 ]
   : "../../InvestmentBlock/data/nc4/resilient-data/smspp_2n_2c_1g_1lext.nc";
 const std::string constrained = ( argc > 2 ) ? argv[ 2 ]
   : "../../InvestmentBlock/test/instance_constrained.nc";
 const std::string bcfg       = "InnerBCfg.txt";
 const std::string scfg_osimp = "../../InvestmentBlock/test/BSPar_osimp.txt";
 const std::string ucs        = "BSCfg1.txt";

 try {
  std::cout << std::setprecision( 12 )
            << "[test_constraints] asset->variable mapping + implicit constraints\n";
  bool all_ok = true;
  all_ok &= case_period_mapping( base , bcfg , scfg_osimp , ucs );
  all_ok &= case_vertical_cut( constrained , bcfg , scfg_osimp , ucs );
  std::cout << ( all_ok ? "  PASS" : "  FAIL" ) << std::endl;
  return( all_ok ? 0 : 1 );
  }
 catch( const std::exception & e ) {
  std::cerr << "[test_constraints] ERROR: " << e.what() << std::endl;
  return( 2 );
  }
 }
