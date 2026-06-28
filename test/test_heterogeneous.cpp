/*--------------------------------------------------------------------------*/
/* test_heterogeneous.cpp — local dev test (git-excluded).                    */
/*                                                                            */
/* Genuinely DIFFERENT components (not the identical-component sums of        */
/* test_equivalence). Component 0 loads the base instance, component 1 loads  */
/* instance_hetero.nc (same data, inner demand x0.8), so F_0 != F_1. The      */
/* disaggregated objective must equal the weighted sum of the components,     */
/* re-evaluated at the solved point:                                          */
/*     v(master) == sum_k w_k * F_k(point).                                   */
/* f_k->get_value() returns w_k*F_k after a compute() (the weight is applied  */
/* at the source), so summing the freshly re-computed component values must   */
/* reproduce the master objective. We also assert F_0 != F_1 so the check is  */
/* genuinely heterogeneous, not a degenerate identical-component case.        */
/*                                                                            */
/*   (free)   x optimized: consistency holds at the free optimum x*.          */
/*   (pinned) x fixed at 0 (lb=ub=0): a deterministic, convergence-free check  */
/*            that also exercises the forced-bounds path; with x pinned there */
/*            is no Jensen gap, so v is exactly the weighted average of the   */
/*            per-scenario zero-investment costs.                            */
/*--------------------------------------------------------------------------*/

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <vector>

#include "investment_test_common.h"

using namespace SMSpp_di_unipi_it;

int main( int argc , char ** argv )
{
 const std::string base = ( argc > 1 ) ? argv[ 1 ]
   : "../../InvestmentBlock/data/nc4/resilient-data/smspp_2n_2c_1g_1lext.nc";
 const std::string hetero = ( argc > 2 ) ? argv[ 2 ]
   : "../../InvestmentBlock/test/instance_hetero.nc";
 const std::string bcfg       = "InnerBCfg.txt";
 const std::string scfg_osimp = "../../InvestmentBlock/test/BSPar_osimp.txt";
 const std::string ucs        = "BSCfg1.txt";

 try {
  netCDF::NcFile fb , fh;
  auto gb = ib_group( fb , base );
  auto gh = ib_group( fh , hetero );

  const auto dims = design_dims( gb );   // base and hetero share dimensions
  const Index n = dims.n;

  std::cout << std::setprecision( 12 )
            << "[test_heterogeneous] different scenarios, v == sum_k w_k F_k\n";

  // build K components over the given scenario groups, solve, re-evaluate each
  // component at the solved point, and check the decomposition is consistent
  auto run_case = [ & ]( const std::vector< double > & w ,
                         const std::vector< netCDF::NcGroup > & scen ,
                         bool pin_zero , const char * tag ) {
   const Index K = w.size();
   auto ib = new InvestmentBlock( nullptr , n );
   std::vector< InvestmentFunction * > funcs;
   for( Index k = 0 ; k < K ; ++k ) {
    auto fk = new InvestmentFunction();
    ib->add_component( fk , w[ k ] );
    funcs.push_back( fk );
    }
   for( Index k = 0 ; k < K ; ++k ) {
    funcs[ k ]->set_variables( active_variables( ib ) );
    funcs[ k ]->deserialize( scen[ k ] );
    }
   if( pin_zero )
    ib->set_variable_bounds( std::vector< double >( n , 0.0 ) ,
                             std::vector< double >( n , 0.0 ) );
   else
    ib->set_variable_bounds( dims.lb , dims.ub );

   const double v = solve_block( ib , funcs , bcfg , scfg_osimp , ucs );
   if( ! pin_zero )
    get_solution( ib );   // load x* into the design variables before re-eval

   double sum = 0.0;
   std::vector< double > Fk( K );
   for( Index k = 0 ; k < K ; ++k ) {
    funcs[ k ]->compute( true );
    const double Vk = funcs[ k ]->get_value();   // = w_k * F_k(point)
    sum += Vk;
    Fk[ k ] = Vk / w[ k ];
    }

   const bool hetero_ok =
    std::abs( Fk[ 0 ] - Fk[ 1 ] ) > 1e-3 * std::max( 1.0 , std::abs( Fk[ 0 ] ) );
   const double diff = std::abs( v - sum );
   const double tol  = 1e-4 * std::max( 1.0 , std::abs( v ) );
   const bool ok = hetero_ok && ( diff <= tol );

   std::cout << "  " << tag << " K=" << K << ": v=" << v
             << " sum_k w_k F_k=" << sum << " (diff=" << diff << ")"
             << "  F_0=" << Fk[ 0 ] << " F_1=" << Fk[ 1 ]
             << ( hetero_ok ? " (heterogeneous)" : " (EQUAL!)" )
             << "  -> " << ( ok ? "ok" : "FAIL" ) << "\n";
   delete ib;
   return( ok );
   };

  bool all_ok = true;
  // (free) K=2 weights {2,3}, base vs 0.8x-demand, x optimized
  all_ok &= run_case( { 2.0 , 3.0 } , { gb , gh } , false , "(free)  " );
  // (pinned) K=3 weights {1,2,3}, base/0.8x/base, x fixed at 0
  all_ok &= run_case( { 1.0 , 2.0 , 3.0 } , { gb , gh , gb } , true , "(pinned)" );

  std::cout << ( all_ok ? "  PASS" : "  FAIL" ) << std::endl;
  return( all_ok ? 0 : 1 );
  }
 catch( const std::exception & e ) {
  std::cerr << "[test_heterogeneous] ERROR: " << e.what() << std::endl;
  return( 2 );
  }
 }
