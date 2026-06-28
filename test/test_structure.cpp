/*--------------------------------------------------------------------------*/
/* test_structure.cpp — local dev test (git-excluded).                        */
/*                                                                            */
/* Structural invariants of the disaggregated InvestmentBlock, with NO        */
/* optimization:                                                              */
/*   [add_component] invalid arguments / an invalid Block state must throw     */
/*       (fail loud) instead of building a broken object.                     */
/*   [modification] a real Modification on a shared design variable rises up   */
/*       the Block tree to a Solver registered on the MASTER, and every        */
/*       component re-evaluates against the shared point (no stale cache).     */
/*       set_value() would NOT do — it is solution data and issues no          */
/*       Modification; we use is_fixed(true), which issues an eModBlck one.    */
/*--------------------------------------------------------------------------*/

#include <algorithm>
#include <cmath>
#include <functional>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <Solver.h>

#include "investment_test_common.h"

using namespace SMSpp_di_unipi_it;

/*--------------------------------------------------------------------------*/

// returns true if build() throws std::logic_error (invalid_argument included)
static bool expect_throw( const char * what , const std::function< void() > & build )
{
 try {
  build();
  }
 catch( const std::logic_error & e ) {
  std::cout << "    ok   " << what << "  ->  " << e.what() << "\n";
  return( true );
  }
 catch( const std::exception & e ) {
  std::cout << "    FAIL " << what << "  ->  wrong exception: " << e.what() << "\n";
  return( false );
  }
 std::cout << "    FAIL " << what << "  ->  no exception thrown\n";
 return( false );
 }

static bool case_add_component_checks()
{
 int bad = 0;
 std::cout << "  [add_component] failing-loud validation\n";

 if( ! expect_throw( "no design variables" , [] {
       InvestmentBlock ib( nullptr , 0 );
       ib.add_component( nullptr , 1.0 ); } ) )
  ++bad;

 if( ! expect_throw( "null component" , [] {
       InvestmentBlock ib( nullptr , 1 );
       ib.add_component( nullptr , 1.0 ); } ) )
  ++bad;

 if( ! expect_throw( "weight <= 0" , [] {
       InvestmentBlock ib( nullptr , 1 );
       ib.add_component( new InvestmentFunction() , 0.0 ); } ) )
  ++bad;

 // the multi-component path must refuse bound reformulation (the per-component
 // InvestmentFunction are not told of the shift, so it would silently compute
 // with wrong bounds). f_reformulate_bounds=1 via a SimpleConfiguration<int>;
 // bounds must be non-empty or generate_abstract_constraints returns early.
 if( ! expect_throw( "reformulation refused with multiple components" , [] {
       InvestmentBlock ib( nullptr , 1 );
       ib.add_component( new InvestmentFunction() , 1.0 );
       ib.set_variable_bounds( { 0.0 } , { 10.0 } );
       SimpleConfiguration< int > cfg( 1 );
       ib.generate_abstract_constraints( & cfg ); } ) )
  ++bad;

 return( bad == 0 );
 }

/*--------------------------------------------------------------------------*/

/// a Solver that does not solve anything; it only counts the Modifications it
/// receives, to prove they reach a Solver registered on the master Block
class CountingMockSolver : public Solver {
 public:
  int n_modifications = 0;

  void add_Modification( sp_Mod & mod ) override {
   ++n_modifications;
   Solver::add_Modification( mod );
   }

  int compute( bool changedvars = true ) override { return( Solver::kOK ); }

  void get_var_solution( Configuration * solc = nullptr ) override {}

 private:
  const std::string & private_name( void ) const override {
   static const std::string name = "CountingMockSolver";
   return( name );
   }
 };

