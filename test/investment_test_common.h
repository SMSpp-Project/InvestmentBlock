/*--------------------------------------------------------------------------*/
/* investment_test_common.h — shared helpers for the InvestmentBlock tests.   */
/*--------------------------------------------------------------------------*/
#ifndef INVESTMENT_TEST_COMMON_H
#define INVESTMENT_TEST_COMMON_H

#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "InvestmentBlock.h"
#include "InvestmentFunction.h"
#include "common_utils.h"

#include <BlockSolverConfig.h>
// ComputeConfig comes from ThinComputeInterface.h and SimpleConfiguration from
// Configuration.h, both pulled in transitively by InvestmentBlock.h

namespace SMSpp_di_unipi_it {

/// pointers to all the design variables of the InvestmentBlock, in order.
/// Every component declares this same set as its active variables (the
/// BundleSolver disaggregated-mode contract).
inline std::vector< ColVariable * > active_variables( InvestmentBlock * ib )
{
 std::vector< ColVariable * > active;
 for( auto & v : ib->get_variables_writable() )
  active.push_back( & v );
 return( active );
 }

/// read the registered Solver's solution into the design variables and return
/// their values
inline std::vector< double > get_solution( InvestmentBlock * ib )
{
 ib->get_registered_solvers().front()->get_var_solution();
 return( ib->get_variable_values() );
 }

/// configure the inner-Block formulation, attach an inner Solver to every
/// component function, and build the abstract representation — everything that
/// precedes attaching a master Solver. Shared by solve_block() and by tests
/// that drive the components by hand (no master solve).
inline void configure_block( InvestmentBlock * ib ,
                             const std::vector< InvestmentFunction * > & funcs ,
                             const std::string & bcfg_file ,   // inner formulations
                             const std::string & ucs_file )    // inner UCBlock Solver
{
 b_config_Block( ib , Configuration::deserialize( bcfg_file ) , bcfg_file );

 for( auto * func : funcs ) {
  ComputeConfig cc;
  cc.f_extra_Configuration =
   new SimpleConfiguration< std::map< std::string , Configuration * > >(
    { { "BlockSolverConfig" , Configuration::deserialize( ucs_file ) } } );
  func->set_ComputeConfig( & cc );
  }

 ib->generate_abstract_variables();
 ib->generate_abstract_constraints();
 ib->generate_objective();
 }

/// configure the Block (configure_block), attach the outer BundleSolver and
/// return its objective value
inline double solve_block( InvestmentBlock * ib ,
                           const std::vector< InvestmentFunction * > & funcs ,
                           const std::string & bcfg_file ,   // inner formulations
                           const std::string & scfg_file ,   // outer BundleSolver
                           const std::string & ucs_file )    // inner UCBlock Solver
{
 configure_block( ib , funcs , bcfg_file , ucs_file );

 s_config_Block( ib , Configuration::deserialize( scfg_file ) , scfg_file );

 auto solver = ib->get_registered_solvers().front();
 solver->compute();
 return( solver->get_var_value() );
 }

/// open \p path and return its "InvestmentBlock" netCDF group. The caller must
/// keep \p f alive while the returned group is in use (it is a handle into the
/// still-open file).
inline netCDF::NcGroup ib_group( netCDF::NcFile & f , const std::string & path )
{
 read_open_netCDF( f , path );
 auto group = f.getGroup( "InvestmentBlock" );
 if( group.isNull() )
  throw std::runtime_error( "no 'InvestmentBlock' group in " + path );
 return( group );
 }

/// the design dimension and the design-variable bounds of the InvestmentBlock
/// described by \p group, read by deserializing it once (as the legacy
/// reference) and then discarding it
struct DesignDims { Index n; std::vector< double > lb , ub; };

inline DesignDims design_dims( const netCDF::NcGroup & group )
{
 auto ib = dynamic_cast< InvestmentBlock * >( Block::new_Block( group , nullptr ) );
 if( ! ib )
  throw std::runtime_error( "the instance is not an InvestmentBlock" );
 const DesignDims d{ ib->get_number_variables() ,
                     ib->get_variable_lower_bound() ,
                     ib->get_variable_upper_bound() };
 delete ib;
 return( d );
 }

}  // namespace SMSpp_di_unipi_it

#endif  // INVESTMENT_TEST_COMMON_H
