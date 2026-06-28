/*--------------------------------------------------------------------------*/
/* test_equivalence.cpp — local dev test (git-excluded).                      */
/*                                                                            */
/* The disaggregated sum reproduces the legacy single-function optimum,       */
/* scaled by sum(w). For K identical weighted components the objective is     */
/* (sum_k w_k) * F, whose minimizer is the legacy x* (a positive scale does   */
/* not move the argmin). So for every (K, weights):                          */
/*     value == (sum_k w_k) * v_legacy   AND   x* == x*_legacy,               */
/* and the block must expose exactly K component sub-Blocks. Checking x* (not */
/* just the value) catches a set_weight that scaled the value but not its     */
/* linearizations — the bundle model would then converge to a wrong point.   */
/*                                                                            */
/*   (A) K=1 on the QPPenaltyMP master (BSPar.txt): a single component must    */
/*       reproduce the legacy value and runs license-free (QPPenaltyMP admits */
/*       only a non-decomposable Fi, i.e. K=1).                              */
/*   (B) K=2,3,4 on the OSiMP master: QPPenaltyMP rejects a decomposable Fi,   */
/*       so K>=2 needs the Osi master. Weights summing to 1 reproduce the     */
/*       legacy value exactly; other sums scale it.                          */
/*--------------------------------------------------------------------------*/

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <utility>
#include <vector>

#include "investment_test_common.h"

using namespace SMSpp_di_unipi_it;

int main( int argc , char ** argv )
{
 const std::string instance = ( argc > 1 ) ? argv[ 1 ]
   : "../../InvestmentBlock/data/nc4/resilient-data/smspp_2n_2c_1g_1lext.nc";
 const std::string bcfg       = "InnerBCfg.txt";
 const std::string scfg_qpp   = "BSPar.txt";
 const std::string scfg_osimp = "../../InvestmentBlock/test/BSPar_osimp.txt";
 const std::string ucs        = "BSCfg1.txt";

 try {
  netCDF::NcFile f;
  auto group = ib_group( f , instance );

  // --- legacy reference: value and x* (QPP, single function) ---
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
                                       bcfg , scfg_qpp , ucs );
  const auto x_legacy = get_solution( ib_legacy );

  std::cout << std::setprecision( 12 )
            << "[test_equivalence] disaggregated sum == sum(w) * legacy\n"
            << "  legacy: v=" << v_legacy << "  x*=[ ";
  for( auto xi : x_legacy ) std::cout << xi << " ";
  std::cout << "]  sub-Blocks=" << ib_legacy->get_number_nested_Blocks()
            << " (aggregated)\n";

  bool all_ok = ( ib_legacy->get_number_nested_Blocks() == 0 );

  // build a disaggregated block: one weighted component per entry of w, all
  // over the same InvestmentFunction data and the same design variables
  auto build = [ & ]( const std::vector< double > & w ) {
   auto ib = new InvestmentBlock( nullptr , n );
   std::vector< InvestmentFunction * > funcs;
   for( double wk : w ) {
    auto fk = new InvestmentFunction();
    ib->add_component( fk , wk );
    funcs.push_back( fk );
    }
   for( auto * fk : funcs ) {
    fk->set_variables( active_variables( ib ) );
    fk->deserialize( group );
    }
   ib->set_variable_bounds( lb , ub );
   return( std::make_pair( ib , funcs ) );
   };

  // one (K, weights) check: solve with master \p scfg, compare value to
  // sum(w) * v_legacy and the solution to x*_legacy, assert K sub-Blocks
  auto check = [ & ]( const std::vector< double > & w , const std::string & scfg ,
                      const char * tag ) {
   const Index K = w.size();
   double sumw = 0.0;
   for( double wi : w ) sumw += wi;

   auto [ ib , funcs ] = build( w );
   const double v = solve_block( ib , funcs , bcfg , scfg , ucs );
   const auto x = get_solution( ib );

   const double expected = sumw * v_legacy;
   const double v_diff = std::abs( v - expected );
   const double v_tol  = 1e-4 * std::max( 1.0 , std::abs( expected ) );
   bool ok = ( v_diff <= v_tol ) && ( x.size() == x_legacy.size() )
             && ( ib->get_number_nested_Blocks() == K );
   double x_max_diff = 0.0;
   for( Index i = 0 ; i < x.size() ; ++i ) {
    const double xd = std::abs( x[ i ] - x_legacy[ i ] );
    x_max_diff = std::max( x_max_diff , xd );
    if( xd > 1e-3 * std::max( 1.0 , std::abs( x_legacy[ i ] ) ) )
     ok = false;
    }

   std::cout << "  " << tag << " K=" << K << " sum(w)=" << sumw
             << ": v=" << v << " expected=" << expected
             << " (vdiff=" << v_diff << ", x*diff=" << x_max_diff
             << ", sub-Blocks=" << ib->get_number_nested_Blocks() << ")  "
             << ( ok ? "ok" : "FAIL" ) << "\n";
   delete ib;
   return( ok );
   };

  // (A) K=1 on the license-free QPP master
  all_ok &= check( { 1.0 } , scfg_qpp , "(A,QPP)  " );

  // (B) K>=2 decompositions on the OSiMP master (QPP rejects a decomposable Fi)
  const std::vector< std::vector< double > > configs = {
    { 1.0 , 1.0 } ,            // sum=2 -> 2x legacy
    { 0.4 , 0.6 } ,            // sum=1 -> == legacy
    { 0.2 , 0.3 , 0.5 } ,      // sum=1 -> == legacy
    { 2.0 , 3.0 } ,            // sum=5 -> 5x legacy
    { 1.0 , 1.0 , 1.0 , 1.0 }  // sum=4 -> 4x legacy
    };
  for( const auto & w : configs )
   all_ok &= check( w , scfg_osimp , "(B,OSiMP)" );

  std::cout << ( all_ok ? "  PASS" : "  FAIL" ) << std::endl;
  delete ib_legacy;
  return( all_ok ? 0 : 1 );
  }
 catch( const std::exception & e ) {
  std::cerr << "[test_equivalence] ERROR: " << e.what() << std::endl;
  return( 2 );
  }
 }