static bool case_modification_propagation( const std::string & base ,
                                           const std::string & hetero ,
                                           const std::string & bcfg ,
                                           const std::string & ucs )
{
 const std::vector< double > weights = { 1.0 , 2.0 , 3.0 };

 netCDF::NcFile fb , fh;
 auto gb = ib_group( fb , base );
 auto gh = ib_group( fh , hetero );
 const std::vector< netCDF::NcGroup > scen = { gb , gh , gb };

 const auto dims = design_dims( gb );
 const Index n = dims.n;

 const Index K = weights.size();
 auto ib = new InvestmentBlock( nullptr , n );
 std::vector< InvestmentFunction * > funcs;
 for( Index k = 0 ; k < K ; ++k ) {
  auto fk = new InvestmentFunction();
  ib->add_component( fk , weights[ k ] );
  funcs.push_back( fk );
  }
 for( Index k = 0 ; k < K ; ++k ) {
  funcs[ k ]->set_variables( active_variables( ib ) );
  funcs[ k ]->deserialize( scen[ k ] );
  }
 ib->set_variable_bounds( dims.lb , dims.ub );

 // configure the inner formulation and attach inner Solvers, but DO NOT attach
 // a master Solver: this case drives the components by hand
 configure_block( ib , funcs , bcfg , ucs );

 // baseline component values at the current point (x = 0)
 std::vector< double > before( K );
 for( Index k = 0 ; k < K ; ++k ) {
  funcs[ k ]->compute( true );
  before[ k ] = funcs[ k ]->get_value();
  }

 // (a) Modifications rise up the tree: the mock goes on the MASTER
 auto mock = new CountingMockSolver();
 ib->register_Solver( mock );          // does NOT take ownership

 auto & x0 = ib->get_variables_writable()[ 0 ];
 x0.is_fixed( true );                   // issues an eModBlck Modification
 const bool ok_mod = ( mock->n_modifications > 0 );

 // (b) re-evaluate at a different feasible point -> no stale cache
 x0.is_fixed( false );
 const double step = 1000.0;            // within [lb,ub]; F_k is not flat here
 x0.set_value( x0.get_value() + step );
 bool ok_reval = true;
 for( Index k = 0 ; k < K ; ++k ) {
  funcs[ k ]->compute( true );
  if( std::abs( funcs[ k ]->get_value() - before[ k ] )
      <= 1e-6 * std::max( 1.0 , std::abs( before[ k ] ) ) )
   ok_reval = false;                    // identical value -> stale (or flat)
  }

 std::cout << "  [modification] propagation + no stale cache\n"
           << "    (a) mods reaching master = " << mock->n_modifications
           << "  -> " << ( ok_mod ? "ok" : "FAIL" ) << "\n"
           << "    (b) components re-evaluated at perturbed point = "
           << ( ok_reval ? "ok" : "stale/flat" ) << "\n";

 ib->unregister_Solver( mock );
 delete mock;
 delete ib;
 return( ok_mod && ok_reval );
 }

/*--------------------------------------------------------------------------*/

int main( int argc , char ** argv )
{
 const std::string base = ( argc > 1 ) ? argv[ 1 ]
   : "../../InvestmentBlock/data/nc4/resilient-data/smspp_2n_2c_1g_1lext.nc";
 const std::string hetero = ( argc > 2 ) ? argv[ 2 ]
   : "../../InvestmentBlock/test/instance_hetero.nc";
 const std::string bcfg = "InnerBCfg.txt";
 const std::string ucs  = "BSCfg1.txt";

 try {
  std::cout << std::setprecision( 12 )
            << "[test_structure] structural invariants (no optimization)\n";
  bool all_ok = true;
  all_ok &= case_add_component_checks();
  all_ok &= case_modification_propagation( base , hetero , bcfg , ucs );
  std::cout << ( all_ok ? "  PASS" : "  FAIL" ) << std::endl;
  return( all_ok ? 0 : 1 );
  }
 catch( const std::exception & e ) {
  std::cerr << "[test_structure] ERROR: " << e.what() << std::endl;
  return( 2 );
  }
 }
